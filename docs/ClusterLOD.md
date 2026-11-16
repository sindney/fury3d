# Geometry cluster LOD

Geometry cluster LOD selects a conservative DAG cut per eligible static mesh
instance, culls the selected clusters, and submits indexed indirect draws into
the existing deferred GBuffer. Lighting, shadows, and postprocessing remain
unchanged. Clustered lighting is a separate feature.

## Runtime controls and assets

- Rendering is disabled by default. Enable it in Render Settings or with
  `FURY_CLUSTER_LOD=1`. Screen error is positive in the editor and serialized as
  `cluster_lod_error`.
- Eligible static meshes receive a validated, content-keyed `.clod` payload at
  import or migration time. Missing, stale, or corrupt mandatory payloads fail
  asset loading. Descriptor-less native scenes are accepted only by the
  migration CLI; ordinary runtime loads stay strict. Runtime does not generate
  payloads or silently use legacy rendering for invalid data.
- The Mesh inspector shows payload counts and can rebuild with validated
  default options. The rebuild drains in-flight rendering and marks the scene
  dirty for the normal save/cook flow.
- `FURY_CLUSTER_LOD_ERROR=0` is an automation-only all-leaf cut.
  `FURY_CLUSTER_LOD_BUDGET` supplies a capacity limit; overflow moves whole
  instances to legacy rendering in snapshot order.

## Backend coverage

- **Native Metal:** fully supported and validated on compatible Apple GPUs.
  GPU-encoded indexed indirect command buffers use GPU-written execution ranges.
  Camera/material globals, mesh streams, storage buffers, and texture argument
  buffers are encoded into every command. Devices without the required ICB or
  execution-range capabilities use legacy rendering.
- **Native Vulkan:** indexed multi-draw indirect with GPU-written counts and
  first-instance visible-record addressing is required. No hardware-native
  Vulkan driver is available on the macOS host. The non-MoltenVK Mesa
  KosmicKrisp driver passes the focused `mdi-count` smoke with distinct
  base-instance IDs; offline gfxrecon inspection of that smoke shows the
  dispatch, compute-to-indirect barriers, and indirect-count call. The required
  tank capture/replay remains unrun because KosmicKrisp crashes in
  `wsi_configure_image` during tank swapchain creation. This smoke evidence does
  not complete the tank capture lane or establish hardware-native performance.
- **macOS Vulkan / MoltenVK:** not supported for GPU cluster rendering because
  `drawIndirectCount` is unavailable. Eligible meshes use the existing legacy
  path and the editor reports the missing capability. No single-indirect
  emulation is used. Other Vulkan rendering remains available.
- **OpenGL and other unsupported devices/materials:** legacy whole-mesh rendering.

Shadow, PreZ, and secondary-view passes retain their existing geometry paths.
Skinned, transparent, wind, billboard, and HISM content is outside the initial
cluster path. Selection is dense per visible instance; HZB occlusion, mesh/task
shaders, and runtime streaming are not included.

## Verification

Build matching binaries before cook/package checks:

```bash
cmake --build build --target fury furye furye-cli furye-ui-tests -j8
```

Run native and render checks from the repository root:

```bash
ctest --test-dir build --output-on-failure
python3 -I tests/cluster_lod_render.py --backend metal --artifacts /tmp/cluster-lod-metal
python3 -I tests/cluster_lod_render.py --backend vulkan --artifacts /tmp/cluster-lod-vulkan
PAK_TEST_KEEP=1 tests/pak_pipeline.sh
python3 -I tests/run.py run --lane headless --artifacts /tmp/fury-headless
PACKAGED_TEST_KEEP=1 OUT_DIR=/tmp/cluster-lod-packaged tests/cluster_lod_packaged.sh
```

The final integration runs passed every supported native test in both
configurations, with expected Vulkan indirect-smoke skips on macOS, and
33 headless tests with two precondition skips in each configuration. All
five cluster render cases on Metal passed in both configurations. Vulkan/MoltenVK
reported an expected unsupported-platform skip after verifying legacy fallback;
native Vulkan MDI cases remain platform work. Editor
cluster automation passed in both render-thread modes. The isolated packaged
tank passed on native Metal with cooked shader hits and no source assets or
host DDC/CLOD fallback; packaged MoltenVK verified the same legacy fallback.

The render driver checks complete CPU/GPU selected sets from leaves through
forest roots, all three tank materials, near/far reduction, non-uniform scale,
off-screen instances, image parity, and deterministic budget fallback. CTest
also exercises a 16,385-command split-range allocation. The pak suite covers
migration-only loading and strict reload, plus one manifest/pak entry for two
distinct meshes with identical cluster content. Final Metal legacy/cluster
captures, GBuffer depth/diffuse, and near/far views were
visually reviewed with no missing geometry, duplicate draws, holes, stale debug
tint, or feature-disabled regression.

The editor's Geometry Clusters menu provides `Cluster LOD` and `Cluster ID`.
`Read Cluster Stats` asynchronously performs an on-demand readback associated
with the last executed frame; it does not block the UI thread.
Automation can use `FURY_CLUSTER_LOD_DEBUG=lod|id`,
`FURY_CLUSTER_LOD_DEBUG_CYCLE=1`, and `--gpu-dump`. Normal frames do not read
GPU counters back to the CPU.

ASAN binaries are emitted below `/tmp/fury-asan-bin/`; use those explicit paths
rather than `examples/fury` when running ASAN comparisons. On macOS, use
`ASAN_OPTIONS=detect_leaks=0:abort_on_error=1`. An empty `VK_LAYER_PATH` can be
used for ASAN runs when MoltenVK portability validation messages would obscure
memory failures; it does not replace native Vulkan validation.

```bash
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  python3 -I tests/run.py run --lane headless \
  --fury /tmp/fury-asan-bin/fury --furye-cli /tmp/fury-asan-bin/furye-cli \
  --artifacts /tmp/fury-headless-asan
```

## Measurements and open lanes

`tests/cluster_lod_perf.py` records legacy/cluster statistics and measured CPU
GBuffer/frame time for the tank and Sponza. GPU time is reported only when a
backend supplies real timing data; CPU time is not presented as GPU time.
Native Vulkan tank gfxrecon capture/replay and hardware-native MDI performance
remain platform work, not passing coverage. Exact KosmicKrisp commands, capture
artifacts, and the driver crash backtrace are retained under
`test-artifacts/gfxrecon-kosmickrisp/`; start with `COMMANDS.sh`. The gfxrecon
layer also produced zeroed smoke readback on both host drivers despite recording
the expected command stream; that is recorded as a capture-tool failure, not a
renderer failure.

On the retained Apple M4 Pro Metal tank capture, cluster rendering reduces draw
submissions from 56 to 36 and emits five material batches with 1,527 commands
and 128,880 drawn triangles at the adaptive near pose. The all-leaf cut emits
1,536 commands and 134,816 triangles; the far pose emits 1,506 commands and
120,856 triangles. See `test-artifacts/cluster-lod-perf/RESULTS.md` for CPU
timings, historical MoltenVK emulation measurements, commands, and variance
caveats. Those emulation measurements are not supported-path evidence. Metal
GPU time and native Vulkan MDI GPU time are unmeasured.

The checked-in Sponza mesh has 103 submeshes, above the initial eight-submesh
cluster batching limit, so every Sponza instance uses legacy fallback. Its
comparison measures identical work and is not a cluster speedup result.
