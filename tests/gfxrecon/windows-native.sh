#!/usr/bin/env bash
# COMMANDS.sh - reproducible commands for OpenSpec add-gpu-cluster-lod task 5.3
# (gfxrecon capture/replay inspection of the tank cluster frame, native Windows).
#
# Environment:
#   OS: Windows 10 Pro (win32), shell: Git Bash
#   GPU: NVIDIA GeForce RTX 5060 Ti
#   GFXReconstruct: 1.0.5-dev (dev:a20d3db), Vulkan Header 1.4.363
#   Engine binary: D:/git/fury3d/examples/fury.exe (branch feature/gpu-cluster-lod)
#
# All paths below are absolute. Run from any Git Bash cwd. The gfxrecon tools
# directory is treated as untrusted input: its executables are only ever
# invoked by absolute path, and nothing is written or executed from inside it.
#
# NOTE on paths: bash here uses /d/... (MSYS) for cd/ls, but the engine and
# gfxrecon tools take native Windows paths (D:\... or D:/...) as arguments.
# Forward slashes work for engine arguments; gfxrecon-cli --args uses the
# Windows form. Environment values that the Vulkan loader reads (VK_LAYER_PATH)
# use the backslash Windows form.

set -u

REPO="D:\\git\\fury3d"
EXAMPLES="${REPO}\\examples"
ART="${REPO}\\test-artifacts\\gfxrecon-windows-native"
ART_M="/d/git/fury3d/test-artifacts/gfxrecon-windows-native"
GFXROOT="/d/Software/gfxrecon-gui-windows-x64/gfxrecon-gui"
GFXCLI="${GFXROOT}/gfxrecon-cli.exe"
GFXREPLAY="${GFXROOT}/tools/gfxrecon-replay.exe"
GFXINFO="${GFXROOT}/tools/gfxrecon-info.exe"
LAYER_WIN='D:\Software\gfxrecon-gui-windows-x64\gfxrecon-gui\tools\layer'
VAL_WIN='D:\Software\gfxrecon-gui-windows-x64\gfxrecon-gui\tools\validation'

# Pinned tank near-camera and cluster enablement (see tests/cluster_lod_render.py).
export FURY_CAM="15,8,17,41.43,-15.82,5000"
SCENE="Projects/feature/feature.bin"
FRAME=30

# ---------------------------------------------------------------------------
# 0. Tool versions (for the record)
# ---------------------------------------------------------------------------
"${GFXREPLAY}" --version

# ---------------------------------------------------------------------------
# 1. Focused uncaptured smoke: ./examples/fury.exe vulkan-indirect-smoke
#    Expected: gpuCount=2 id3=3 id7=3 id11=0. Exit 0 == PASS. The probe prints
#    the line to stdout, but this binary's Vulkan teardown path does not flush
#    std::cout, so the text is not capturable; exit code 0 is the pass signal
#    (the probe returns 0 only after asserting those exact values).
#    See docs/verification/gfxrecon-windows-native.md "Limitations" and step 9 for the captured readback proof.
# ---------------------------------------------------------------------------
cd "${EXAMPLES}"
./fury.exe vulkan-indirect-smoke
echo "smoke_rc=$?"
# ctest lane (from build-engine, Release): ctest -C Release -R vulkan_indirect_smoke -V

# ---------------------------------------------------------------------------
# 2. Uncaptured tank reference at frame 30 (render thread OFF; see docs/verification/gfxrecon-windows-native.md
#    for why RT=0). Produces reference image + frame-associated gpu-dump stats.
# ---------------------------------------------------------------------------
cd "${EXAMPLES}"
FURY_CLUSTER_LOD=1 ./fury.exe Player.lua "${SCENE}" --rhi=vulkan --render-thread=0 \
  --screenshot "${ART}/reference-frame30-rt0.png" --screenshot-frame ${FRAME} \
  --gpu-dump "${ART}/reference-frame30-rt0-dump" --gpu-dump-frame ${FRAME} \
  > "${ART_M}/reference-frame30-rt0.log" 2>&1
echo "reference_rc=$?"

# RT=1 variant (matches the default engine path). Used only to confirm RT does
# not change the image; this exact command under the capture layer CRASHES (see
# docs/verification/gfxrecon-windows-native.md "Renderer/capture failure").
FURY_CLUSTER_LOD=1 ./fury.exe Player.lua "${SCENE}" --rhi=vulkan \
  --screenshot "${ART}/reference-frame30.png" --screenshot-frame ${FRAME} \
  > "${ART_M}/reference-frame30.log" 2>&1
echo "reference_rt1_rc=$?"

# ---------------------------------------------------------------------------
# 3. CAPTURED tank cluster frame (primary). Manual layer injection (no
#    force-quit) so the captured process also emits the frame-associated
#    --gpu-dump stats and --screenshot. RT=0 avoids the capture+render-thread
#    assertion documented in docs/verification/gfxrecon-windows-native.md.
# ---------------------------------------------------------------------------
cd "${EXAMPLES}"
VK_LAYER_PATH="${LAYER_WIN}" \
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_gfxreconstruct \
GFXRECON_CAPTURE_FILE="${ART}/capture-frame30-rt0.gfxr" \
GFXRECON_CAPTURE_FRAMES=${FRAME} \
FURY_CLUSTER_LOD=1 ./fury.exe Player.lua "${SCENE}" --rhi=vulkan --render-thread=0 \
  --gpu-dump "${ART}/captured-frame30-dump" --gpu-dump-frame ${FRAME} \
  --screenshot "${ART}/captured-frame30.png" --screenshot-frame ${FRAME} \
  > "${ART_M}/captured-frame30-manual.log" 2>&1
echo "capture_rc=$?"
# Produces: capture-frame30-rt0_frame_30_<timestamp>.gfxr (copy kept as
# capture-frame30-rt0.gfxr), captured-frame30.png, captured-frame30-dump/.

# ---------------------------------------------------------------------------
# 3b. CLI-native scheduled capture (preferred workflow; verified working).
#     The CLI injects the layer itself and inherits FURY_CAM/FURY_CLUSTER_LOD.
#     It sets GFXRECON_QUIT_AFTER_CAPTURE_FRAMES=true, so the engine's
#     post-present --gpu-dump/--screenshot do not run in this process; use it
#    for the .gfxr only.
# ---------------------------------------------------------------------------
cd "${EXAMPLES}"
FURY_CLUSTER_LOD=1 "${GFXCLI}" capture windows \
  --exe "${EXAMPLES}\\fury.exe" \
  --args "Player.lua ${SCENE} --rhi=vulkan --render-thread=0" \
  --cwd "${EXAMPLES}" \
  --frames ${FRAME} \
  --out "${ART}\\capture-session-rt0" \
  --timeout 400 > "${ART_M}/capture-cli-rt0.log" 2>&1
echo "capture_cli_rc=$?"
# Produces: capture-session-rt0/capture_frame_30.gfxr

# ---------------------------------------------------------------------------
# 4. Static decode of the capture (no GPU): dispatches, barriers, indirect
#    draws, pipeline/shader reflection. Re-run of the analysis inputs.
# ---------------------------------------------------------------------------
"${GFXCLI}" dump --capture "${ART}/capture-frame30-rt0.gfxr" \
  --out "${ART}/decoded-frame30" --overwrite > "${ART_M}/dump-frame30.log" 2>&1
echo "dump_rc=$?"
# Key decoded files (frame index 0001 == captured application frame 30):
#   frames/0001/passes/001-compute-b2028/draws/000{2,3}-*-vkCmdDispatch/draw.json
#   frames/0001/passes/002-rendering-b2093/draws/0004..0008-*-vkCmdDrawIndexedIndirectCount/draw.json
#   frames/0001/events.jsonl  (vkCmdPipelineBarrier / vkCmdDispatch / vkCmdDrawIndexedIndirectCount)

# ---------------------------------------------------------------------------
# 5. Replay + screenshot (image parity). gfxrecon-replay re-executes the
#    capture on the local GPU and dumps the swapchain image for replay frame 1
#    (== captured application frame 30).
# ---------------------------------------------------------------------------
mkdir -p "${ART_M}/replay-shot"
cd "${ART_M}"
"${GFXREPLAY}" --screenshots 1 --screenshot-format png \
  --screenshot-dir "${ART}/replay-shot" --screenshot-prefix replay \
  --log-level info "${ART}/capture-frame30-rt0.gfxr" > "${ART_M}/replay-shot.log" 2>&1
echo "replay_rc=$?"
# Produces: replay-shot/replay_frame_1.png

# ---------------------------------------------------------------------------
# 6. Replay under the Khronos validation layer (bundled under tools/validation).
# ---------------------------------------------------------------------------
VK_LAYER_PATH="${VAL_WIN}" \
"${GFXREPLAY}" --validate --screenshots 1 --screenshot-format png \
  --screenshot-dir "${ART}/replay-validate" --screenshot-prefix validate \
  --log-level info "${ART}/capture-frame30-rt0.gfxr" > "${ART_M}/replay-validate.log" 2>&1
echo "validate_rc=$?"
# Validation report: replay-validate.log (see docs/verification/gfxrecon-windows-native.md for the cluster-path
# findings). Screenshot: replay-validate/validate_frame_1.png

# ---------------------------------------------------------------------------
# 7. Per-draw ID readback (firstInstance). replay draw --full re-executes up to
#    one indirect draw and reads back the argument buffer
#    (result: geometry.argument_source == "replay_readback"; raw records in
#    replay/capture_dr.json -> parameters.indirectParams[*].firstInstance).
#    Grass batch == execution 4, Wood batch == execution 8.
# ---------------------------------------------------------------------------
"${GFXCLI}" replay draw --capture "${ART}/capture-frame30-rt0.gfxr" \
  --execution 4 --full --out "${ART}/inspect-grass" > "${ART_M}/inspect-grass.log" 2>&1
echo "inspect_grass_rc=$?"
"${GFXCLI}" replay draw --capture "${ART}/capture-frame30-rt0.gfxr" \
  --execution 8 --full --out "${ART}/inspect-wheel" > "${ART_M}/inspect-wheel.log" 2>&1
echo "inspect_wood_rc=$?"

# ---------------------------------------------------------------------------
# 8. Legacy (cluster-disabled) capture + validation: confirms which validation
#    errors are cluster-path specific. FURY_CLUSTER_LOD=0.
# ---------------------------------------------------------------------------
cd "${EXAMPLES}"
VK_LAYER_PATH="${LAYER_WIN}" \
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_gfxreconstruct \
GFXRECON_CAPTURE_FILE="${ART}/capture-frame30-legacy.gfxr" \
GFXRECON_CAPTURE_FRAMES=${FRAME} \
FURY_CLUSTER_LOD=0 ./fury.exe Player.lua "${SCENE}" --rhi=vulkan --render-thread=0 \
  --screenshot "${ART}/legacy-frame30.png" --screenshot-frame ${FRAME} \
  > "${ART_M}/captured-frame30-legacy.log" 2>&1
echo "legacy_capture_rc=$?"
VK_LAYER_PATH="${VAL_WIN}" \
"${GFXREPLAY}" --validate "${ART}/capture-frame30-legacy_frame_30_<timestamp>.gfxr" \
  > "${ART_M}/replay-validate-legacy.log" 2>&1
echo "legacy_validate_rc=$?"

# ---------------------------------------------------------------------------
# 9. Smoke capture + readback: proves the literal gpuCount/id values on the GPU
#    that the uncaptured smoke could not print (step 1).
# ---------------------------------------------------------------------------
cd "${EXAMPLES}"
VK_LAYER_PATH="${LAYER_WIN}" \
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_gfxreconstruct \
GFXRECON_CAPTURE_FILE="${ART}/smoke-capture.gfxr" \
./fury.exe vulkan-indirect-smoke > "${ART_M}/smoke-capture.log" 2>&1
echo "smoke_capture_rc=$?"
# Decode to find execution index of the single vkCmdDrawIndexedIndirectCount:
"${GFXCLI}" dump --capture "${ART}/smoke-capture_<timestamp>.gfxr" \
  --out "${ART}/decoded-smoke" > "${ART_M}/dump-smoke.log" 2>&1
# The draw is execution 1 (dispatch is execution 0). Read back the 64-byte
# RHIIndirectSmokeObservations buffer (16 x uint32):
"${GFXCLI}" replay draw --capture "${ART}/smoke-capture_<timestamp>.gfxr" \
  --execution 1 --full --out "${ART}/inspect-smoke" > "${ART_M}/inspect-smoke.log" 2>&1
echo "smoke_inspect_rc=$?"
# Parse inspect-smoke/replay/buffer_*.bin: the 64-byte buffer holds
# observations; index 3 == 3, index 7 == 3, index 11 == 0 (== id3/id7/id11).

# ---------------------------------------------------------------------------
# 10. Image comparison metrics (tests/tools/imgprobe.py, PIL).
# ---------------------------------------------------------------------------
cd "${REPO}"
python -I tests/tools/imgprobe.py diff \
  test-artifacts/gfxrecon-windows-native/reference-frame30-rt0.png \
  test-artifacts/gfxrecon-windows-native/replay-shot/replay_frame_1.png
# -> "MEAN_ABS_DIFF MAX_ABS_DIFF FRAC_GT_8"

# ---------------------------------------------------------------------------
# Post-fix (2026-11-17): the cluster GBuffer SSBOs were redeclared 'readonly
# buffer' (GBuffer.glsl / GBufferNoTexture.glsl). Targeted recapture/replay
# that cleared VUID-RuntimeSpirv-NonWritable-06341 is scripted separately:
#   tests/gfxrecon/windows-native-post-readonly.sh
# See docs/verification/gfxrecon-windows-native.md "Post-fix (2026-11-17)".
# ---------------------------------------------------------------------------
