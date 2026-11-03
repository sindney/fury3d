#!/bin/bash
# Standalone PreZ CPU-proxy timing probe; not part of the functional test lanes.

set -u
TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXAMPLES_DIR="$(dirname "$TESTS_DIR")/examples"
OUT="${OUT_DIR:-/tmp/prez_perf}"
CAM_PALM="10000,300,11500,200,10"
mkdir -p "$OUT"
cd "$EXAMPLES_DIR" || exit 1

run_timing() {
    env FURY_CLOUD_FREEZE=1 FURY_VEG_PREZ="$1" FURY_CAM="$CAM_PALM" FURY_EXIT_FRAME=610 \
        ./fury ../tests/lua/scenes/cloud_visual_check.lua > "$OUT/timing_$2.log" 2>&1
}

run_timing 1 on || exit 1
run_timing 0 off || exit 1
MS_ON=$(sed -n 's/.*PERF avg_frame_ms=\([0-9.]*\).*/\1/p' "$OUT/timing_on.log")
MS_OFF=$(sed -n 's/.*PERF avg_frame_ms=\([0-9.]*\).*/\1/p' "$OUT/timing_off.log")
if [ -z "$MS_ON" ] || [ -z "$MS_OFF" ]; then
    echo "prez_perf: PERF line missing (on='$MS_ON' off='$MS_OFF')"
    exit 1
fi
python3 -c "print('prez_perf: avg_frame_ms prez=$MS_ON baseline=$MS_OFF ratio=%.3f' % ($MS_ON / $MS_OFF))"
