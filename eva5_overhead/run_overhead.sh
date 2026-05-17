#!/usr/bin/env zsh
# Run both overhead-instrumented binaries back-to-back and summarize per-activation cost.
#
# Both configurations use separate=false so timer events flow through the queue. The only
# difference is the queue type:
#   - overhead_proposed       : QosPromisedQueue (token bucket + dynamic buffer)
#   - overhead_events_false   : SimpleEventsQueue (plain FIFO; no token bucket)
# The cost difference between them isolates the per-activation overhead added by the
# proposed mechanism.
#
# Prereqs:
#   1. rclcpp must be rebuilt with BOTH EXP_QOS=ON and EXP_OVERHEAD=ON, e.g.
#        cd /opt/src && colcon build --packages-select rclcpp \
#            --cmake-args -DEXP_QOS=ON -DEXP_OVERHEAD=ON
#   2. cd /opt/ros2_ws && colcon build --packages-select eva5_overhead --symlink-install
#   3. source /opt/src/install/setup.zsh && source /opt/ros2_ws/install/setup.zsh

set -e

LOG_DIR=${RCLCPP_EXP_OVERHEAD_LOG_DIR:-/tmp/rclcpp_exp_overhead}
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/qos_overhead.txt "$LOG_DIR"/simple_overhead.txt \
      "$LOG_DIR"/proposed_activations.txt "$LOG_DIR"/events_false_activations.txt
export RCLCPP_EXP_OVERHEAD_LOG_DIR="$LOG_DIR"

echo "=== Running proposed (QosPromisedQueue, separate=false) for ~60s ==="
ros2 run eva5_overhead overhead_proposed
echo

echo "=== Running baseline (SimpleEventsQueue, separate=false) for ~60s ==="
ros2 run eva5_overhead overhead_events_false
echo

echo "=== Raw results ==="
echo "--- $LOG_DIR/qos_overhead.txt ---"
cat "$LOG_DIR"/qos_overhead.txt
echo
echo "--- $LOG_DIR/simple_overhead.txt ---"
cat "$LOG_DIR"/simple_overhead.txt
echo
echo "--- $LOG_DIR/proposed_activations.txt ---"
cat "$LOG_DIR"/proposed_activations.txt
echo
echo "--- $LOG_DIR/events_false_activations.txt ---"
cat "$LOG_DIR"/events_false_activations.txt
echo

echo "=== Summary ==="
python3 - <<EOF
import os
log = os.environ["RCLCPP_EXP_OVERHEAD_LOG_DIR"]

def parse(p):
    d = {}
    with open(p) as f:
        for line in f:
            line = line.strip()
            if "=" in line:
                k, v = line.split("=", 1)
                d[k] = v
    return d

qos = parse(f"{log}/qos_overhead.txt")
sim = parse(f"{log}/simple_overhead.txt")
prop_act = parse(f"{log}/proposed_activations.txt")
base_act = parse(f"{log}/events_false_activations.txt")

def total_ns_proposed(d):
    # All work the QosPromisedQueue does per run
    keys = ["enqueue_ns", "dequeue_ns", "increase_buffer_ns", "produce_token_ns"]
    return sum(int(d.get(k, 0)) for k in keys)

def total_ns_simple(d):
    keys = ["enqueue_ns", "dequeue_ns"]
    return sum(int(d.get(k, 0)) for k in keys)

prop_total_ns = total_ns_proposed(qos)
base_total_ns = total_ns_simple(sim)

prop_act_total = int(prop_act.get("total_timer_cnt", 0))
base_act_total = int(base_act.get("total_timer_cnt", 0))

print(f"Proposed (QosPromisedQueue, sep=false):")
print(f"  total queue ns        = {prop_total_ns}")
print(f"  timer activations     = {prop_act_total}")
if prop_act_total:
    prop_pa = prop_total_ns / prop_act_total / 1000.0
    print(f"  per-activation queue cost  = {prop_pa:.3f} us")
print()
print(f"Baseline (SimpleEventsQueue, sep=false):")
print(f"  total queue ns        = {base_total_ns}")
print(f"  timer activations     = {base_act_total}")
if base_act_total:
    base_pa = base_total_ns / base_act_total / 1000.0
    print(f"  per-activation queue cost  = {base_pa:.3f} us")
print()
if prop_act_total and base_act_total:
    delta = prop_pa - base_pa
    rel = (delta / base_pa * 100.0) if base_pa else float("inf")
    print(f"Net overhead added by proposed mechanism:")
    print(f"  absolute  = {delta:.3f} us / activation")
    print(f"  relative  = {rel:.1f} %")
    tput_diff = (prop_act_total - base_act_total) / base_act_total * 100.0
    print(f"  throughput diff (proposed vs baseline) = {tput_diff:+.2f} %")
print()
print(f"Per-method (proposed):")
for k in ["enqueue", "dequeue", "increase_buffer", "produce_token"]:
    ns = int(qos.get(k+"_ns", 0))
    cnt = int(qos.get(k+"_cnt", 0))
    if cnt:
        print(f"  {k}: {ns} ns / {cnt} calls = {ns/cnt:.0f} ns/call")
print(f"Per-method (baseline):")
for k in ["enqueue", "dequeue"]:
    ns = int(sim.get(k+"_ns", 0))
    cnt = int(sim.get(k+"_cnt", 0))
    if cnt:
        print(f"  {k}: {ns} ns / {cnt} calls = {ns/cnt:.0f} ns/call")
EOF
