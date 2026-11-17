# add-gpu-cluster-lod task 5.3 - gfxrecon native Windows capture/replay inspection

Date: 2026-11-16. Branch: feature/gpu-cluster-lod. Artifact dir:
`D:\git\fury3d\test-artifacts\gfxrecon-windows-native`

## Environment

- OS: Windows 10 Pro (win32), Git Bash.
- GPU: NVIDIA GeForce RTX 5060 Ti. Native Vulkan (not MoltenVK).
- GFXReconstruct: 1.0.5-dev (dev:a20d3db), Vulkan Header 1.4.363
  (gfxrecon-replay/info/convert). Bundled Khronos validation layer used.
- Engine device features (from the run log): `multiDraw=1 firstInstance=1
  drawParameters=1 count=1`; `maxDrawCount=4294967295 stride=20`.
  All required indirect features present, so the cluster MDI path is active.
- Scene: `examples/Projects/feature/feature.bin`, `--rhi=vulkan`,
  `FURY_CAM=15,8,17,41.43,-15.82,5000`, `FURY_CLUSTER_LOD=1`, frame 30.
- Reproducible commands: `tests/gfxrecon/windows-native.sh`.

Raw captures, logs, decoded output, and comparison images are ignored local
regeneration outputs under `test-artifacts/`. The reusable commands and this
report are versioned.

Capture artifacts (original .gfxr retained):

| file | sha256 |
|---|---|
| capture-frame30-rt0.gfxr | dcd3883f5fb1632dd67465bcaefd4ecee3f2083b6bf27507e7badacec0749922 |
| capture-frame30-legacy_frame_30_20261116T231305.gfxr | 849e21b9c563ae2fd3644a17ad9ea3b9b164151efb65569e39c14fa167f0d9f |
| smoke-capture_20261116T232834.gfxr | bb84a05ba185dc5a138c2a4f90c8ef2a64997a5e3b2a5cbbe4516879175f2b22 |

## Verdict summary

| Acceptance point | Verdict |
|---|---|
| Focused smoke `vulkan-indirect-smoke` (gpuCount=2 id3=3 id7=3 id11=0) | PASS |
| Instance compaction + selection + culling + command-gen dispatches | PASS |
| Compute-to-indirect / count / vertex-read barriers | PASS |
| Indexed indirect-count submissions, maxDrawCount/stride/count offsets | PASS |
| Per-draw IDs (firstInstance/baseInstance visible-record, nonzero IDs) | PASS |
| Final replay image parity vs uncaptured reference (mean<0.75, frac>8<0.02) | PASS |
| Replay/validation logs free of cluster-path errors | FAIL pre-fix -> PASS post-fix (see "Post-fix (2026-11-17)" below) |
| Frame-associated semantic output retained | PASS |

## 1. Focused smoke - PASS

- `./examples/fury.exe vulkan-indirect-smoke` exits 0 (also `ctest -C Release -R
  vulkan_indirect_smoke` -> Passed). The probe returns 0 only after asserting
  gpuCount==2, id3==3, id7==3, id11==0, so exit 0 is the pass signal.
- GPU readback proof (the uncaptured stdout is not capturable; see Limitations):
  captured `smoke-capture_*.gfxr`, decoded, replayed the single
  `vkCmdDrawIndexedIndirectCount` (execution 1) with `--full` and read back the
  64-byte `RHIIndirectSmokeObservations` buffer
  (`inspect-smoke/replay/buffer_15994572941506112246.bin`):

  `observations = [0,0,0,3, 0,0,0,3, 0,0,0,0, 1,6,1,1]`
  -> index 3 = 3 (id3), index 7 = 3 (id7), index 11 = 0 (id11);
     index 12 = 1 (positions), 13 = 6 (scale), 14 = 1 (tint), 15 = 1 (texture).
  Two commands carried instanceCount=1 (indices 3 and 7 each counted 3
  vertices); the firstInstance=11 command had instanceCount=0 -> count == 2.
  Exact expected values confirmed on the GPU.

## 2. Expected dispatches - PASS

Static decode of `capture-frame30-rt0.gfxr` (application frame 30, capture
frame 1). The four conceptual cluster steps map onto two compute dispatches
(`decoded-frame30/frames/0001/passes/001-compute-b2028/`):

- `vkCmdDispatch groupCount=(8,1,1)` - per-instance frustum compaction fused
  with dense DAG-cut selection (one workgroup per candidate instance;
  diagnostics: candidateInstances=8). The select shader
  (`engine/Fury/ClusterShaderSources.h`) tests each instance against the 6
  frustum planes, early-outs off-screen instances, then evaluates the error
  predicate and atomically appends selected (instance,cluster) pairs. SSBO
  bindings match the select module: ClusterInstances(0), Clusters(1),
  Groups(2), SelectedCounts(3), SelectedPairs(4), FrustumPlanes(5),
  ClusterStats(10). (visibleInstances=8 in the diagnostics is this compaction
  result: all 8 candidate instances passed.)
- `vkCmdDispatch groupCount=(5,1,1)` - selected-cluster frustum/normal-cone
  culling fused with indirect argument + count generation (one workgroup per
  batch; diagnostics: batches=5). Adds bindings ClusterBatches(6),
  ClusterVisibleRecords(7), IndirectArguments(8), IndirectCounts(9),
  InstanceMaterials(11), ClusterLimits(12).

Captured-run frame-30 diagnostics (`captured-frame30-manual.log`):
instances=8 candidateInstances=8 fallbackInstances=0 batches=5
visibleInstances=8 selected=1527 visible=1527 commands=1527
drawnTriangles=128880 selectionOverflow=0 visibilityOverflow=0.

## 3. Compute-to-indirect/count/vertex-read barriers - PASS

From `decoded-frame30/frames/0001/events.jsonl` (block ids):

- After selection dispatch (block 1370->1371): memory barrier
  ALL_COMMANDS/MEMORY_WRITE -> ALL_COMMANDS/(MEMORY_READ|MEMORY_WRITE).
- After cull/cmdgen dispatch (block 1374->1375): same full memory barrier.
- Per batch before the indirect draws (blocks 1375..1399), buffer memory
  barriers (src stage COMPUTE|TRANSFER, src access SHADER_WRITE|TRANSFER_WRITE):
  - IndirectArguments (handle 5462) -> DRAW_INDIRECT / INDIRECT_COMMAND_READ
    (compute-to-indirect argument barrier).
  - IndirectCounts (handle 5463) -> DRAW_INDIRECT / INDIRECT_COMMAND_READ
    (compute-to-indirect-count barrier).
  - Three storage buffers (handles 5454, 5461, 5455; the cluster GBuffer shader
    binds ClusterInstances, ClusterVisibleRecords, Clusters) ->
    VERTEX_SHADER|FRAGMENT_SHADER / SHADER_READ|SHADER_WRITE
    (compute-to-vertex/fragment-read barrier for the visible-record mechanism).

Barriers and the indirect draws reference the same buffer handles (5462/5463),
so each barrier targets the buffer its draw reads.

## 4. Indexed indirect-count submissions - PASS

Five `vkCmdDrawIndexedIndirectCount` in the cluster GBuffer pass
(`decoded-frame30/.../002-rendering-b2093/draws/0004..0008`):

| batch | maxDrawCount | countBufferOffset | offset(bytes) | stride | diagnostics capacity/commands |
|---|---|---|---|---|---|
| Grass | 27  | 0  | 0     | 20 | 27 / 27   |
| Wheel | 24  | 4  | 540   | 20 | 24 / 24   |
| Body  | 1476| 8  | 1020  | 20 | 1476 / 1467 |
| Glass | 6   | 12 | 30540 | 20 | 6 / 6     |
| Wood  | 3   | 16 | 30660 | 20 | 3 / 3     |

- maxDrawCount equals each batch's planned capacity; the GPU-written count
  buffer holds the actual (Body drew 1467 of 1476).
- stride = 20 = sizeof(RHIIndirectDrawArgs) (5 x uint32).
- countBufferOffset steps by 4 bytes (one uint32 per batch).
- argument offset(bytes) = cumulative capacity x 20 (0, 27*20=540,
  51*20=1020, 1527*20=30540, 1533*20=30660), matching the diagnostics
  argumentOffset (0,27,51,1527,1533 commands) and visibleOffset.
- maxDrawIndirectCount splitting is not exercised here (total commands 1527 <<
  maxDrawCount 4294967295); the split path is covered by the smoke test's
  separate split case, not this tank frame.

## 5. Per-draw IDs - PASS

- Mechanism confirmed in the decoded cluster GBuffer vertex shader
  (`decoded-frame30/shaders/436/module.glsl`):
  `ClusterInstances.instances[ClusterVisibleRecords.records[gl_BaseInstanceARB].instanceID].world`
  -> per-draw firstInstance indexes the visible-record buffer via
  gl_BaseInstance (ARB_shader_draw_parameters). Fragment outputs carry
  cluster_id/cluster_level from the same record.
- Nonzero IDs confirmed by readback (`replay draw --full`,
  `evidence-firstinstance-smoke.json`):
  - Grass batch (execution 4): firstInstance = 0..26 (27 records; batch base
    visibleOffset 0, so its first draw is 0 by design).
  - Wood batch (execution 8): firstInstance = 1533, 1534, 1535 (batch base
    visibleOffset 1533) - nonzero, matching the diagnostics.
  - Every record has instanceCount=1; firstIndex accumulates correctly.
  Across all five batches firstInstance spans the visible-record range
  0..1535; the Wheel/Body/Glass/Wood batches are all nonzero-based
  (27, 51, 1527, 1533).

## 6. Final replay image parity - PASS

imgprobe.py diff (tolerance mean<0.75, frac>8<0.02):

| comparison | mean | max | frac>8 |
|---|---|---|---|
| replay (manual capture) vs uncaptured RT0 reference | 0.0078 | 41 | 0.000565 |
| replay (CLI-native capture) vs uncaptured RT0 reference | 0.0065 | 53 | 0.000342 |
| validation replay vs uncaptured RT0 reference | 0.0078 | 41 | 0.000565 |
| captured-run screenshot vs uncaptured RT0 reference | 0.0190 | 119 | 0.001124 |
| uncaptured RT1 vs RT0 reference | 0.2249 | 108 | 0.011659 |

All well within tolerance. Replay image: `replay-shot/replay_frame_1.png`.
The replay reproduces the cluster path (same dispatches/draws) and matches the
non-captured frame 30.

## 7. Validation - FAIL (finding, reported distinctly)

`gfxrecon-replay --validate` (bundled Khronos validation layer) on the cluster
capture reports, per distinct VUID:

- 20x VUID-RuntimeSpirv-NonWritable-06341: the cluster GBuffer VERTEX shader
  uses storage buffers ClusterVisibleRecords / Clusters / ClusterInstances
  (Set 0, bindings 1/2/0) "not marked with NonWritable, but
  vertexPipelineStoresAndAtomics was not enabled".
- 2x VUID-RuntimeSpirv-NonWritable-06340: a FRAGMENT "CounterBuffer" (a
  different, non-cluster-LOD shader) - also present in the legacy run.
- 2x VUID-VkSwapchainCreateInfoKHR-flags-03168: swapchain mutable-format
  without VkImageFormatListCreateInfo - general, also present in legacy.

Cluster specificity: the legacy (FURY_CLUSTER_LOD=0) capture validated with
ONLY 06340 + 03168 and zero Cluster*/06341 errors. The 20x 06341 errors are
therefore attributable to the cluster path.

Root cause: Slang emits these SSBOs as writable `buffer` blocks (the shaders
only read them) and the engine does not enable vertexPipelineStoresAndAtomics.
Impact: a SPIR-V decoration / feature-enablement spec violation. It does not
affect rendering correctness on this device (image parity passes, 0 renderer
`[EROR]` lines), but it is not guaranteed portable and fails the strict
"replay completes without validation ... errors attributable to the cluster
path" clause. The engine should either mark the cluster SSBOs `readonly` or
enable vertexPipelineStoresAndAtomics. (Engine code was not modified per the
task constraints.)

RESOLVED post-fix: the three cluster SSBOs were redeclared `readonly buffer`
in the two cluster GBuffer shaders and this finding is now cleared - see the
dated "Post-fix (2026-11-17)" section at the end of this file.

## Renderer/capture failure (distinct report)

With the render thread ENABLED (default `--render-thread=1`), the captured run
reproducibly aborts during startup:

`Assertion failed: (fury::RenderThread::Get().MayUseGL()), function
fury::Pass::Bind, file D:\git\fury3d\engine\Fury\Pass.cpp, line 515:
RHI call from a thread that does not own the context`

preserved in `captured-frame30-rt1.log`. Uncaptured RT=1 runs are fine; the
gfxrecon capture layer slows startup enough to expose this engine thread-
ownership race (a Pass::Bind on a non-render thread while the render thread is
running). It is independent of the cluster path and of the capture tool's
correctness. Targeted workaround used for all capture/replay here:
`--render-thread=0` (single-threaded; MayUseGL then always true). RT=0 vs RT=1
uncaptured images differ only by temporal particle state
(mean 0.2249 < 0.75), and the cluster diagnostics are identical, so RT=0 is a
faithful stand-in for the cluster GPU work. This is a pre-existing engine race
to report to the engine owner; not fixed here.

## Frame-associated semantic output - PASS

The captured run (manual capture, no force-quit) emitted the frame-30
`--gpu-dump` (`captured-frame30-dump/` with backbuffer.png, rendergraph.json,
GBuffer textures) and the frame-30 `Cluster diagnostics` line quoted in
section 2, so the stats are frame-associated from the captured process itself.
The CLI-native capture sets GFXRECON_QUIT_AFTER_CAPTURE_FRAMES=true and exits
right after frame 30, so its process cannot also emit the post-present
gpu-dump; the manual capture (same frame, same scene) is retained for the
frame-associated stats, and the CLI-native capture is retained as proof the
preferred CLI workflow covers the tank frame.

## Limitations

- Uncaptured `vulkan-indirect-smoke` stdout is not capturable: this console-
  subsystem binary does not flush std::cout on the Vulkan teardown path (all
  redirection/pty attempts yield empty output; ctest also captures none). The
  pass is established by exit code 0 plus the captured GPU readback in
  section 1.
- The two duplicate cluster dispatch/draw sets in `events.jsonl` (blocks
  1370-1448 in the capture preamble and 2028-2221 in the submitted frame) are a
  trimmed-capture recording artifact; the submitted frame has exactly 2
  dispatches + 5 indirect draws, and the engine diagnostics report a single
  render (batches=5, commands=1527).
- Validation findings (section 7) are reported as a failure on that clause; all
  other acceptance points pass. Whether the NonWritable decoration issue blocks
  task 5.3 is left to the reviewer.

## Post-fix (2026-11-17): readonly cluster SSBO recapture/replay

Fix under test (applied by the coordinator; engine/tracked files otherwise
unchanged by this task): the three `CLUSTER_BASE_INSTANCE` SSBO declarations in
`examples/Resource/Shader/Lambert/GBuffer.glsl` (lines 70-72) and
`GBufferNoTexture.glsl` (lines 60-62) are now `readonly buffer` (previously
writable `buffer`). Post-fix artifacts are generated in `post-readonly/`; exact
commands are in `tests/gfxrecon/windows-native-post-readonly.sh`. Original
pre-fix measurements above are retained for comparison.

### Procedure
1. Uncaptured RT=0 reference + gpu-dump (`post-readonly/reference-frame30-rt0.log`):
   shaders recompiled from the changed source (`gbuffer_shader_cluster` /
   `gbuffer_notexture_shader_cluster`: "Vulkan shader compiled"), RC=0, 0
   `[EROR]`. Frame-30 stats unchanged: selected=1527 visible=1527 commands=1527
   drawnTriangles=128880, selectionOverflow=0 visibilityOverflow=0.
2. Post-fix capture (manual RT=0 layer injection): `post-readonly/capture-frame30-rt0.gfxr`
   (sha256 96c16bbb3863bf10ec9bb2189b9fb91a512201d5692c5c9cd17499b723a56748).
   Captured run: RC=0, 0 `[EROR]`, identical frame-30 stats.
3. Replay under Khronos `--validate` + replay screenshot
   (`post-readonly/replay-validate.log`, `replay-validate/validate_frame_1.png`).
4. Static decode (`post-readonly/decoded-frame30/`).

### Verdict evidence (post-fix)

- Validation by log inspection (not exit code): `VUID-RuntimeSpirv-NonWritable-06341`
  count = **0**; zero "Cluster" validation messages. The pre-existing,
  non-cluster `06340` (CounterBuffer fragment) = 2 and swapchain `03168` = 2
  remain, unchanged from the legacy baseline and kept distinct.
- Shader fix confirmed in the recompiled SPIR-V: the cluster GBuffer VERTEX
  shader (`post-readonly/decoded-frame30/shaders/436/module.spvasm`) now carries
  3x `OpDecorate %Cluster{VisibleRecords,Clusters,Instances} NonWritable`
  (pre-fix had 0). The compute shaders keep writable `buffer` for
  ClusterVisibleRecords/ClusterInstances/Clusters, which is valid because
  VUID-06341 governs the vertex/tessellation/geometry stages only; compute
  storage writes are always permitted.
- Cluster GPU work unchanged: 2 dispatches (groups 8 then 5) and 5
  `vkCmdDrawIndexedIndirectCount` (maxDrawCount 27,3,24,1476,6 in the new batch
  submission order Grass,Wood,Wheel,Body,Glass; stride 20; count offsets
  0,4,8,12,16; argument byte offsets 0,540,600,1080,30600). The batch order
  differs from the pre-fix run (an unordered-container iteration order); the
  selected set, capacities, and rendering are identical.
- Image parity (tolerance mean<0.75, frac>8<0.02):
  - replay (validated) vs post-fix uncaptured reference: mean=0.0715 max=87
    frac>8=0.004304.
  - captured-run screenshot vs post-fix reference: mean=0.0764 max=130
    frac>8=0.004187.
  - post-fix reference vs pre-fix reference: mean=0.0699 max=91 frac>8=0.004266
    (the readonly change does not alter rendering).

### Updated verdict

The section-7 validation finding is RESOLVED: replay now completes with zero
validation errors attributable to the cluster path (only the known pre-existing
non-cluster 06340 and swapchain 03168 remain). All task-5.3 acceptance points
now PASS: smoke, dispatches, barriers, indirect-count submissions, per-draw
IDs, image parity, validation, and frame-associated semantic output. The
render-thread + capture assertion documented above is a separate pre-existing
engine race, unchanged by this fix and still worked around with
`--render-thread=0`.
