#!/usr/bin/env python3
"""Run one (composition × executor) simulation and write logs in the same
format as eva6, so the existing analysis scripts (latency_stats.py,
summarize.py) can be reused unchanged.

Usage:
    python3 run_sim.py --composition 3ns1s --executor proposed \
                       --out-dir /tmp/eva7
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from core import Clock                            # noqa: E402
from workload import build_composition, RUN_TIME_US  # noqa: E402
from monitor import Monitor                        # noqa: E402
from executors import EXECUTORS                    # noqa: E402


def run(composition: str, executor: str, out_dir: str) -> None:
    chains = build_composition(composition)
    log_dir = os.path.join(out_dir, composition, executor)
    os.makedirs(log_dir, exist_ok=True)
    monitor = Monitor(log_dir, chains)

    clock = Clock()
    ExecCls = EXECUTORS[executor]
    exec_inst = ExecCls(chains, clock, monitor)

    clock.run(exec_inst.handle)

    monitor.write_activations(composition, executor, RUN_TIME_US)
    monitor.close()

    total = sum(monitor.timer_cnt)
    print(f"[{composition}/{executor}] timer counts: "
          f"{monitor.timer_cnt}  total={total}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--composition", required=True,
                    choices=["3ns1s", "2ns2s", "1ns3s", "0ns4s"])
    ap.add_argument("--executor", required=True,
                    choices=list(EXECUTORS.keys()))
    ap.add_argument("--out-dir", default=os.environ.get(
        "EVA7_OUT_DIR", "/tmp/eva7"))
    args = ap.parse_args()
    run(args.composition, args.executor, args.out_dir)


if __name__ == "__main__":
    main()
