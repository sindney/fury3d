#!/usr/bin/env bash
# windows-native-post-readonly.sh - targeted post-fix recapture/replay for the
# validation finding (VUID-RuntimeSpirv-NonWritable-06341).
#
# Fix under test (applied by coordinator, NOT by this task): the three
# CLUSTER_BASE_INSTANCE SSBOs in examples/Resource/Shader/Lambert/GBuffer.glsl
# (lines 70-72) and GBufferNoTexture.glsl (lines 60-62) are now declared
# 'readonly buffer'. Engine/tracked files otherwise unchanged.
#
# Artifacts land in test-artifacts/gfxrecon-windows-native/post-readonly/.
# Same environment as windows-native.sh: Windows 10, NVIDIA RTX 5060 Ti,
# GFXReconstruct 1.0.5-dev.

set -u

REPO="D:\\git\\fury3d"
EXAMPLES="${REPO}\\examples"
POST="${REPO}\\test-artifacts\\gfxrecon-windows-native\\post-readonly"
POST_M="/d/git/fury3d/test-artifacts/gfxrecon-windows-native/post-readonly"
GFXROOT="/d/Software/gfxrecon-gui-windows-x64/gfxrecon-gui"
GFXCLI="${GFXROOT}/gfxrecon-cli.exe"
GFXREPLAY="${GFXROOT}/tools/gfxrecon-replay.exe"
LAYER_WIN='D:\Software\gfxrecon-gui-windows-x64\gfxrecon-gui\tools\layer'
VAL_WIN='D:\Software\gfxrecon-gui-windows-x64\gfxrecon-gui\tools\validation'

export FURY_CAM="15,8,17,41.43,-15.82,5000"
SCENE="Projects/feature/feature.bin"
FRAME=30

# ---------------------------------------------------------------------------
# 1. Uncaptured post-fix reference (RT=0). Confirms the recompiled cluster
#    shaders (source change -> new DDC hash -> "Vulkan shader compiled") still
#    render, and emits the frame-30 gpu-dump stats + reference image.
# ---------------------------------------------------------------------------
cd "${EXAMPLES}"
FURY_CLUSTER_LOD=1 ./fury.exe Player.lua "${SCENE}" --rhi=vulkan --render-thread=0 \
  --screenshot "${POST}/reference-frame30-rt0.png" --screenshot-frame ${FRAME} \
  --gpu-dump "${POST}/reference-frame30-rt0-dump" --gpu-dump-frame ${FRAME} \
  > "${POST_M}/reference-frame30-rt0.log" 2>&1
echo "reference_rc=$?"

# ---------------------------------------------------------------------------
# 2. Post-fix capture (manual RT=0 layer injection, no force-quit) so the
#    captured process also emits frame-30 gpu-dump + screenshot.
# ---------------------------------------------------------------------------
cd "${EXAMPLES}"
VK_LAYER_PATH="${LAYER_WIN}" \
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_gfxreconstruct \
GFXRECON_CAPTURE_FILE="${POST}/capture-frame30-rt0.gfxr" \
GFXRECON_CAPTURE_FRAMES=${FRAME} \
FURY_CLUSTER_LOD=1 ./fury.exe Player.lua "${SCENE}" --rhi=vulkan --render-thread=0 \
  --gpu-dump "${POST}/captured-frame30-dump" --gpu-dump-frame ${FRAME} \
  --screenshot "${POST}/captured-frame30.png" --screenshot-frame ${FRAME} \
  > "${POST_M}/captured-frame30-manual.log" 2>&1
echo "capture_rc=$?"
# Produces capture-frame30-rt0_frame_30_<timestamp>.gfxr (copy kept as
# capture-frame30-rt0.gfxr, sha256 96c16bbb...a56748).
cp "${POST}/capture-frame30-rt0_frame_30_"*.gfxr "${POST}/capture-frame30-rt0.gfxr"

# ---------------------------------------------------------------------------
# 3. Replay under Khronos validation + replay screenshot (image parity).
# ---------------------------------------------------------------------------
mkdir -p "${POST_M}/replay-validate"
cd "${POST_M}"
VK_LAYER_PATH="${VAL_WIN}" \
"${GFXREPLAY}" --validate --screenshots 1 --screenshot-format png \
  --screenshot-dir "${POST}/replay-validate" --screenshot-prefix validate \
  --log-level info "${POST}/capture-frame30-rt0.gfxr" \
  > "${POST_M}/replay-validate.log" 2>&1
echo "validate_rc=$?"
# Verdict evidence (log inspection, NOT exit code):
#   grep -c 06341 replay-validate.log   -> 0
#   grep -c 06340 replay-validate.log   -> 2 (pre-existing, non-cluster)
#   grep -c 03168 replay-validate.log   -> 2 (pre-existing swapchain)
# Screenshot: replay-validate/validate_frame_1.png

# ---------------------------------------------------------------------------
# 4. Static decode: confirm dispatches/indirect draws remain and inspect the
#    recompiled vertex shader SPIR-V for the NonWritable decoration.
# ---------------------------------------------------------------------------
"${GFXCLI}" dump --capture "${POST}/capture-frame30-rt0.gfxr" \
  --out "${POST}/decoded-frame30" > "${POST_M}/dump-frame30.log" 2>&1
echo "dump_rc=$?"
# Dispatches: frames/0001/events.jsonl vkCmdDispatch groupCount (8,1,1)+(5,1,1).
# Indirect draws: passes/002-rendering-*/draws/*vkCmdDrawIndexedIndirectCount.
# Vertex shader NonWritable: decoded-frame30/shaders/436/module.spvasm has 3x
#   "OpDecorate %Cluster{VisibleRecords,Clusters,Instances} NonWritable"
#   (pre-fix had 0).

# ---------------------------------------------------------------------------
# 5. Image parity: replay vs post-fix uncaptured reference.
# ---------------------------------------------------------------------------
cd "${REPO}"
python -I tests/tools/imgprobe.py diff \
  test-artifacts/gfxrecon-windows-native/post-readonly/reference-frame30-rt0.png \
  test-artifacts/gfxrecon-windows-native/post-readonly/replay-validate/validate_frame_1.png
# -> "0.0715 87 0.004304" (mean max frac>8), within tolerance mean<0.75 frac<0.02.
