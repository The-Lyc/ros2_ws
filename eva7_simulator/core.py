"""Discrete-event simulation core.

Everything is in microseconds (integer) to avoid floating-point drift.

The simulator advances `sim_clock` from one event to the next. Each event
has a fire time and a payload describing what should happen at that time.
Events are popped in time order; ties are broken by insertion order
(stable FIFO at the same timestamp).

Executors implement `handle(event)`, which may modify their internal state
and schedule further events via `clock.schedule(...)`.
"""
from __future__ import annotations

import heapq
from dataclasses import dataclass, field
from typing import Any, Optional


# ─── Event types ──────────────────────────────────────────────────────────────

# Internal event kinds. Payload semantics depend on the kind.
TIMER_EXPIRY = "timer_expiry"          # payload: chain_id  (a chain's main timer is due)
LF_TIMER_EXPIRY = "lf_timer_expiry"    # payload: chain_id  (a chain's LF token producer is due)
CALLBACK_DONE = "callback_done"        # payload: (chain_id, callback_j)
SUB_TRIGGERED = "sub_triggered"        # payload: (chain_id, callback_j)  (the previous step published)
RUN_END = "run_end"                    # payload: None


@dataclass(order=True)
class _ScheduledEvent:
    """A heap entry. (time, seq, kind, payload). seq breaks ties FIFO."""
    time_us: int
    seq: int
    kind: str = field(compare=False)
    payload: Any = field(compare=False, default=None)


class Clock:
    """Event queue + current simulated time. Single source of truth for time."""

    def __init__(self):
        self.now_us: int = 0
        self._heap: list[_ScheduledEvent] = []
        self._seq: int = 0
        self._stopped: bool = False

    def schedule(self, delay_us: int, kind: str, payload: Any = None) -> None:
        """Schedule an event `delay_us` microseconds from now."""
        ev = _ScheduledEvent(self.now_us + delay_us, self._seq, kind, payload)
        self._seq += 1
        heapq.heappush(self._heap, ev)

    def schedule_at(self, time_us: int, kind: str, payload: Any = None) -> None:
        """Schedule an event at absolute simulated time `time_us`."""
        if time_us < self.now_us:
            time_us = self.now_us  # already past; fire immediately
        ev = _ScheduledEvent(time_us, self._seq, kind, payload)
        self._seq += 1
        heapq.heappush(self._heap, ev)

    def stop(self) -> None:
        self._stopped = True

    def run(self, handler) -> None:
        """Pop events in time order and call handler(kind, payload).
        Handler may schedule more events via self.schedule(...)."""
        while self._heap and not self._stopped:
            ev = heapq.heappop(self._heap)
            self.now_us = ev.time_us
            handler(ev.kind, ev.payload)


# ─── rcl_timer state (paper Algorithm 1) ──────────────────────────────────────

class RclTimer:
    """Models rcl_timer's next_call_time bookkeeping.

    Behaves as the wait-set / default-executor timer described in paper
    Section III, Algorithm 1: each call to update(now) advances
    next_call_time by one period; if next_call_time is still in the past
    relative to `now`, more periods are skipped until it catches up.

    The number of periods skipped beyond the first is reported as
    `last_periods_skipped`, which the executor uses to record dropped
    activations for paper Fig.9 metrics.
    """

    def __init__(self, period_us: int, first_fire_us: int):
        self.period_us = period_us
        self.next_call_us = first_fire_us
        self.last_call_us = 0
        self.last_periods_skipped = 0    # missed intended activations on the most recent call

    def is_ready(self, now_us: int) -> bool:
        return now_us >= self.next_call_us

    def time_until_ready(self, now_us: int) -> int:
        return max(0, self.next_call_us - now_us)

    def call(self, now_us: int) -> int:
        """Equivalent to rcl_timer_call. Returns the INTENDED activation slot
        this call corresponds to (i.e. the value of next_call_us BEFORE update).
        After this, next_call_us is advanced per Algorithm 1."""
        intended_slot = self.next_call_us
        self.last_call_us = now_us
        new_next = self.next_call_us + self.period_us
        skipped = 0
        # Algorithm 1: while next_call_time < now, advance by another period.
        while new_next < now_us:
            new_next += self.period_us
            skipped += 1
        self.next_call_us = new_next
        self.last_periods_skipped = skipped
        return intended_slot
