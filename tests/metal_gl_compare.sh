#!/bin/bash
# tests/metal_gl_compare.sh - OpenGL vs Metal pixel-parity gate
# (change: add-rhi-layer, task 8.5).
#
# Run from anywhere; the script cd's into examples/ itself:
#   bash tests/metal_gl_compare.sh
#
# Method:
#   The island scene is rendered twice per camera - once on each backend -
#   with the clock pinned (FURY_FIXED_TIME) so the vegetation wind and the
#   ocean waves are at the same phase on both. A third run repeats the
#   OpenGL pass: it is the noise floor (the same backend twice must be
#   bit-identical), and the Metal-vs-OpenGL delta is judged against it.
#
# Env:
#   FURY_CAM_OVERRIDE   replace the camera list with "name:x,y,z,yaw,pitch;..."
#   OUT_DIR             where the screenshots land (default /tmp/metal_gl_cmp)
#
# Cameras: a palm grove (terrain + CSM shadows + alpha-tested foliage), a
# beach vista (ocean + sky + volumetric clouds) and a wide shot over the
# island (long view depth, deep cascades).
#
# Prints PASS/FAIL per case; exit non-zero on any FAIL.

set -u
TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXAMPLES_DIR="$(dirname "$TESTS_DIR")/examples"
IMGPROBE="python3 $TESTS_DIR/tools/imgprobe.py"
OUT="${OUT_DIR:-/tmp/metal_gl_cmp}"
SCENE="$TESTS_DIR/lua/scenes/cloud_visual_check.lua"
FRAME=60
# Tolerance: mean abs diff per pixel and the fraction of pixels differing
# by more than 8/255. The noise floor (opengl vs opengl) must be 0, so any
# non-zero delta is a backend difference; these bounds keep AA-level noise
# out of the gate while catching a wrong pixel.
MAX_MEAN=0.10
MAX_FRAC=0.005

mkdir -p "$OUT"
cd "$EXAMPLES_DIR" || exit 1

if [ -n "${FURY_CAM_OVERRIDE:-}" ]; then
    CAMERAS="$FURY_CAM_OVERRIDE"
else
    CAMERAS="grove:10000,300,11500,200,10
beach:10000,600,10000,225,8
wide:10000,1200,10000,225,-12"
fi

PASS=0
FAIL=0
SKIP=0

report() { # name status detail
    if [ "$2" = "PASS" ]; then PASS=$((PASS+1)); elif [ "$2" = "SKIP" ]; then SKIP=$((SKIP+1)); else FAIL=$((FAIL+1)); fi
    printf "%-4s %-24s %s\n" "$2" "$1" "$3"
}

render() { # backend camera tag
    env FURY_CLOUD_FREEZE=1 FURY_TOD_HOURS=13 FURY_FIXED_TIME=12.5 FURY_CAM="$2" \
        ./fury "$SCENE" --rhi="$1" --screenshot "$OUT/$3.png" --screenshot-frame $FRAME \
        > "$OUT/$3.log" 2>&1
}

echo "=== metal vs opengl parity (scene: ocean_island.bin, frame $FRAME) ==="
while IFS=: read -r name cam; do
    [ -z "$name" ] && continue
    render opengl "$cam" "${name}_gl"
    render opengl "$cam" "${name}_gl2"
    render metal  "$cam" "${name}_mt"

    if [ ! -s "$OUT/${name}_gl.png" ] || [ ! -s "$OUT/${name}_mt.png" ]; then
        report "$name" "FAIL" "no screenshot (see $OUT/${name}_*.log)"
        continue
    fi

    read -r FLOOR_MEAN FLOOR_MAX FLOOR_FRAC <<< $($IMGPROBE diff "$OUT/${name}_gl.png" "$OUT/${name}_gl2.png")
    read -r MEAN MAX FRAC <<< $($IMGPROBE diff "$OUT/${name}_gl.png" "$OUT/${name}_mt.png")

    if [ "$FLOOR_MEAN" != "0.0000" ]; then
        report "$name" "FAIL" "noise floor not zero: gl-vs-gl mean=$FLOOR_MEAN (run not deterministic)"
        continue
    fi

    ok=$(python3 -c "print(1 if $MEAN < $MAX_MEAN and $FRAC < $MAX_FRAC else 0)")
    if [ "$ok" = "1" ]; then
        report "$name" "PASS" "mean=$MEAN max=$MAX frac>8=$FRAC (floor 0.0000)"
    else
        report "$name" "FAIL" "mean=$MEAN (want <$MAX_MEAN) frac>8=$FRAC (want <$MAX_FRAC)"
    fi
done <<< "$CAMERAS"

echo "------------------------------------------------------------"
echo "metal_gl_compare: $PASS passed, $FAIL failed, $SKIP skipped"
[ "$FAIL" -eq 0 ]
