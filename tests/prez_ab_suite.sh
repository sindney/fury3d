#!/bin/bash
# tests/prez_ab_suite.sh - vegetation PreZ pass verification
# (change: cloud-quality-veg-prepass, tasks 7.5, 7.6).
#
# Run from anywhere; the script cd's into examples/ itself:
#   bash tests/prez_ab_suite.sh
#
# Cases:
#   image_ab   - palm grove, FURY_VEG_PREZ=1 vs 0 screenshots: images must
#                match (mean abs diff < 0.5, frac of pixels >8/255 < 1%)
#   self_skip  - scene without PreZ vegetation (outdoor_water.bin) with
#                FURY_VEG_PREZ=1 must run and exit clean
#   timing     - 600 timed frames at the palm grove, prez on vs off; PASS
#                when prez is not slower than 5% over baseline.
#                NOTE: the driver's PERF number is os.clock-based = CPU
#                time of the update loop, not GPU frame time; the PreZ
#                pass trades a cheap CPU depth submit for less GPU shading,
#                so GPU-side wins do not show up here.
# Prints PASS/FAIL per case; exit non-zero on any FAIL.

set -u
TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXAMPLES_DIR="$(dirname "$TESTS_DIR")/examples"
IMGPROBE="python3 $TESTS_DIR/tools/imgprobe.py"
OUT=/tmp/prez_suite
CAM_PALM="10000,300,11500,200,10"
mkdir -p "$OUT"
cd "$EXAMPLES_DIR"

PASS=0
FAIL=0
declare -a RESULTS

FILTERS=("$@")
want_case() {
    [ ${#FILTERS[@]} -eq 0 ] && return 0
    local c
    for c in "${FILTERS[@]}"; do
        case "$1" in *"$c"*) return 0;; esac
    done
    return 1
}

report() { # name status detail
    RESULTS+=("$2 $1 :: $3")
    if [ "$2" = "PASS" ]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    printf "%-4s %-28s %s\n" "$2" "$1" "$3"
}

# ---------------------------------------------------------------- case 1: image A/B
if want_case image_ab; then
env FURY_CLOUD_FREEZE=1 FURY_VEG_PREZ=1 FURY_CAM="$CAM_PALM" \
    ./fury ../tests/lua/cloud_visual_check.lua \
    --screenshot "$OUT/prez_on.png" --screenshot-frame 60 > "$OUT/prez_on.log" 2>&1
env FURY_CLOUD_FREEZE=1 FURY_VEG_PREZ=0 FURY_CAM="$CAM_PALM" \
    ./fury ../tests/lua/cloud_visual_check.lua \
    --screenshot "$OUT/prez_off.png" --screenshot-frame 60 > "$OUT/prez_off.log" 2>&1
if [ -s "$OUT/prez_on.png" ] && [ -s "$OUT/prez_off.png" ]; then
    read DM DMAX DFRAC <<< $($IMGPROBE diff "$OUT/prez_on.png" "$OUT/prez_off.png")
    ok=$(python3 -c "print(1 if $DM < 0.5 and $DFRAC < 0.01 else 0)")
    if [ "$ok" = "1" ]; then
        report "prez_image_ab" "PASS" "mean=$DM max=$DMAX frac>8=$DFRAC"
    else
        report "prez_image_ab" "FAIL" "mean=$DM (want <0.5) frac>8=$DFRAC (want <0.01)"
    fi
else
    report "prez_image_ab" "FAIL" "missing screenshot (see $OUT/prez_*.log)"
fi
fi

# ---------------------------------------------------------------- case 2: self-skip on a scene without PreZ vegetation
# (needs a SkyAtmosphere too - the driver asserts one exists;
# outdoor_water.bin has none, outdoor_terrain.bin does)
if want_case self_skip; then
SCENE="Projects/outdoor/outdoor_terrain.bin"
if [ -f "$SCENE" ]; then
    env FURY_CLOUD_FREEZE=1 FURY_VEG_PREZ=1 FURY_SCENE="$SCENE" FURY_EXIT_FRAME=25 \
        ./fury ../tests/lua/cloud_visual_check.lua > "$OUT/self_skip.log" 2>&1
    rc=$?
    if [ $rc -eq 0 ] && grep -q "cloud_visual_check OK" "$OUT/self_skip.log"; then
        report "prez_self_skip" "PASS" "$SCENE ran clean with FURY_VEG_PREZ=1 (exit 0)"
    else
        report "prez_self_skip" "FAIL" "$SCENE exit=$rc (see $OUT/self_skip.log)"
    fi
else
    report "prez_self_skip" "FAIL" "$SCENE not found"
fi
fi

# ---------------------------------------------------------------- case 3: timing (os.clock CPU proxy - see header note)
if want_case timing; then
env FURY_CLOUD_FREEZE=1 FURY_VEG_PREZ=1 FURY_CAM="$CAM_PALM" FURY_EXIT_FRAME=610 \
    ./fury ../tests/lua/cloud_visual_check.lua > "$OUT/timing_on.log" 2>&1
env FURY_CLOUD_FREEZE=1 FURY_VEG_PREZ=0 FURY_CAM="$CAM_PALM" FURY_EXIT_FRAME=610 \
    ./fury ../tests/lua/cloud_visual_check.lua > "$OUT/timing_off.log" 2>&1
MS_ON=$(sed -n 's/.*PERF avg_frame_ms=\([0-9.]*\).*/\1/p' "$OUT/timing_on.log")
MS_OFF=$(sed -n 's/.*PERF avg_frame_ms=\([0-9.]*\).*/\1/p' "$OUT/timing_off.log")
if [ -n "$MS_ON" ] && [ -n "$MS_OFF" ]; then
    ok=$(python3 -c "print(1 if $MS_ON <= $MS_OFF * 1.05 else 0)")
    if [ "$ok" = "1" ]; then
        report "prez_timing" "PASS" "avg_frame_ms prez=$MS_ON baseline=$MS_OFF (CPU proxy, <= +5%)"
    else
        report "prez_timing" "FAIL" "avg_frame_ms prez=$MS_ON baseline=$MS_OFF (> +5% slower)"
    fi
else
    report "prez_timing" "FAIL" "PERF line missing (on='$MS_ON' off='$MS_OFF')"
fi
fi

# ---------------------------------------------------------------- summary
echo "------------------------------------------------------------"
for r in "${RESULTS[@]}"; do echo "$r"; done
echo "============================================================"
echo "prez_ab_suite: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
