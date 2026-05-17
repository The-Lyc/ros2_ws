# eva7_simulator — Discrete-event simulator for ROS 2 executors

Implements the four executor models described in the paper:

- `DefaultSim`        — paper Section III (default executor, Algorithm 1)
- `EventsFalseSim`    — paper Section IV (events executor, separate=false)
- `EventsTrueSim`     — paper Section IV (events executor, separate=true)
- `ProposedSim`       — paper Section V (decoupled token-bucket design)

The simulator is **pure computation**: no real workload, no OS scheduling,
no DDS. Each callback's "execution" is a virtual `sim_clock += WCET` step,
and the executor's state machine determines which callback runs next.

This decouples the executor logic (under test) from any rclcpp implementation
bugs (e.g. timer drift, index_array overflow, buffer/set state mismatches in
QosPromisedQueue). Baselines are validated against real ROS 2 measurements;
the proposed mechanism is evaluated against its paper specification.

Output files match eva6's format (`frequency_log.txt`, `eelatency_log.txt`,
`activations.txt`) so the existing `latency_stats.py` / `summarize.py`
scripts work unchanged.

## Layout

```
eva7_simulator/
├── README.md            # this file
├── core.py              # DES core: Event, EventQueue, Clock
├── workload.py          # Composition config (period, WCET, QoS, priority)
├── executors.py         # DefaultSim, EventsFalseSim, EventsTrueSim, ProposedSim
├── monitor.py           # output logger (same format as eva6)
├── run_sim.py           # main entry: one composition × one executor
└── run_all.sh           # 4 compositions × 4 executors
```

## Usage

```bash
# Single run
python3 run_sim.py --composition 3ns1s --executor proposed \
                   --out-dir /tmp/eva7

# All 16
bash run_all.sh
```
