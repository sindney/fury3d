#!/bin/sh
# Task 5.3 (add-gpu-cluster-lod) Vulkan gfxrecon investigation, 2026-11-13.
# Host: macOS, Apple M4 Pro. ICDs: MoltenVK + Mesa KosmicKrisp (non-MoltenVK).
# Run from the repository root unless noted. No engine code was modified.

# --- Environment / hardware inspection (read-only) ---
system_profiler SPDisplaysDataType | head -20
ls /usr/local/share/vulkan/icd.d/            # MoltenVK_icd.json, libkosmickrisp_icd.json
VK_LOADER_DEBUG=error vulkaninfo --summary   # GPU0 MoltenVK (DRIVER_ID_MOLTENVK),
                                             # GPU1 KosmicKrisp (DRIVER_ID_MESA_KOSMICKRISP,
                                             # driverInfo vulkan-sdk-1.4.363 git-269a955c89)

# --- Restrict Vulkan to the non-MoltenVK driver (KosmicKrisp) ---
export KK=/usr/local/share/vulkan/icd.d/libkosmickrisp_icd.json
VK_ICD_FILENAMES=$KK vulkaninfo --summary    # only GPU0 Apple M4 Pro / KosmicKrisp remains
# Feature probe: multiDrawIndirect, drawIndirectFirstInstance, drawIndirectCount,
# shaderDrawParameters all true; maxDrawIndirectCount = 65535.

# --- Focused indirect smoke on KosmicKrisp (no capture): PASSES, native mdi-count ---
cd examples
VK_ICD_FILENAMES=$KK ./fury vulkan-indirect-smoke
# -> vulkan_indirect_smoke: frame=1 mode=mdi-count gpuCount=2 id3=3 id7=3 id11=0 PASS
# Retained uncaptured output: smoke/kosmickrisp-uncaptured.log
# (3/3 runs deterministic; per-draw base-instance IDs verified by GPU readback)

# --- Same smoke under the gfxrecon capture layer: zero readback on KosmicKrisp ---
# No validation errors. The captured stream itself is complete (see .gfxr artifacts).
VK_ICD_FILENAMES=$KK VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_gfxreconstruct \
  GFXRECON_CAPTURE_FILE=/tmp/indirect-dbg.gfxr GFXRECON_CAPTURE_FILE_TIMESTAMP=false \
  ./fury vulkan-indirect-smoke

# MoltenVK has no drawIndirectCount and is unsupported for GPU cluster rendering.

# --- Tank frame on KosmicKrisp: driver segfault in swapchain setup (exit 139) ---
VK_ICD_FILENAMES=$KK FURY_CLUSTER_LOD=1 FURY_CAM="15,8,17,41.43,-15.82,5000" \
  ./fury Player.lua Projects/feature/feature.bin --rhi=vulkan \
  --screenshot /tmp/tank-kk.png --screenshot-frame 30
# lldb: EXC_BAD_ACCESS address 0x0 in libvulkan_kosmickrisp.dylib wsi_configure_image+672,
# called under vkCreateSwapchainKHR (api_dump last call). MoltenVK same command: exit 0.

# --- Offline inspection of captures (no GPU) ---
CLI=/Users/sindney/Documents/git/gfxrecon-gui/dist/gfxrecon-gui/gfxrecon-cli
$CLI info --capture smoke/indirect-kosmickrisp-mdi-count.gfxr
# 1 frame, 264 calls, 1 dispatch execution, 1 draw execution, 2 pipelines
$CLI events --capture smoke/indirect-kosmickrisp-mdi-count.gfxr --draws-only
# vkCmdDispatch(groupCount 1,1,1); vkCmdDrawIndexedIndirectCount(
#   buffer=19 countBuffer=20 countBufferOffset=0 maxDrawCount=3 offset=0 stride=20)
$CLI events --capture smoke/indirect-kosmickrisp-mdi-count.gfxr \
  --filter PipelineBarrier --limit 30
# blocks 120-122: buffers 19/20/21 SHADER_WRITE -> INDIRECT_COMMAND_READ
# (compute-to-indirect and compute-to-vertex/fragment observation barriers)

# gfxrecon-capture-vulkan.py variant (from tests/README.md); NOTE the -w dir did not
# receive the file and the run landed on MoltenVK despite VK_ICD_FILENAMES:
gfxrecon-capture-vulkan.py -w "$OUT" -o indirect.gfxr --no-file-timestamp \
  --file-flush examples/fury vulkan-indirect-smoke
