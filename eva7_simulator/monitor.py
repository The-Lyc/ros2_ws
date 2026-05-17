"""Logging that matches eva6's output format so existing analysis
scripts (latency_stats.py, summarize.py) work unchanged.

Three files per run:
    frequency_log.txt   one line per callback START
                          "record time:T_start_ms.us, Pos: i,j, Frequency: F Hz"
    latency_log.txt     one line per callback END (relative latency)
                          "record time:T_end_ms.us, Pos: i,j, Latency: rel_ms ms"
    eelatency_log.txt   one line per callback END (chain-end-to-end latency)
                          "record time:T_end_ms.us, Pos: i,j, Latency: e2e_ms ms"
    activations.txt     end-of-run summary
"""
from __future__ import annotations

import os
from collections import deque
from typing import Optional


WINDOW_SIZE = 4   # paper Section VI.A: sliding average of last up to 4 executions


class Monitor:
    def __init__(self, out_dir: str, chains: list):
        os.makedirs(out_dir, exist_ok=True)
        self.out_dir = out_dir
        self.chains = chains
        self.freq_fp = open(os.path.join(out_dir, "frequency_log.txt"), "w")
        self.lat_fp  = open(os.path.join(out_dir, "latency_log.txt"), "w")
        self.e2e_fp  = open(os.path.join(out_dir, "eelatency_log.txt"), "w")

        # Per-callback (chain_i, callback_j) state.
        # For frequency: history of recent T_start values for sliding window.
        self.freq_window: dict[tuple[int, int], deque[int]] = {}
        # For activation counts: how many timer callbacks have executed per chain.
        self.timer_cnt = [0, 0, 0, 0]

    # ─── output helpers ────────────────────────────────────────────────────

    @staticmethod
    def _fmt_time_us(t_us: int) -> str:
        # eva6 format: "T_ms.frac"  where frac is the microsecond part (0..999)
        ms, us = divmod(t_us, 1000)
        return f"{ms}.{us}"

    def record_start(self, t_us: int, chain_i: int, callback_j: int) -> None:
        """Called at the START of a callback execution."""
        # Bookkeeping
        if callback_j == 0:
            self.timer_cnt[chain_i] += 1
        key = (chain_i, callback_j)
        win = self.freq_window.setdefault(key, deque(maxlen=WINDOW_SIZE))
        win.append(t_us)
        # Compute sliding-average frequency
        if len(win) >= 2:
            span_us = win[-1] - win[0]
            freq_hz = (len(win) - 1) / (span_us / 1_000_000) if span_us > 0 else 0.0
        else:
            freq_hz = 0.0
        self.freq_fp.write(
            f"record time:{self._fmt_time_us(t_us)},Pos: {chain_i},{callback_j},"
            f"Frequency: {freq_hz} Hz\n"
        )

    def record_end(
        self,
        t_us: int,
        chain_i: int,
        callback_j: int,
        intended_slot_us: int,
        rel_baseline_us: int,
    ) -> None:
        """Called at the END of a callback execution.

        `intended_slot_us` is the chain instance's intended activation time,
        used for the end-to-end latency.
        `rel_baseline_us` is the "ideal finish time of this callback alone"
        (mirroring eva6's finish_time_array), used for the relative latency.
        """
        e2e_ms = (t_us - intended_slot_us) // 1000
        rel_ms = (t_us - rel_baseline_us) // 1000
        self.lat_fp.write(
            f"record time:{self._fmt_time_us(t_us)},Pos: {chain_i},{callback_j},"
            f"Latency: {rel_ms} ms\n"
        )
        self.e2e_fp.write(
            f"record time:{self._fmt_time_us(t_us)},Pos: {chain_i},{callback_j},"
            f"Latency: {e2e_ms} ms\n"
        )

    def write_activations(self, composition: str, executor: str, run_time_us: int) -> None:
        with open(os.path.join(self.out_dir, "activations.txt"), "w") as f:
            f.write(f"composition={composition}\n")
            f.write(f"executor={executor}\n")
            f.write(f"run_time_ms={run_time_us // 1000}\n")
            for i, ch in enumerate(self.chains):
                f.write(
                    f"chain{i+1} period={ch.period_us//1000} "
                    f"qos_period={ch.qos_period_us//1000} "
                    f"priority={ch.priority} "
                    f"skippable={1 if ch.skippable else 0} "
                    f"timer_cnt={self.timer_cnt[i]}\n"
                )
            f.write(f"total_timer_cnt={sum(self.timer_cnt)}\n")

    def close(self) -> None:
        for fp in (self.freq_fp, self.lat_fp, self.e2e_fp):
            fp.close()
