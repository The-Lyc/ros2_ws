"""Workload specification for the simulator.

Strictly matches paper Table I + Section VI.A:

    Chain | Period | Timer WCET | Other callbacks (ms)        | Type   | Util
    C1    | 80 ms  | 2.3        | 16.1                        | NS/S   | 23.0%
    C2    | 100 ms | 20.6       | 17.9, 6.6                   | NS/S   | 45.1%
    C3    | 160 ms | 1.7        | 11.0, 6.6, 7.9              | NS/S   | 17.0%
    C4    | 120 ms | 6.2        | 6.6                         | NS/S   | 10.7%

  QoS periods (Section VI.A): 1.25 × native = {100, 125, 200, 150} ms.
  Burst: every 10th C3 instance, timer cb takes 60 ms instead of 1.7 ms.
  Run time: 60.2 s.

Composition variants differ in priority assignment only:

  3NS+1S : NS={C1,C2,C3} S={C4}    priority={2,2,2,1}
  2NS+2S : NS={C1,C2}    S={C3,C4} priority={2,2,1,1}
  1NS+3S : NS={C1}       S={C2,C3,C4} priority={2,1,1,1}
  0NS+4S : NS={}         S=all     priority={1,1,1,1}

Skippable flag is purely metadata used for reporting; the executor
mechanism distinguishes NS vs S via priority.
"""
from dataclasses import dataclass, field
from typing import List


# ─── Chain config ─────────────────────────────────────────────────────────────

@dataclass
class ChainSpec:
    name: str
    period_us: int          # timer period (microseconds)
    qos_period_us: int      # QoS period (microseconds) — drives LF token rate
    priority: int           # higher value = higher priority
    skippable: bool         # metadata: NS=False, S=True
    timer_wcet_us: int      # timer callback WCET
    sub_wcet_us: List[int]  # sub callback WCETs, in chain order

    @property
    def num_callbacks(self) -> int:
        return 1 + len(self.sub_wcet_us)

    def callback_wcet_us(self, j: int) -> int:
        if j == 0:
            return self.timer_wcet_us
        return self.sub_wcet_us[j - 1]


# Base WCETs in microseconds (mirrors composition_common.hpp SLEEP_TIME)
_BASE_TIMER_WCETS  = [2_300, 20_600, 1_700,  6_200]
_BASE_SUB_WCETS    = [
    [16_100],                           # C1: 1 sub
    [17_900, 6_600],                    # C2: 2 subs
    [11_000, 6_600, 7_900],             # C3: 3 subs
    [6_600],                            # C4: 1 sub
]
PERIODS_US         = [80_000, 100_000, 160_000, 120_000]
# QoS periods are identical across compositions per paper Section VI.A.
QOS_PERIODS_US     = [100_000, 125_000, 200_000, 150_000]

BURST_TIMER_WCET_US = 60_000     # paper: every 10th C3 instance, timer cb takes 60 ms
BURST_INTERVAL      = 10          # every 10th instance
BURST_CHAIN_IDX     = 2           # C3

RUN_TIME_US        = 60_200_000  # 60.2 s


# ─── Composition variants ─────────────────────────────────────────────────────

COMPOSITIONS = {
    "3ns1s": {
        "priorities":  [2, 2, 2, 1],
        "skippables":  [False, False, False, True],
    },
    "2ns2s": {
        "priorities":  [2, 2, 1, 1],
        "skippables":  [False, False, True, True],
    },
    "1ns3s": {
        "priorities":  [2, 1, 1, 1],
        "skippables":  [False, True, True, True],
    },
    "0ns4s": {
        "priorities":  [1, 1, 1, 1],
        "skippables":  [True, True, True, True],
    },
}


def build_composition(name: str) -> List[ChainSpec]:
    if name not in COMPOSITIONS:
        raise ValueError(f"Unknown composition: {name}")
    cfg = COMPOSITIONS[name]
    chains = []
    for i in range(4):
        chains.append(ChainSpec(
            name=f"C{i+1}",
            period_us=PERIODS_US[i],
            qos_period_us=QOS_PERIODS_US[i],
            priority=cfg["priorities"][i],
            skippable=cfg["skippables"][i],
            timer_wcet_us=_BASE_TIMER_WCETS[i],
            sub_wcet_us=list(_BASE_SUB_WCETS[i]),
        ))
    return chains


def is_burst_instance(chain_idx: int, instance_k: int) -> bool:
    """Returns True for the instances on which C3's timer is jittered.
    Mirrors `if (i == 2 && (index_array[i][0] % 10 == 9))` in eva6."""
    return chain_idx == BURST_CHAIN_IDX and (instance_k % BURST_INTERVAL) == (BURST_INTERVAL - 1)


def effective_timer_wcet_us(chains: List[ChainSpec], chain_idx: int, instance_k: int) -> int:
    """Return the timer WCET for this specific instance, applying the
    C3 burst injection."""
    if is_burst_instance(chain_idx, instance_k):
        return BURST_TIMER_WCET_US
    return chains[chain_idx].timer_wcet_us
