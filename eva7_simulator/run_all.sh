#!/usr/bin/env bash
# Run all 4 compositions × 4 executors in the simulator (≈ seconds total).
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${EVA7_OUT_DIR:-/tmp/eva7}"
rm -rf "$OUT_DIR" && mkdir -p "$OUT_DIR"
export EVA7_OUT_DIR="$OUT_DIR"

for comp in 3ns1s 2ns2s 1ns3s 0ns4s; do
  for exe in proposed events_false events_true default; do
    python3 "$SCRIPT_DIR/run_sim.py" --composition "$comp" --executor "$exe" \
                                    --out-dir "$OUT_DIR"
  done
done

echo
echo "Done. Output under $OUT_DIR/<composition>/<executor>/"
