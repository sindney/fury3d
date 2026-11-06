## 1. Baseline and compact G-buffer

- [x] 1.1 Inventory every G-buffer writer/reader and capture clustered-off HDR/Lambert reference images with SSAO, SSR, sky, ocean, and representative PBR materials
- [x] 1.2 Add shared GLSL G-buffer pack/decode helpers and migrate all deferred writers and lighting/screen-space consumers to them without changing output
- [x] 1.3 Apply the `rgba8` diffuse/metallic, octahedral-normal/roughness `rgba8`, and linear `depth24` layout to the deferred pipeline assets while retaining HDR `rgba16f` lighting/composite targets
- [x] 1.4 Update GPU capture, profiler previews, and any tooling that assumes the previous G-buffer formats or channel layout
- [x] 1.5 Run compacted-G-buffer regression captures on OpenGL and Metal, comparing fallback output and all reserved `$gbuffer_*` effect inputs against the baseline

## 2. Settings and capability gating

- [x] 2.1 Add `clustered_lighting=false` to `RenderSettings`, serialize it through the shared archive path, and seed the active pipeline on scene load
- [x] 2.2 Add an Engine settings checkbox that edits the requested setting, marks the scene dirty, and displays requested/effective state plus unsupported-backend diagnostics
- [x] 2.3 Add a renderer capability query for the complete clustered feature set and implement effective-mode selection with deterministic fallback on missing capabilities, shaders, or resources; append any new pipeline-switch ordinals without renumbering existing Lua-visible values
- [x] 2.4 Add a scene round-trip test for enabled/disabled/missing `clustered_lighting` and verify unsupported loading preserves the saved preference

## 3. RHI compute and shader infrastructure

- [x] 3.1 Audit and complete RHI compute dispatch, storage-buffer binding, counter clear/update, and compute-to-graphics barrier support required by the cluster passes on Metal and Vulkan
- [x] 3.2 Add a small compute-to-SSBO round-trip test on OpenGL, Metal, and Vulkan and make clustered capability reporting depend on its required operations rather than the existing broad compute flags
- [x] 3.3 Ensure deferred resource creation, upload, and destruction are render-thread confined and safe across resize, mode toggles, and in-flight frames
- [x] 3.4 Extend the single-file shader/cross-compilation path as needed for clustered compute and graphics permutations, including storage-buffer reflection and deterministic name-based bindings
- [x] 3.5 Include the new shaders and define permutations in shaderc/DDC/cook discovery and verify packaged Metal/Vulkan loading without runtime GLSL compilation
- [x] 3.6 Keep OpenGL clustered capability disabled and add a startup/runtime test proving the existing fallback performs no clustered dispatch or resource binding

## 4. Cluster construction and assignment

- [x] 4.1 Add shared cluster constants and shader helpers for the 16 x 8 x 24 grid, exponential slice boundaries, linear-depth lookup, and cluster-index reconstruction
- [x] 4.2 Extend frame-packet snapshots with the visible local-light parameters and camera/viewport values needed by render-thread cluster assignment
- [x] 4.3 Implement fixed-capacity cluster count/index buffers, per-frame clear, one-thread-per-cluster assignment dispatch, and the required RHI barrier
- [x] 4.4 Implement conservative point-sphere and spot bounding-volume/cluster intersection tests with deterministic 128-light saturation and overflow diagnostics
- [x] 4.5 Add CPU/reference tests for cluster coordinates, near/far and tile boundaries, off-frustum lights, multi-cluster lights, spot edges, and overflow behavior

## 5. Clustered deferred shading

- [x] 5.1 Refactor common point/spot attenuation, BRDF, and shadow sampling into shared shader functions used by both fallback and clustered lighting permutations
- [x] 5.2 Implement the clustered PBR local-light draw, including G-buffer decode, cluster selection, empty-cluster early-out, shadow bindings, and far-depth masking
- [x] 5.3 Implement the equivalent clustered Lambert local-light path without changing directional, ambient, sky, transparent, or postprocess passes
- [x] 5.4 Wire effective clustered mode into `DeferredPipeline` so it replaces only point/spot accumulation and cannot double-evaluate a local light
- [x] 5.5 Schedule local shadow-map production independently of the legacy accumulation draws and preserve `packet.shadowResults` for transparent, ocean, and particle consumers
- [x] 5.6 Add deterministic mixed-light scenes/tests and compare clustered versus fallback images for HDR and LDR, shadowed/unshadowed lights, normal maps, metallic/roughness, and emissive inputs

## 6. Editor cluster debug views

- [x] 6.1 Add `Cluster Grid` and `Cluster Light Count` to the existing editor render debug-view selector and route their output through the `debug_view` texture
- [x] 6.2 Implement the grid shader using shared tile/depth-slice helpers and the count shader using the active cluster count buffer with a documented color ramp
- [x] 6.3 Expose maximum light count and overflow state in the debug UI, and handle unavailable clustering without binding uninitialized resources
- [x] 6.4 Verify debug views under camera motion, resize, near/far changes, backend depth conventions, and return to `None`, without persisting editor debug state into the scene

## 7. Backend validation and performance

- [x] 7.1 Compile all new shader permutations for OpenGL fallback, Metal, and Vulkan; run the aggregate Lua/scripted tests and fix all regressions
- [x] 7.2 Validate Metal clustered/fallback parity at several resolutions, aspect ratios, and camera depth ranges, including spot-cone edges and cluster boundaries
- [x] 7.3 Validate Vulkan clustered/fallback parity with validation layers and verify cooked/pak execution
- [x] 7.4 Stress overlapping local lights beyond the per-cluster cap and verify deterministic saturation, visible diagnostics, bounded memory, and valid fallback switching
- [x] 7.5 Soak mode toggles, scene reloads, viewport resizes, and render-thread execution under ASAN and the supported-host leak checks
- [x] 7.6 Benchmark fallback versus clustered frame time and GPU passes in sparse and dense local-light scenes; record grid setup cost, light-count statistics, and any overflow-limited cases
- [x] 7.7 Perform final editor visual verification of the setting, both debug views, and Metal/Vulkan scene output; leave OpenGL clustered enablement out of scope unless its full parity gate passes
- [x] 7.8 Rename `PrelightPipeline` and the misspelled deferred pipeline assets throughout current code, scripts, and scenes; remove disposable grid-test code while retaining the runtime compute capability probe
