## 1. Cluster asset model and offline builder

- [x] 1.1 Define `ClusterLodAsset`, cluster/group CPU records, explicitly aligned GPU records, build options, and shared merged-index ownership; add layout and bounds unit tests.
- [x] 1.2 Implement the full render-relevant mesh content hash and optional mesh cluster descriptor without changing legacy LOD serialization behavior.
- [x] 1.3 Implement deterministic LOD-0 meshlet construction, merged indices, material assignment, conservative bounds, and normal cones using the canonical submesh indices.
- [x] 1.4 Implement shared-edge cluster grouping, locked-boundary simplification, re-clustering, and parent/child group links through coarse roots without crossing material boundaries.
- [x] 1.5 Implement propagated-max group errors, descendant-containing bounds, root/leaf sentinels, the CPU reference cut selector, and per-instance scalar hysteresis math.
- [x] 1.6 Add builder validation and focused fixtures for closed, open, disconnected, attribute-seamed, and multi-material meshes, including sampled crack-free non-overlapping cuts and failure rejection.
- [x] 1.7 Build the existing tank mesh in a headless test and verify 22,163 source triangles, three materials, shared-mesh reuse, valid ranges, monotonic metadata, and decreasing selected triangle counts at coarser cuts.

## 2. Payload persistence, DDC, cook, and pak

- [x] 2.1 Implement versioned `.clod` binary serialization and validation for headers, merged indices, cluster/group records, alignment, truncation, and incompatible schema handling.
- [x] 2.2 Persist cluster descriptors through native JSON/BIN mesh and scene round trips, and verify descriptor or payload loss with direct mesh inspection and saved-document comparison without embedding cluster arrays in scene JSON.
- [x] 2.3 Implement `CLOD_` DDC keys, import/migration/runtime payload resolution, stale/missing/corrupt diagnostics, and mandatory-payload load failure with no runtime generation or legacy missing-payload path.
- [x] 2.4 Add automatic import enumeration and once-per-shared-mesh builder invocation, durable `Resource/ClusterLOD` payloads with content/settings/version-keyed DDC cache hits, explicit generation errors, cook validation of existing payloads, and the `cluster_meshes` manifest section.
- [x] 2.5 Add pak writing and `AssetBackend` loading for content-keyed cluster artifacts, including shared embedded meshes and operation without source FBX or host DDC.
- [x] 2.6 Add import/cook/package integration tests for descriptor and payload round trips, cache invalidation, invalid builds, missing payload load failure, and packaged tank loading.
- [x] 2.7 Add the temporary native JSON/BIN migration CLI, including shared-mesh deduplication, per-mesh failure reporting, migrated reload tests, and removal of cluster-free native compatibility handling.

## 3. Neutral RHI and Vulkan indirect foundation

- [x] 3.1 Add neutral indirect argument/batch APIs, indirect and transfer buffer usage, capability fields, and frame-associated buffer readback while preserving existing RHI callers.
- [x] 3.2 Query and enable Vulkan `multiDrawIndirect`, `drawIndirectFirstInstance`, shader draw parameters, and `drawIndirectCount` when present; expose and log all required limits on native Vulkan and MoltenVK.
- [x] 3.3 Implement Vulkan indirect/count native allocation, compute-to-indirect and readback barriers, bounded buffer copies/readback, and render-thread resource retention.
- [x] 3.4 Implement Vulkan indirect batch preparation/execution with GPU counts, `maxDrawIndirectCount` splitting, validated stride/alignment, and first-instance visible-record addressing.
- [x] 3.5 Extend shader compilation/reflection for vertex-stage SSBO cluster permutations and validate SPIR-V base-instance semantics plus the optional `gl_DrawID` permutation and capability fallback.
- [x] 3.6 Add a focused Vulkan compute-to-indexed-indirect smoke test proving multiple commands and distinct nonzero per-draw IDs with correct readback; require native indirect-count support and record unsupported devices and native gfxrecon acceptance separately.

## 4. Vulkan GPU cluster renderer

- [x] 4.1 Gather eligible opaque static instances into frame-packet snapshots, bypass whole-mesh tier submission only for active cluster units, and retain ineligible instances on their legacy paths.
- [x] 4.2 Implement render-thread cluster resource upload, per-frame instance/camera inputs, leaf-based capacity planning, deterministic whole-instance budget fallback, and in-flight retirement.
- [x] 4.3 Implement instance frustum compaction and dense cut-selection compute shaders using the atomic group CPU predicate, parent-group distance projection, single-evaluation selected records, and scalar hysteresis.
- [x] 4.4 Implement separate selected-cluster frustum/cone culling, visible compaction, bounded per-batch argument/count generation, overflow diagnostics, and required barriers.
- [x] 4.5 Implement full PSO/material/mesh batching, visible-record-driven GBuffer vertex inputs, per-draw debug metadata, and Vulkan indirect submission without per-unit world uniforms.
- [x] 4.6 Integrate indirect draws into the existing opaque GBuffer pass, exclude cluster units from legacy direct/initial PreZ loops, preserve linear depth and material outputs, and leave lighting/postprocessing unchanged.
- [x] 4.7 Add runtime activation/fallback handling for compute-disabled runs, unsupported materials/projections, missing Vulkan features or per-draw IDs, and budget exhaustion; report mandatory payload failures during asset loading instead.

## 5. Vulkan correctness, capture, and performance validation

- [x] 5.1 Compare CPU and Vulkan selected cluster sets across tank near/far cameras, multiple thresholds, shared instances, translated/rotated/non-uniformly scaled transforms, roots, and boundary-adjacent thresholds.
- [x] 5.2 Compare LOD-zero and adaptive cluster GBuffer/final images against legacy tank renders, verify all three materials and no duplicate opaque draws, and confirm distant views reduce drawn triangles.
- [ ] 5.3 Delegate a gfxrecon capture/replay inspection of the tank frame, verifying dispatches, barriers, indirect counts, per-draw IDs, and final-image parity; retain reproducible capture commands and frame-associated semantic output.
- [x] 5.4 Stress GPU counts, `maxDrawIndirectCount` splitting, atomic capacities, off-screen instances, unsafe cone transforms, and deterministic overflow fallback without holes or out-of-bounds accesses.
- [x] 5.5 Run the Vulkan cluster tests with the render thread enabled and under AddressSanitizer, including toggles, mesh rebuilds, packet reuse, resource destruction, and on-demand readback.
- [x] 5.6 Measure and document legacy versus cluster batch count, indirect draw count, selected/drawn triangles, GBuffer GPU/CPU time, and whole-frame time on tank and Sponza without claiming unmeasured hardware coverage.

## 6. Editor, debug views, and reusable sample

- [x] 6.1 Add Mesh editor cluster payload status/counts, cache/fallback diagnostics, and an optional validated `Rebuild Cluster LOD` dialog while import performs generation without user action.
- [x] 6.2 Add persisted default-off cluster enablement and positive screen-error controls with environment/test overrides and immediate safe fallback behavior.
- [x] 6.3 Add mutually exclusive `Cluster LOD` and `Cluster ID` viewport debug views driven by visible-record data, including clean restoration to `None` and legacy LOD-debug coexistence.
- [x] 6.4 Add frame-associated on-demand cluster statistics and explicit unavailable/fallback reasons without normal-frame readback stalls or uninitialized debug sampling.
- [x] 6.5 Add a reusable tank sample/automation scene covering three materials, shared instances, near/far cameras, non-uniform scale, legacy/cluster/debug captures, and semantic statistics.
- [x] 6.6 Verify editor controls through the established in-process ImGui automation method and review tank near/far/debug screenshots on Vulkan, delegating mechanical screenshot comparison where useful.

## 7. Native Metal indirect implementation and parity

- [x] 7.1 Add a focused Metal probe for GPU-encoded indexed ICBs, neutral-argument conversion, pipeline/buffer bindings, `base_instance` shader translation, GPU execution ranges, resource usage, and deployment-target availability.
- [x] 7.2 Implement Metal indirect batch allocation and the backend-owned MSL conversion kernel that encodes neutral arguments, PSO state, mesh buffers, batch bindings, and visible-record base instances.
- [x] 7.3 Implement Metal ICB execution from GPU-written ranges, compute/encoder ordering, resource declarations, count-limit handling, and in-flight lifetime retention without full-capacity empty draws or CPU count readback.
- [x] 7.4 Package and version Metal cluster shader permutations and the native ICB encoder resource, and implement explicit Metal capability fallback and diagnostics.
- [x] 7.5 Run the shared CPU selection, tank image, statistics, overflow, cook/pak, debug-view, render-thread, and AddressSanitizer tests on native Metal; inspect ICB counts/base instances and resource lifetimes.

## 8. Final integration verification

- [x] 8.1 Build normal and AddressSanitizer configurations and run the complete headless Lua/semantic suite plus the new cluster asset, RHI, Vulkan, Metal, cook/pak, and fallback tests.
- [x] 8.2 Run packaged Vulkan/MoltenVK and native Metal tank checks without source assets or host DDC, and run native Vulkan on available non-MoltenVK hardware or record that hardware lane as not run.
- [x] 8.3 Perform a final legacy/cluster visual and performance comparison, verify no regression with the feature disabled, and document backend capabilities, initial shadow/PreZ limitations, test commands, and measured results.
- [x] 8.4 Review the completed change against every capability scenario, resolve or explicitly record any unrun platform checks, and leave the working tree ready for user review without committing unless requested.
