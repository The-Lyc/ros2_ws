#!/usr/bin/env zsh
# Run the chain-composition experiment.
#
# Baselines (default / events_false / events_true) do NOT distinguish NS vs S
# in their dispatch logic, so their behaviour is identical across all four
# compositions. We therefore run each baseline ONCE (under the 3ns1s
# directory) and reuse the same data when reporting the other compositions.
# Only `proposed` is rerun for each of the four compositions.
#
# Total runs: 4 (proposed) + 3 (baselines) = 7  (~7 min).
#
# Prereqs:
#   1. rclcpp must be built with -DEXP_QOS=ON -DEXP_SEP=OFF.
#   2. colcon build --packages-select eva6_composition --symlink-install
#   3. source /opt/src/install/setup.zsh && source /opt/ros2_ws/install/setup.zsh
#
# Every run is pinned to a single CPU core (taskset -c 0). This is critical
# for events_true: without pinning, its timer manager thread runs on a
# different core than the main thread and silently avoids the contention
# that the paper's experiments measured on a single-threaded executor budget.

set -e

OUT_ROOT=${EVA6_OUT_DIR:-/tmp/eva6_composition}
mkdir -p "$OUT_ROOT"
rm -rf "$OUT_ROOT"/*
export EVA6_OUT_DIR="$OUT_ROOT"

COMPOSITIONS=(3ns1s 2ns2s 1ns3s 0ns4s)
BASELINES=(events_false events_true default)
CANONICAL_COMP=3ns1s         # Where baselines are run / stored.

run_one() {
  local comp="$1"
  local exec_name="$2"
  echo
  echo "============================================================"
  echo "  composition=$comp  executor=$exec_name"
  echo "============================================================"
  EVA6_EXEC="$exec_name" taskset -c 0 ros2 run eva6_composition comp_${comp}
}

# 1. Baselines once (under CANONICAL_COMP/).
for exec_name in "${BASELINES[@]}"; do
  run_one "$CANONICAL_COMP" "$exec_name"
done

# 2. Proposed for every composition.
for comp in "${COMPOSITIONS[@]}"; do
  run_one "$comp" proposed
done

echo
echo "============================================================"
echo "All runs complete. Output under $OUT_ROOT/<composition>/<executor>/"
echo "  - $CANONICAL_COMP/{proposed,events_false,events_true,default}/"
echo "  - 2ns2s/proposed/ , 1ns3s/proposed/ , 0ns4s/proposed/"
echo "  - (baselines are shared across compositions via analyze.py)"
echo "============================================================"
ls "$OUT_ROOT"
