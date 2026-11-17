#!/usr/bin/env bash
# Package the feature scene and render its tank from pak-only assets.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXAMPLES="$REPO_ROOT/examples"
FURY="${FURY_BIN:-$EXAMPLES/fury}"
FURYCLI="${FURY_CLI_BIN:-$EXAMPLES/furye-cli}"
OUT="${OUT_DIR:-/tmp/fury-cluster-lod-packaged}"
case "$OUT" in
    /*) ;;
    *) OUT="$PWD/$OUT" ;;
esac
case "$OUT" in
    ""|/|"$REPO_ROOT"|"$EXAMPLES")
        echo "packaged cluster test: unsafe output directory '$OUT'" >&2
        exit 1
        ;;
esac
KEEP="${PACKAGED_TEST_KEEP:-0}"

[ -x "$FURY" ] || { echo "packaged cluster test: $FURY not found" >&2; exit 1; }
[ -x "$FURYCLI" ] || { echo "packaged cluster test: $FURYCLI not found" >&2; exit 1; }

if [ -e "$OUT" ] && [ "${PACKAGED_TEST_REPLACE:-0}" != "1" ]; then
    echo "packaged cluster test: $OUT exists; set PACKAGED_TEST_REPLACE=1 to replace it" >&2
    exit 1
fi
rm -rf "$OUT"
mkdir -p "$OUT/iso" "$OUT/logs" "$OUT/ddc"
cp "$FURY" "$OUT/iso/fury"

if ! (cd "$EXAMPLES" && FURY_DDC="$OUT/ddc" FURY_CLOD_ROOT="$EXAMPLES/Resource/ClusterLOD" \
        "$FURYCLI" package Projects/feature/feature.bin --ddc "$OUT/ddc" --rhi=metal,vulkan \
        --output "$OUT/iso/feature.pak" > "$OUT/logs/package.log" 2>&1); then
    cat "$OUT/logs/package.log" >&2
    exit 1
fi

for backend in metal vulkan; do
    for mode in dump shot; do
        log="$OUT/logs/${backend}_${mode}.log"
        args=(feature.pak "--rhi=$backend" --render-thread=1)
        if [ "$mode" = dump ]; then
            args+=(--gpu-dump "$OUT/${backend}-gpu-dump" --gpu-dump-frame 30)
        else
            args+=(--screenshot "$OUT/${backend}-frame30.png" --screenshot-frame 30)
        fi
        if ! (cd "$OUT/iso" && FURY_DDC="$OUT/empty-ddc" FURY_CLOD_ROOT="$OUT/empty-clod" \
                FURY_CLUSTER_LOD=1 \
                FURY_CAM=15,8,17,41.43,-15.82,5000 FURY_MAX_FPS=0 \
                ./fury "${args[@]}" > "$log" 2>&1); then
            cat "$log" >&2
            exit 1
        fi
    done
done

python3 - "$OUT" <<'PY'
import pathlib, re, sys
out = pathlib.Path(sys.argv[1])
for backend in ("metal", "vulkan"):
    log = (out / "logs" / (backend + "_dump.log")).read_text(errors="replace")
    if "[EROR]" in log or "GLSL cross-compile failed" in log:
        raise SystemExit(backend + ": renderer or runtime shader error")
    image = out / (backend + "-frame30.png")
    if not image.is_file() or image.stat().st_size == 0:
        raise SystemExit(backend + ": missing screenshot")
    marker = "Cluster diagnostics frame=30"
    if backend == "vulkan" and "count=0" in log:
        if marker in log:
            raise SystemExit("vulkan: cluster work ran without GPU-written indirect counts")
        print("vulkan: packaged legacy fallback PASS (indirect count unavailable)")
        continue
    if marker not in log:
        raise SystemExit(backend + ": missing frame-30 cluster diagnostics")
    stats = log[log.index(marker):]
    for key in ("selectionOverflow", "visibilityOverflow", "mismatches"):
        match = re.search(r"\b" + key + r"=([0-9]+)", stats)
        if not match or int(match.group(1)) != 0:
            raise SystemExit(backend + ": bad " + key)
    for key in ("instances", "batches", "selected", "visible", "commands", "drawnTriangles"):
        match = re.search(r"\b" + key + r"=([0-9]+)", stats)
        if not match or int(match.group(1)) == 0:
            raise SystemExit(backend + ": zero or missing " + key)
    fallback = re.search(r"\bfallbackInstances=([0-9]+)", stats)
    if not fallback or int(fallback.group(1)) != 0:
        raise SystemExit(backend + ": unexpected legacy fallback")
    backend_name = "Metal" if backend == "metal" else "Vulkan"
    for shader in ("ClusterSelect", "ClusterCull"):
        expected = shader + ": " + backend_name + " compute shader DDC hit"
        if expected not in log:
            raise SystemExit(backend + ": missing cooked " + shader)
    if "_cluster: " + backend_name + " shader DDC hit" not in log:
        raise SystemExit(backend + ": missing cooked cluster GBuffer permutation")
    print(backend + ": packaged cluster rendering PASS")
print("packaged tank: supported backends PASS; unsupported Vulkan uses legacy fallback")
PY

if [ "$KEEP" != "1" ]; then
    find "$OUT" -mindepth 1 -maxdepth 1 ! -name 'logs' ! -name '*-frame30.png' ! -name 'summary.txt' -exec rm -rf {} +
fi
printf '%s\n' "packaged tank: PASS" "artifacts: $OUT" | tee "$OUT/summary.txt"
