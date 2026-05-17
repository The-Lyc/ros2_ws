#!/usr/bin/env python3
"""Summarize eva6_composition runs.

Reads <out_dir>/<composition>/<executor>/{activations.txt, frequency_log.txt}
for all 4 compositions x 4 executors and prints:

  1. Per-chain activation count table (chain-by-chain x executor) for each composition
  2. Whether each chain met its QoS frequency (i.e. observed frequency >= 1/QoS_period
     for the entire run, allowing the first few warm-up windows)
  3. Aggregate metrics:
       - # of NS chains preserved at full count
       - # of chains meeting QoS threshold throughout
       - Total skipped activations
"""
import argparse
import os
import re
import sys
from collections import defaultdict


COMPOSITIONS = ["3ns1s", "2ns2s", "1ns3s", "0ns4s"]
EXECUTORS = ["proposed", "events_false", "events_true", "default"]
WARMUP_SKIP = 2  # ignore the first few frequency samples (window not full yet)


def parse_activations(path):
    info = {}
    chains = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if "=" in line and " " not in line:
                k, v = line.split("=", 1)
                info[k] = v
                continue
            m = re.match(
                r"chain(\d+)\s+period=(\d+)\s+qos_period=(\d+)\s+priority=(\d+)\s+skippable=(\d+)\s+timer_cnt=(\d+)",
                line)
            if m:
                idx = int(m.group(1))
                chains[idx] = {
                    "period": int(m.group(2)),
                    "qos_period": int(m.group(3)),
                    "priority": int(m.group(4)),
                    "skippable": int(m.group(5)),
                    "timer_cnt": int(m.group(6)),
                }
    info["chains"] = chains
    return info


def parse_frequency_log(path):
    """Return per-chain list of (time_ms, freq_hz) for timer events (j == 0)."""
    chain_series = defaultdict(list)
    pat = re.compile(
        r"record time:(\d+)\.\d+,Pos: (\d+),(\d+),Frequency: ([0-9.eE+-]+) Hz")
    with open(path) as f:
        for line in f:
            m = pat.match(line.strip())
            if not m:
                continue
            t = int(m.group(1))
            ci = int(m.group(2))
            cj = int(m.group(3))
            freq = float(m.group(4))
            if cj == 0:
                chain_series[ci].append((t, freq))
    return chain_series


def qos_violation_rate(series, qos_freq_hz):
    """Fraction of samples (after warmup) where observed frequency < qos."""
    if len(series) <= WARMUP_SKIP:
        return None
    samples = series[WARMUP_SKIP:]
    bad = sum(1 for (_, f) in samples if f > 0 and f < qos_freq_hz)
    return bad / len(samples)


def expected_count(period_ms, run_time_ms):
    return (run_time_ms + 1) // period_ms - 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=os.environ.get("EVA6_OUT_DIR",
                                                       "/tmp/eva6_composition"))
    args = ap.parse_args()

    root = args.out_dir
    if not os.path.isdir(root):
        print(f"No such dir: {root}", file=sys.stderr)
        sys.exit(1)

    for comp in COMPOSITIONS:
        print(f"\n{'=' * 78}")
        print(f"  Composition: {comp}")
        print(f"{'=' * 78}")

        # First, read the proposed run to get chain metadata (NS/S, qos, priority).
        meta_path = f"{root}/{comp}/proposed/activations.txt"
        if not os.path.exists(meta_path):
            print(f"  [missing] {meta_path}")
            continue
        meta = parse_activations(meta_path)
        chains = meta["chains"]
        run_ms = int(meta["run_time_ms"])

        # Header showing chain config.
        print(
            f"\n  Chain config (run {run_ms} ms):")
        print(f"  {'chain':>6} {'period':>7} {'QoS_T':>6} {'prio':>5} {'NS/S':>5} {'expect':>7}")
        for ci in [1, 2, 3, 4]:
            c = chains[ci]
            label = "S" if c["skippable"] else "NS"
            print(
                f"  {ci:>6} {c['period']:>7} {c['qos_period']:>6} "
                f"{c['priority']:>5} {label:>5} "
                f"{expected_count(c['period'], run_ms):>7}")

        # Per-executor activation counts.
        print(f"\n  Activation counts per executor:")
        header = f"  {'chain':>6}"
        for exe in EXECUTORS:
            header += f"  {exe:>14}"
        header += f"  {'expected':>10}"
        print(header)
        rows = []
        for ci in [1, 2, 3, 4]:
            row = f"  {ci:>6}"
            exp = expected_count(chains[ci]['period'], run_ms)
            for exe in EXECUTORS:
                ap = f"{root}/{comp}/{exe}/activations.txt"
                if not os.path.exists(ap):
                    row += f"  {'-':>14}"
                else:
                    info = parse_activations(ap)
                    cnt = info["chains"].get(ci, {}).get("timer_cnt", 0)
                    pct = cnt / exp * 100 if exp else 0
                    row += f"  {cnt:>6} ({pct:5.1f}%)"
            row += f"  {exp:>10}"
            print(row)

        # Per-executor QoS-frequency violation rate per chain.
        print(f"\n  QoS-frequency violation rate (fraction of windows below QoS):")
        header = f"  {'chain':>6} {'QoS Hz':>8}"
        for exe in EXECUTORS:
            header += f"  {exe:>14}"
        print(header)
        for ci in [1, 2, 3, 4]:
            qos_T = chains[ci]['qos_period']
            qos_hz = 1000.0 / qos_T
            row = f"  {ci:>6} {qos_hz:>8.3f}"
            for exe in EXECUTORS:
                fp = f"{root}/{comp}/{exe}/frequency_log.txt"
                if not os.path.exists(fp):
                    row += f"  {'-':>14}"
                    continue
                series = parse_frequency_log(fp).get(ci - 1, [])
                # The eva6 PublisherNode uses 0-indexed chain idx, mapping is i=ci-1.
                v = qos_violation_rate([s for s in series], qos_hz)
                if v is None:
                    row += f"  {'n/a':>14}"
                else:
                    row += f"  {v * 100:13.1f}%"
            print(row)

    print()


if __name__ == "__main__":
    main()
