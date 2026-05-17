#!/usr/bin/env python3
"""Compute end-to-end latency statistics for eva6_composition runs.

For each <composition>/<executor>/{frequency_log.txt, eelatency_log.txt}:

  - Pair every callback's START timestamp (frequency_log) with its END
    timestamp (latency_log / eelatency_log) by (chain, callback_pos, index).
  - Recover the *real* intended activation slot from the START timestamp:
        intended_slot = round(T_start / period) * period
    This corrects for ROS 2 rcl_timer skipping behavior (Algorithm 1),
    where the k-th completed callback may correspond to the K'-th intended
    activation slot, K' > k, due to skipped intermediate activations.
  - Corrected end-to-end latency:
        e2e = T_end - intended_slot          (for j = 0, timer cb)
        e2e = T_end - intended_slot          (for j > 0, sub cb of the same instance)

  The chain-end latency (paper Fig.10/11) is the e2e of the LAST sub callback
  in the chain, i.e. the largest j with non-empty topic for that chain.

Outputs a per-(composition, executor, chain) summary: count, mean, median,
p95, p99, max. Optionally writes a CSV of corrected per-instance latencies
for downstream plotting.

Usage:
    python3 latency_stats.py [--out-dir /tmp/eva6_composition] [--csv]
"""
import argparse
import os
import re
import sys
from collections import defaultdict
from statistics import mean, median, quantiles


COMPOSITIONS = ["3ns1s", "2ns2s", "1ns3s", "0ns4s"]
EXECUTORS = ["proposed", "events_false", "events_true", "default"]

# Mirror PublisherNode's chain topology in composition_common.hpp
PERIODS_MS = {1: 80, 2: 100, 3: 160, 4: 120}
# Number of sub callbacks per chain (NOT including the timer itself, j=0).
#   C1: timer + 1 sub  → last_j = 1
#   C2: timer + 2 sub  → last_j = 2
#   C3: timer + 3 sub  → last_j = 3
#   C4: timer + 1 sub  → last_j = 1
LAST_J = {1: 1, 2: 2, 3: 3, 4: 1}

REC_RE = re.compile(
    r"record time:(\d+)\.(\d+),Pos:\s*(\d+),(\d+),Latency:\s*(-?\d+)\s*ms")
FREQ_RE = re.compile(
    r"record time:(\d+)\.(\d+),Pos:\s*(\d+),(\d+),Frequency:\s*[\d.eE+-]+\s*Hz")


def parse_freq_log(path):
    """Return list of (T_start_us, chain_i, callback_j) in record order."""
    out = []
    with open(path) as f:
        for line in f:
            m = FREQ_RE.match(line.strip())
            if not m:
                continue
            ms, us, ci, cj = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))
            T_us = ms * 1000 + us
            out.append((T_us, ci, cj))
    return out


def parse_lat_log(path):
    """Return list of (T_end_us, chain_i, callback_j, recorded_latency_ms)."""
    out = []
    with open(path) as f:
        for line in f:
            m = REC_RE.match(line.strip())
            if not m:
                continue
            ms, us, ci, cj, lat = (int(m.group(1)), int(m.group(2)),
                                   int(m.group(3)), int(m.group(4)),
                                   int(m.group(5)))
            T_us = ms * 1000 + us
            out.append((T_us, ci, cj, lat))
    return out


def pair_start_end(freq_records, lat_records):
    """For each (chain_i, callback_j), zip the freq (start) and lat (end)
    records in order. The k-th freq sample and k-th lat sample for the
    same (i,j) belong to the same callback execution."""
    by_pos_freq = defaultdict(list)
    by_pos_lat = defaultdict(list)
    for t, ci, cj in freq_records:
        by_pos_freq[(ci, cj)].append(t)
    for t, ci, cj, lat in lat_records:
        by_pos_lat[(ci, cj)].append((t, lat))
    paired = defaultdict(list)
    for key in by_pos_freq:
        starts = by_pos_freq[key]
        ends = by_pos_lat.get(key, [])
        n = min(len(starts), len(ends))
        for k in range(n):
            paired[key].append((starts[k], ends[k][0], ends[k][1]))
    return paired


def compute_corrected_e2e(paired):
    """For each (chain_i, callback_j), produce a list of corrected
    end-to-end latencies (in ms) using the actual T_start to recover
    the intended activation slot.

    Note: paper's Fig.10/11 reports the *chain end-to-end* latency, which is
    the e2e of the LAST callback in the chain (j = LAST_J[chain_id]).
    The chain instance's intended activation slot is derived from the
    timer callback's (j=0) start timestamp, which is the truest measure
    of when the chain instance began. To attribute sub callbacks to the
    same chain instance, we use position-in-stream alignment: the k-th
    sub at (i, j>0) corresponds to the k-th timer at (i, 0) — i.e., the
    same chain instance.
    """
    e2e_per_pos = {}
    # First pass: per (i, j=0), figure out each timer instance's intended slot.
    # Each chain's first timer fire is at an OFFSET past time 0 (startup time of
    # create_wall_timer). Use the very first observed T_start as the offset, then
    # snap subsequent T_start values to (offset + k * period) by rounding the
    # quotient. This way the intended slot is always ≤ T_start.
    timer_intended = {}  # (chain_i, k_index) -> intended_slot_us
    for (ci, cj), records in paired.items():
        if cj != 0:
            continue
        period_us = PERIODS_MS[ci + 1] * 1000
        if not records:
            continue
        offset_us = records[0][0]  # first T_start for this chain
        for k, (t_start, t_end, _lat_recorded) in enumerate(records):
            # Number of periods since offset, rounded to nearest integer.
            # Since callbacks fire at or after their intended slot, rounding
            # to nearest correctly identifies the slot even with small jitter.
            n = max(0, round((t_start - offset_us) / period_us))
            slot = offset_us + n * period_us
            # Guarantee non-negative latency: if the recovered slot is in the
            # future relative to T_start (would happen for very-early fires
            # below the rounding boundary), snap back one period.
            if slot > t_start:
                slot -= period_us
            timer_intended[(ci, k)] = slot

    # Second pass: compute corrected latency for every callback
    for (ci, cj), records in paired.items():
        e2e_list = []
        for k, (t_start, t_end, _lat_recorded) in enumerate(records):
            # The k-th callback of (ci, cj) belongs to the chain instance
            # whose intended slot was computed from the k-th timer fire.
            slot = timer_intended.get((ci, k))
            if slot is None:
                # Sub callback without a matching timer record — skip.
                continue
            corrected_e2e_us = t_end - slot
            e2e_list.append(corrected_e2e_us / 1000.0)  # to ms
        e2e_per_pos[(ci, cj)] = e2e_list
    return e2e_per_pos


def summarize_chain(e2e_per_pos):
    """For each chain, take the chain-end latency series (the last sub
    callback) and compute statistics."""
    out = {}
    for ci_minus1 in range(4):
        chain_id = ci_minus1 + 1
        last_j = LAST_J[chain_id]
        series = e2e_per_pos.get((ci_minus1, last_j))
        if not series:
            out[chain_id] = None
            continue
        s = sorted(series)
        n = len(s)
        out[chain_id] = {
            "n": n,
            "mean": mean(s),
            "median": median(s),
            "p95": s[int(n * 0.95)] if n else 0,
            "p99": s[int(n * 0.99)] if n else 0,
            "max": s[-1],
            "min": s[0],
        }
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=os.environ.get(
        "EVA6_OUT_DIR", "/tmp/eva6_composition"))
    ap.add_argument("--csv", action="store_true",
                    help="Also write per-instance corrected e2e to CSV")
    args = ap.parse_args()

    root = args.out_dir
    if not os.path.isdir(root):
        print(f"No such dir: {root}", file=sys.stderr)
        sys.exit(1)

    for comp in COMPOSITIONS:
        print(f"\n{'=' * 88}")
        print(f"  Composition: {comp}  —  chain end-to-end latency (ms)")
        print(f"{'=' * 88}")
        header = (f"  {'executor':>14} {'chain':>6} "
                  f"{'n':>5} {'mean':>8} {'median':>8} {'p95':>8} {'p99':>8} {'max':>8}")
        print(header)
        for exe in EXECUTORS:
            base = os.path.join(root, comp, exe)
            fp = os.path.join(base, "frequency_log.txt")
            lp = os.path.join(base, "eelatency_log.txt")
            if not (os.path.exists(fp) and os.path.exists(lp)):
                print(f"  {exe:>14}  <missing>")
                continue
            freq_rec = parse_freq_log(fp)
            lat_rec = parse_lat_log(lp)
            paired = pair_start_end(freq_rec, lat_rec)
            e2e_per_pos = compute_corrected_e2e(paired)
            stats = summarize_chain(e2e_per_pos)
            for chain_id in [1, 2, 3, 4]:
                s = stats[chain_id]
                if s is None:
                    print(f"  {exe:>14} {chain_id:>6}  <no data>")
                    continue
                print(f"  {exe:>14} {chain_id:>6} "
                      f"{s['n']:>5} {s['mean']:>8.2f} {s['median']:>8.2f} "
                      f"{s['p95']:>8.2f} {s['p99']:>8.2f} {s['max']:>8.2f}")

            if args.csv:
                csv_path = os.path.join(base, "corrected_e2e.csv")
                with open(csv_path, "w") as cf:
                    cf.write("chain,callback_j,instance_k,e2e_ms\n")
                    for (ci, cj), series in e2e_per_pos.items():
                        for k, v in enumerate(series):
                            cf.write(f"{ci+1},{cj},{k},{v:.3f}\n")


if __name__ == "__main__":
    main()
