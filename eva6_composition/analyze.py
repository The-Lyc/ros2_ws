#!/usr/bin/env python3
"""Single source of truth: callbacks.csv → count, frequency, latency.

For each run, callbacks.csv has one row per callback execution:

    chain,j,t_start_us,t_end_us
    0,0,96405,98982
    0,1,99487,115792
    1,0,116332,137273
    ...

All metrics derive from this table:

  * **count**       — number of rows with j=0 grouped by chain.
  * **frequency**   — sliding-average over t_start of consecutive j=0 rows.
  * **latency**     — defined by the user as
        latency = t_end_of_last_sub - idx * period
    where `idx = floor(t_start_of_timer / period)`. This maps each observed
    timer firing to its actual intended activation slot (works correctly
    even when rcl_timer's Algorithm 1 skips intermediate slots — the k-th
    observed fire still lands in whichever slot floor() places it in).

The k-th observed timer (j=0) of a chain is paired with the k-th observed
final-sub (j=LAST_J) of the same chain to form the chain instance whose
end-to-end latency we report.

Usage:
    # Print summary table:
    python3 analyze.py --out-dir /tmp/eva6_composition

    # Also dump per-instance latency CSV per (composition, executor, chain):
    python3 analyze.py --out-dir /tmp/eva6_composition --csv
"""
from __future__ import annotations

import argparse
import csv
import math
import os
import sys
from collections import defaultdict
from statistics import mean, median


COMPOSITIONS = ["3ns1s", "2ns2s", "1ns3s", "0ns4s"]
EXECUTORS = ["proposed", "events_false", "events_true", "default"]
# Baselines do not differentiate NS vs S; their behaviour is identical
# across all compositions. The runner stores them once under
# CANONICAL_COMP and we reuse those CSVs when reporting other compositions.
CANONICAL_COMP = "3ns1s"
BASELINE_EXECUTORS = {"events_false", "events_true", "default"}

PERIODS_US = {0: 80_000, 1: 100_000, 2: 160_000, 3: 120_000}
QOS_PERIODS_US = {0: 100_000, 1: 125_000, 2: 200_000, 3: 150_000}
# Last sub callback index per chain (= chain end-to-end measurement point):
#   C1: timer + 1 sub  → last_j = 1
#   C2: timer + 2 subs → last_j = 2
#   C3: timer + 3 subs → last_j = 3
#   C4: timer + 1 sub  → last_j = 1
LAST_J = {0: 1, 1: 2, 2: 3, 3: 1}

FREQ_WINDOW = 4   # paper Section VI.A: sliding-window average of last 4


# ─── parse ───────────────────────────────────────────────────────────────────

def read_callbacks(path: str) -> dict[tuple[int, int], list[tuple[int, int]]]:
    """Return {(chain_i, callback_j): [(t_start_us, t_end_us), ...]}, sorted."""
    by_pos: dict[tuple[int, int], list[tuple[int, int]]] = defaultdict(list)
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            ci = int(row['chain'])
            cj = int(row['j'])
            ts = int(row['t_start_us'])
            te = int(row['t_end_us'])
            by_pos[(ci, cj)].append((ts, te))
    for k in by_pos:
        by_pos[k].sort()
    return by_pos


# ─── derived metrics ─────────────────────────────────────────────────────────

def timer_count(by_pos, chain_i: int) -> int:
    """Number of TIMER callback invocations observed (j=0)."""
    return len(by_pos.get((chain_i, 0), []))


def chain_completion_count(by_pos, chain_i: int) -> int:
    """Number of CHAIN INSTANCES that ran end-to-end (the chain's final
    sub callback executed). This is the metric that matters for paper
    Fig.9 — many chain instances may have their timer fire but never
    complete the whole chain because the executor is too backed up.
    """
    return len(by_pos.get((chain_i, LAST_J[chain_i]), []))


def chain_e2e_latencies_ms(by_pos, chain_i: int) -> list[float]:
    """Per-instance chain end-to-end latencies (ms), using
        latency = t_end_of_last_sub - floor(t_start_of_timer / period) * period.

    The k-th timer (j=0) is paired with the k-th final-sub (j=LAST_J).
    For default executor with Algorithm 1 skipping, some intended slots
    have no callback at all — those instances are simply absent from the
    list (we never observed them firing, so no latency is reported)."""
    period_us = PERIODS_US[chain_i]
    last_j = LAST_J[chain_i]
    timers = by_pos.get((chain_i, 0), [])
    subs = by_pos.get((chain_i, last_j), [])
    n = min(len(timers), len(subs))
    out = []
    for k in range(n):
        ts_timer, _ = timers[k]
        _, te_sub = subs[k]
        idx = max(0, ts_timer // period_us)
        intended_us = idx * period_us
        e2e_us = te_sub - intended_us
        out.append(e2e_us / 1000.0)
    return out


def chain_completion_frequency_series(by_pos, chain_i: int) -> list[tuple[float, float]]:
    """Sliding-window frequency over CHAIN INSTANCE COMPLETION times.

    This is the user-observable rate: how often does a complete chain
    instance (timer → all subs) actually finish?  Measuring at timer-cb
    START hides queueing delay — even when the executor is severely backed
    up, the timer manager keeps enqueueing at native rate, so consecutive
    T_start values still average near native and frequency 'looks ok'.

    Using the time the chain's LAST sub callback ENDS captures the user's
    experience: when the system is jammed, completions pile up at
    irregular intervals, and the sliding-average frequency drops.

    Returns [(t_end_ms, freq_hz), ...] in time order.
    """
    last_j = LAST_J[chain_i]
    completions = by_pos.get((chain_i, last_j), [])
    out = []
    window: list[int] = []
    for (_ts, te) in completions:
        window.append(te)
        if len(window) > FREQ_WINDOW:
            window.pop(0)
        if len(window) >= 2:
            span_us = window[-1] - window[0]
            freq = (len(window) - 1) / (span_us / 1_000_000) if span_us > 0 else 0.0
        else:
            freq = 0.0
        out.append((te / 1000.0, freq))
    return out


# Keep the old timer-start frequency available for back-compat / debugging.
def timer_frequency_series(by_pos, chain_i: int) -> list[tuple[float, float]]:
    """Sliding-window frequency over timer callback START times.
    Less faithful to user experience than chain_completion_frequency_series
    when the executor is backed up — use that one for QoS analysis."""
    timers = by_pos.get((chain_i, 0), [])
    out = []
    window: list[int] = []
    for (ts, _te) in timers:
        window.append(ts)
        if len(window) > FREQ_WINDOW:
            window.pop(0)
        if len(window) >= 2:
            span_us = window[-1] - window[0]
            freq = (len(window) - 1) / (span_us / 1_000_000) if span_us > 0 else 0.0
        else:
            freq = 0.0
        out.append((ts / 1000.0, freq))
    return out


def qos_violation_rate(
    e2e_latencies_ms: list[float],
    expected_count: int,
    qos_period_us: int,
) -> float | None:
    """Fraction of EXPECTED chain instances that miss the QoS deadline.

    A chain instance is "on-time" iff its end-to-end latency ≤ QoS period.
    Three failure modes all count as violations:

      1. completed instance with latency > QoS period (late completion)
      2. timer fired but the chain didn't complete end-to-end before
         run-end (still in queue / never reached the last sub)
      3. timer never fired at all (proactively shed by the mechanism)

    All three cases miss the QoS deadline equally hard from the consumer's
    perspective; the denominator is therefore the expected number of chain
    instances over the run, not the number that happened to be observed.

    For S chains in `proposed`, shedding (case 3) is the dominant source of
    violation: those slots will never complete within QoS, so they count
    against the rate.
    """
    if expected_count <= 0:
        return None
    qos_period_ms = qos_period_us / 1000.0
    on_time = sum(1 for l in e2e_latencies_ms if l <= qos_period_ms)
    return (expected_count - on_time) / expected_count


def expected_count(period_us: int, run_time_us: int) -> int:
    """Number of intended timer activations in run_time_us."""
    return run_time_us // period_us


# ─── pretty print ────────────────────────────────────────────────────────────

def stats_line(values: list[float], precision: int = 2) -> str:
    if not values:
        return "  <empty>"
    s = sorted(values)
    n = len(s)
    return (
        f"n={n:4d}  mean={mean(s):7.{precision}f}  median={median(s):7.{precision}f}  "
        f"p95={s[int(n * 0.95)]:7.{precision}f}  p99={s[int(n * 0.99)]:7.{precision}f}  "
        f"max={s[-1]:7.{precision}f}"
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=os.environ.get(
        "EVA6_OUT_DIR", "/tmp/eva6_composition"))
    ap.add_argument("--run-time-ms", type=int, default=60_200,
                    help="Run time used by the experiment, for 'expected' counts.")
    ap.add_argument("--csv", action="store_true",
                    help="Also dump per-instance latency CSVs alongside callbacks.csv.")
    args = ap.parse_args()

    run_time_us = args.run_time_ms * 1000

    for comp in COMPOSITIONS:
        print()
        print("=" * 100)
        print(f"  Composition: {comp}")
        print("=" * 100)

        # Header
        print(f"\n  expected counts (= run_time / period):")
        for ci in range(4):
            exp = expected_count(PERIODS_US[ci], run_time_us)
            print(f"    C{ci+1}  period={PERIODS_US[ci]//1000}ms  expected={exp}")

        for exe in EXECUTORS:
            csv_path = os.path.join(args.out_dir, comp, exe, "callbacks.csv")
            note = ""
            # Baselines are shared across compositions: fall back to the
            # canonical run if this composition didn't run them itself.
            if not os.path.exists(csv_path) and exe in BASELINE_EXECUTORS:
                fallback = os.path.join(args.out_dir, CANONICAL_COMP, exe,
                                        "callbacks.csv")
                if os.path.exists(fallback):
                    csv_path = fallback
                    note = f" (shared from {CANONICAL_COMP}/)"
            if not os.path.exists(csv_path):
                print(f"\n  [{exe}] missing callbacks.csv")
                continue
            by_pos = read_callbacks(csv_path)

            print(f"\n  [{exe}]{note}")
            # Header for the per-chain line
            print(f"    {'chain':<4} {'timer/expected':<18} {'complete/timer':<18} "
                  f"{'QoS_viol':<9} e2e_ms (only for completed instances)")
            for ci in range(4):
                tc = timer_count(by_pos, ci)
                cc = chain_completion_count(by_pos, ci)
                exp = expected_count(PERIODS_US[ci], run_time_us)
                tc_pct = 100.0 * tc / exp if exp else 0
                # "completion rate" relative to timers that fired — exposes
                # the case where lots of timers fired but the chain instance
                # never actually finished end-to-end.
                cc_pct = 100.0 * cc / tc if tc else 0
                e2es = chain_e2e_latencies_ms(by_pos, ci)
                # QoS violation = fraction of EXPECTED chain instances that
                # miss the QoS deadline (including shed / incomplete ones).
                viol = qos_violation_rate(e2es, exp, QOS_PERIODS_US[ci])
                viol_str = f"{viol * 100:5.1f}%" if viol is not None else "  n/a"
                print(f"    C{ci+1:<3} {tc:>4d}/{exp:<4d}({tc_pct:5.1f}%)  "
                      f"{cc:>4d}/{tc:<4d}({cc_pct:5.1f}%)  "
                      f"{viol_str:<9} {stats_line(e2es)}")

            if args.csv:
                for ci in range(4):
                    out_csv = os.path.join(args.out_dir, comp, exe,
                                           f"e2e_C{ci+1}.csv")
                    e2es = chain_e2e_latencies_ms(by_pos, ci)
                    with open(out_csv, "w") as f:
                        f.write("instance_idx,e2e_ms\n")
                        for k, v in enumerate(e2es):
                            f.write(f"{k},{v:.3f}\n")


if __name__ == "__main__":
    main()
