"""Executor simulations.

Each class implements the algorithm from the corresponding paper section.
All four share the same workload spec and produce logs in the same format
so they can be directly compared.

Common conventions:
  * `instance_slot_us` is the chain instance's intended activation time
    (the value rcl_timer reports as `intended_slot` on the timer callback
    that started this chain instance). All subsequent sub callbacks of the
    same instance carry this same value, so chain end-to-end latency is
    `T_end_of_last_callback - instance_slot_us`.
  * `RCL_FIRST_FIRE_US` is the time at which we schedule each chain's
    first TIMER_EXPIRY. We use exactly `period_us` so that the i-th
    intended slot is `(i+1) * period_us`, matching eva6's
    `activation_time_array` initialisation.
"""
from __future__ import annotations

from typing import List, Optional

from core import (Clock, RclTimer,
                  TIMER_EXPIRY, LF_TIMER_EXPIRY, CALLBACK_DONE,
                  SUB_TRIGGERED, RUN_END)
from workload import (ChainSpec, RUN_TIME_US,
                      effective_timer_wcet_us, is_burst_instance)
from monitor import Monitor


RCL_FIRST_FIRE = lambda period_us: period_us  # first fire one period after start


# ─── Base ─────────────────────────────────────────────────────────────────────

class _Executor:
    """Common scaffolding shared by all four executors."""
    name: str = ""

    def __init__(self, chains: List[ChainSpec], clock: Clock, monitor: Monitor):
        self.chains = chains
        self.clock = clock
        self.monitor = monitor
        # rcl_timer state per chain (paper Algorithm 1 lives here)
        self.rcl_timers = [
            RclTimer(c.period_us, RCL_FIRST_FIRE(c.period_us)) for c in chains
        ]
        # Per-chain count of timer callbacks that have actually run.
        # Used to figure out the burst instance number.
        self.timer_instance_executed = [0] * len(chains)

        # Bootstrap: schedule each chain's first TIMER_EXPIRY at its initial
        # next_call_us, plus a global RUN_END at the simulation horizon.
        for i, ch in enumerate(chains):
            clock.schedule_at(self.rcl_timers[i].next_call_us, TIMER_EXPIRY, i)
        clock.schedule_at(RUN_TIME_US, RUN_END, None)

    # ─── helpers ─────────────────────────────────────────────────────────────

    def _wcet_us(self, chain_i: int, callback_j: int, instance_k: int) -> int:
        if callback_j == 0:
            return effective_timer_wcet_us(self.chains, chain_i, instance_k)
        return self.chains[chain_i].sub_wcet_us[callback_j - 1]

    def _rel_baseline_us(self, instance_slot_us: int, chain_i: int, callback_j: int) -> int:
        """Eva6's finish_time_array semantics: per-callback ideal finish =
        intended_slot + per-callback WCET (NOT cumulative). Used for the
        'relative latency' figure. Matches eva6 behavior exactly."""
        return instance_slot_us + self._wcet_us(chain_i, callback_j, 0)

    def _start_callback(self, chain_i: int, callback_j: int,
                        instance_k: int, instance_slot_us: int) -> None:
        """Begin executing a callback. Marks 'CPU' busy, schedules its
        completion event, and writes the START log entry."""
        now = self.clock.now_us
        wcet = self._wcet_us(chain_i, callback_j, instance_k)
        self.monitor.record_start(now, chain_i, callback_j)
        self.clock.schedule(
            wcet, CALLBACK_DONE,
            (chain_i, callback_j, instance_k, instance_slot_us),
        )

    def _finish_callback(self, chain_i: int, callback_j: int,
                         instance_k: int, instance_slot_us: int) -> None:
        """Common end-of-callback bookkeeping: write END log entry, then
        publish to next sub (if any) by enqueueing it."""
        now = self.clock.now_us
        rel_baseline = self._rel_baseline_us(instance_slot_us, chain_i, callback_j)
        self.monitor.record_end(now, chain_i, callback_j, instance_slot_us, rel_baseline)
        # Chain propagation handled by each subclass (they decide where the
        # sub event lands — main queue, timer-mgr queue, dynamic buffer, etc.)

    # Subclasses override:
    def handle(self, kind: str, payload) -> None:
        raise NotImplementedError


# ─── Default executor — paper Section III ─────────────────────────────────────

class DefaultSim(_Executor):
    """SingleThreadedExecutor. Strict wait-set / ready-set / polling
    semantics from paper Section III.

    Sequence:
      1. Polling point: take a snapshot of all released callbacks in the
         wait set, dedup by (chain_i, callback_j), build the ready set.
      2. Processing window: execute every callback in the ready set
         one-by-one, non-preemptively, ordered by (timer first, then sub)
         with registration order within each type.
      3. NEW callbacks released during the processing window go to the
         wait set, NOT the current ready set. They must wait for the next
         polling point.
      4. When the ready set empties, go back to (1).

    rcl_timer.call() at the moment a timer callback enters the ready set
    advances next_call_time per Algorithm 1; sustained executor lag of
    more than one period therefore drops intended activations.
    """
    name = "default"

    def __init__(self, chains, clock, monitor):
        super().__init__(chains, clock, monitor)
        # Wait set bookkeeping: which (chain_i, callback_j) entries have
        # been released but not yet promoted to the ready set.
        self.wait_timer: List[bool] = [False] * len(chains)
        # Subs waiting in wait_set: ordered, deduplicated by (chain_i, j).
        # Each entry is (chain_i, callback_j, instance_k, slot_us).
        self.wait_subs: List[tuple] = []
        self.wait_sub_keys: set = set()
        # Ready set: the snapshot we are currently processing. Same shape.
        self.ready_queue: List[tuple] = []
        self.running: Optional[tuple] = None

    def handle(self, kind, payload):
        if kind == TIMER_EXPIRY:
            self.wait_timer[payload] = True
            # Timer expiries are external events. If the executor is idle
            # AND the ready set is empty, we need to trigger a polling.
            if self.running is None and not self.ready_queue:
                self._poll_and_dispatch()
        elif kind == CALLBACK_DONE:
            self._on_done(payload)
        elif kind == RUN_END:
            self.clock.stop()

    def _on_done(self, payload):
        chain_i, callback_j, instance_k, slot_us = payload
        self._finish_callback(chain_i, callback_j, instance_k, slot_us)
        # Publish to next sub → enters WAIT SET (not current ready set)
        ch = self.chains[chain_i]
        if callback_j < ch.num_callbacks - 1:
            key = (chain_i, callback_j + 1)
            if key not in self.wait_sub_keys:
                self.wait_sub_keys.add(key)
                self.wait_subs.append((chain_i, callback_j + 1, instance_k, slot_us))
        self.running = None
        # Try to keep processing the ready set; if it's empty, poll.
        self._dispatch_or_poll()

    def _dispatch_or_poll(self):
        if self.ready_queue:
            self._dispatch_next()
        else:
            self._poll_and_dispatch()

    def _poll_and_dispatch(self):
        """Take a snapshot of currently-released callbacks (with dedup)
        from the wait set into the ready set, then start the first one.
        Timers are placed first, ordered by chain index; subs follow, also
        in chain (and within-chain position) order."""
        # Timers first
        for i in range(len(self.chains)):
            if self.wait_timer[i]:
                self.wait_timer[i] = False
                # The rcl_timer.call() happens at the polling point (when
                # we observe the timer as "ready" and dispatch it).
                slot_us = self.rcl_timers[i].call(self.clock.now_us)
                # Reschedule next intended expiry. Per Algorithm 1, missed
                # intermediate intended slots are LOST — we don't emit them.
                self.clock.schedule_at(
                    self.rcl_timers[i].next_call_us, TIMER_EXPIRY, i)
                instance_k = self.timer_instance_executed[i]
                self.timer_instance_executed[i] += 1
                self.ready_queue.append((i, 0, instance_k, slot_us))
        # Then subs, dedup'd by (chain_i, callback_j). Already sorted by
        # insertion order which mirrors release order, but spec says
        # registration order; sort by (chain_i, callback_j) explicitly.
        self.wait_subs.sort(key=lambda e: (e[0], e[1]))
        self.ready_queue.extend(self.wait_subs)
        self.wait_subs = []
        self.wait_sub_keys.clear()
        # Start processing
        if self.ready_queue and self.running is None:
            self._dispatch_next()

    def _dispatch_next(self):
        if self.running is not None or not self.ready_queue:
            return
        chain_i, callback_j, instance_k, slot_us = self.ready_queue.pop(0)
        self.running = (chain_i, callback_j, instance_k, slot_us)
        self._start_callback(chain_i, callback_j, instance_k, slot_us)


# ─── Events executor, separate=false — paper Section IV ───────────────────────

class EventsFalseSim(_Executor):
    """Events executor with separate=false. Timer manager wraps every
    released timer as an event and enqueues it into the (single) events
    queue. Main thread processes the queue FIFO.

    Differences from DefaultSim:
      * No timer-vs-sub priority during dequeue — strict FIFO.
      * Timer callbacks still go through rcl_timer (Algorithm 1) for
        next_call_time bookkeeping.
    """
    name = "events_false"

    def __init__(self, chains, clock, monitor):
        super().__init__(chains, clock, monitor)
        # FIFO of (kind, chain_i, callback_j, instance_k, slot_us)
        # kind is 'timer' or 'sub' but they're processed identically here.
        self.events_queue: List[tuple] = []
        self.running: Optional[tuple] = None

    def handle(self, kind, payload):
        if kind == TIMER_EXPIRY:
            self._on_timer_expiry(payload)
        elif kind == CALLBACK_DONE:
            self._on_done(payload)
        elif kind == RUN_END:
            self.clock.stop()

    def _on_timer_expiry(self, chain_i):
        # Timer manager calls rcl_timer immediately (mirrors Algorithm 2 in
        # paper, the timer manager's reorder + dispatch step) — the intended
        # slot we record here is the one this expiry corresponds to.
        slot_us = self.rcl_timers[chain_i].call(self.clock.now_us)
        self.clock.schedule_at(
            self.rcl_timers[chain_i].next_call_us, TIMER_EXPIRY, chain_i)
        instance_k = self.timer_instance_executed[chain_i]
        self.timer_instance_executed[chain_i] += 1
        self.events_queue.append((chain_i, 0, instance_k, slot_us))
        self._maybe_dispatch()

    def _on_done(self, payload):
        chain_i, callback_j, instance_k, slot_us = payload
        self._finish_callback(chain_i, callback_j, instance_k, slot_us)
        ch = self.chains[chain_i]
        if callback_j < ch.num_callbacks - 1:
            self.events_queue.append((chain_i, callback_j + 1, instance_k, slot_us))
        self.running = None
        self._maybe_dispatch()

    def _maybe_dispatch(self):
        if self.running is not None or not self.events_queue:
            return
        chain_i, callback_j, instance_k, slot_us = self.events_queue.pop(0)
        self.running = (chain_i, callback_j, instance_k, slot_us)
        self._start_callback(chain_i, callback_j, instance_k, slot_us)


# ─── Events executor, separate=true — paper Section IV ────────────────────────

class EventsTrueSim(_Executor):
    """Events executor with separate=true. Timer manager runs timer
    callbacks DIRECTLY in its own thread (no events queue for timers).
    Sub callbacks still flow through the main thread's events queue.

    Two 'CPUs':
      * timer_running: which timer cb is executing on the timer manager
      * main_running:  which sub cb is executing on the main thread
    They progress in parallel. Sub events enqueued during timer execution
    are picked up by the main thread independently.
    """
    name = "events_true"

    def __init__(self, chains, clock, monitor):
        super().__init__(chains, clock, monitor)
        self.timer_queue: List[tuple] = []   # timer-mgr-side queue of ready timers
        self.main_queue: List[tuple] = []    # main-thread queue of subs
        self.timer_running: Optional[tuple] = None
        self.main_running: Optional[tuple] = None

    def handle(self, kind, payload):
        if kind == TIMER_EXPIRY:
            self._on_timer_expiry(payload)
        elif kind == CALLBACK_DONE:
            self._on_done(payload)
        elif kind == RUN_END:
            self.clock.stop()

    def _on_timer_expiry(self, chain_i):
        slot_us = self.rcl_timers[chain_i].call(self.clock.now_us)
        self.clock.schedule_at(
            self.rcl_timers[chain_i].next_call_us, TIMER_EXPIRY, chain_i)
        instance_k = self.timer_instance_executed[chain_i]
        self.timer_instance_executed[chain_i] += 1
        self.timer_queue.append((chain_i, 0, instance_k, slot_us))
        self._dispatch_timer()

    def _on_done(self, payload):
        chain_i, callback_j, instance_k, slot_us = payload
        self._finish_callback(chain_i, callback_j, instance_k, slot_us)
        ch = self.chains[chain_i]
        if callback_j < ch.num_callbacks - 1:
            # Sub events always go to main queue (paper IV: only timer cb
            # runs in the separate thread when separate=true).
            self.main_queue.append((chain_i, callback_j + 1, instance_k, slot_us))
        # Clear the right CPU
        if callback_j == 0:
            self.timer_running = None
            self._dispatch_timer()
        else:
            self.main_running = None
        self._dispatch_main()

    def _dispatch_timer(self):
        if self.timer_running is not None or not self.timer_queue:
            return
        chain_i, callback_j, instance_k, slot_us = self.timer_queue.pop(0)
        self.timer_running = (chain_i, callback_j, instance_k, slot_us)
        self._start_callback(chain_i, callback_j, instance_k, slot_us)

    def _dispatch_main(self):
        if self.main_running is not None or not self.main_queue:
            return
        chain_i, callback_j, instance_k, slot_us = self.main_queue.pop(0)
        self.main_running = (chain_i, callback_j, instance_k, slot_us)
        self._start_callback(chain_i, callback_j, instance_k, slot_us)


# ─── Proposed — paper Section V ───────────────────────────────────────────────

class ProposedSim(_Executor):
    """Decoupled token-bucket design from paper Section V.

    Collection: every intended timer activation is collected into the
    dynamic buffer (no skipping at the dispatch level — R1 enforced).

    Dispatch: timer events go through buffer; sub callbacks flow through
    the main thread as usual.

    Release control (paper Section V-B):
      - dual token bucket per chain (HF at native rate, LF at QoS rate)
      - state of a chain: full-rate / mid-rate / low-rate
      - selection priority (P_select_timer):
          1. any chain in low-rate         → highest priority among those
          2. else any chain in mid-rate    → highest priority among those
          3. else idle
      - on token production: HF added on each timer fire, LF added on
        each LF-timer fire. LF bucket allowed to go negative.
      - on consumption: BOTH HF and LF decrement by 1.

    NS-vs-S is expressed via priority: NS chains have a higher priority
    than S chains. Within a tier we tie-break by chain index (any
    deterministic ordering works).
    """
    name = "proposed"

    # Bucket states (paper V-B)
    FULL_RATE = "full"     # HF empty, LF empty
    MID_RATE  = "mid"      # HF non-empty, LF empty  (above QoS, below native)
    LOW_RATE  = "low"      # LF non-empty             (below QoS)

    def __init__(self, chains, clock, monitor):
        super().__init__(chains, clock, monitor)
        # Per-chain state
        self.hf_tokens = [0] * len(chains)
        self.lf_tokens = [0] * len(chains)   # may go negative
        self.buffer    = [0] * len(chains)   # number of pending timer activations
        # Per-chain queue of pending timer instances (slot_us values)
        self.buffer_slots: List[List[int]] = [[] for _ in chains]
        # Sub callbacks live on the main thread; mirror EventsFalseSim's FIFO
        self.main_queue: List[tuple] = []
        self.main_running: Optional[tuple] = None
        # Schedule LF token producers
        for i, ch in enumerate(chains):
            clock.schedule_at(ch.qos_period_us, LF_TIMER_EXPIRY, i)

    def handle(self, kind, payload):
        if kind == TIMER_EXPIRY:
            self._on_timer_expiry(payload)
        elif kind == LF_TIMER_EXPIRY:
            self._on_lf_timer_expiry(payload)
        elif kind == CALLBACK_DONE:
            self._on_done(payload)
        elif kind == RUN_END:
            self.clock.stop()

    # ─── token bucket / state ───────────────────────────────────────────────

    def _state(self, i: int) -> str:
        if self.lf_tokens[i] > 0:
            return self.LOW_RATE
        if self.hf_tokens[i] > 0:
            return self.MID_RATE
        return self.FULL_RATE

    # ─── event handlers ─────────────────────────────────────────────────────

    def _on_timer_expiry(self, chain_i):
        # Collection: paper V says every intended activation is captured —
        # buffer increments on EACH intended fire, not at most one.
        # Run rcl_timer just for next_call_time bookkeeping; intended_slot
        # is the activation time.
        slot_us = self.rcl_timers[chain_i].call(self.clock.now_us)
        # When proposed's collection skips no intended activations, rcl_timer's
        # 'last_periods_skipped' could still be > 0 if we were slow in the sim.
        # Add buffer entries for ALL intended activations that have just passed.
        n_intended = 1 + self.rcl_timers[chain_i].last_periods_skipped
        for k in range(n_intended):
            this_slot = slot_us + k * self.chains[chain_i].period_us
            self.buffer_slots[chain_i].append(this_slot)
            self.buffer[chain_i] += 1
            # Each timer firing produces ONE HF token.
            self.hf_tokens[chain_i] += 1
        # Reschedule next timer expiry
        self.clock.schedule_at(
            self.rcl_timers[chain_i].next_call_us, TIMER_EXPIRY, chain_i)
        self._maybe_release()

    def _on_lf_timer_expiry(self, chain_i):
        # LF tokens are produced at QoS rate.
        # Paper V-B: when LF bucket is negative (timer overshot QoS earlier),
        # adding a token simply resets it to 0 (no movement).
        if self.lf_tokens[chain_i] < 0:
            self.lf_tokens[chain_i] = 0
        else:
            self.lf_tokens[chain_i] += 1
        # Reschedule next LF token production
        self.clock.schedule(
            self.chains[chain_i].qos_period_us, LF_TIMER_EXPIRY, chain_i)
        self._maybe_release()

    def _on_done(self, payload):
        chain_i, callback_j, instance_k, slot_us = payload
        self._finish_callback(chain_i, callback_j, instance_k, slot_us)
        ch = self.chains[chain_i]
        if callback_j < ch.num_callbacks - 1:
            self.main_queue.append((chain_i, callback_j + 1, instance_k, slot_us))
        self.main_running = None
        self._maybe_release()

    # ─── dispatch ───────────────────────────────────────────────────────────

    def _maybe_release(self):
        if self.main_running is not None:
            return
        # Priority of dispatch (matching how the real implementation
        # ordered things): subs in main_queue have already passed through
        # the release controller; release them first to avoid starvation.
        if self.main_queue:
            chain_i, callback_j, instance_k, slot_us = self.main_queue.pop(0)
            self.main_running = (chain_i, callback_j, instance_k, slot_us)
            self._start_callback(chain_i, callback_j, instance_k, slot_us)
            return

        # Otherwise pick from the dynamic buffer using P_select_timer.
        chosen = self._select_from_buffer()
        if chosen is None:
            return  # idle
        chain_i = chosen
        slot_us = self.buffer_slots[chain_i].pop(0)
        self.buffer[chain_i] -= 1
        # Consume one token from BOTH buckets (paper V-B).
        self.hf_tokens[chain_i] -= 1
        self.lf_tokens[chain_i] -= 1
        instance_k = self.timer_instance_executed[chain_i]
        self.timer_instance_executed[chain_i] += 1
        self.main_running = (chain_i, 0, instance_k, slot_us)
        self._start_callback(chain_i, 0, instance_k, slot_us)

    def _select_from_buffer(self) -> Optional[int]:
        """Paper V-B P_select_timer:
            1. if any chain is in low-rate state, pick the highest-priority one.
            2. else if any chain is in mid-rate state, pick the highest-priority one.
            3. else idle.
           Ties within the same priority are broken by chain index (any
           deterministic rule suffices).
           Only chains with buffer > 0 are eligible (we have something to dispatch).
        """
        def pick(state):
            eligible = [
                i for i in range(len(self.chains))
                if self.buffer[i] > 0 and self._state(i) == state
            ]
            if not eligible:
                return None
            eligible.sort(key=lambda i: (-self.chains[i].priority, i))
            return eligible[0]

        return pick(self.LOW_RATE) or pick(self.MID_RATE)


# ─── factory ──────────────────────────────────────────────────────────────────

EXECUTORS = {
    "default":       DefaultSim,
    "events_false":  EventsFalseSim,
    "events_true":   EventsTrueSim,
    "proposed":      ProposedSim,
}
