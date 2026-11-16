## Context

The engine renders static meshes as draw-per-submesh whole-mesh LOD tiers. `MeshRender` selects a tier during game-thread gather, and `DeferredPipeline` submits its submeshes through the RHI. The existing LOD chain is linear and independent per mesh; it cannot select different detail in different parts of a mesh.

The RHI already exposes compute dispatch, storage buffers, atomics, barriers, and vertex-stage SSBO reads. It does not expose indirect buffers, indexed indirect draws, GPU-written draw counts, nonzero first instances, draw IDs, or buffer readback. Vulkan currently enables only a small optional feature set. Metal direct draws hard-code `baseInstance = 0`.

Meshes embed all vertex streams and submesh indices in native scene JSON or LZ4-framed JSON `.bin` files. Meshoptimizer 0.19 is vendored and provides meshlet construction, bounds, and simplification, but no cluster grouping or DAG builder. Cook currently treats meshes as passthrough assets. The tank in `examples/Projects/samples/tank/tank.fbx` and `feature.bin` has three material submeshes and 22,163 triangles, making it a useful shared-asset correctness case; Sponza is the larger checked-in scaling case.

The design must respect render-thread-only RHI access, immutable frame-packet snapshots, Vulkan/MoltenVK feature differences, and native Metal's different indirect-command model. Clustered lighting is unrelated to geometry clusters and remains downstream and unchanged.

## Goals / Non-Goals

**Goals:**

- Build a crack-resistant static-mesh LOD DAG whose internal nodes contain drawable simplified clusters.
- Select one error-based DAG cut per visible mesh instance on the GPU.
- Cull selected clusters by frustum and normal cone, compact them, and submit indexed MDI draws into the existing deferred GBuffer.
- Preserve material and graphics-pipeline state through explicit batching.
- Ship Vulkan first, including MoltenVK, with capture/replay and CPU/GPU selection validation.
- Add native Metal through the same engine-level renderer and backend-neutral RHI contract.
- Generate mandatory cluster payloads automatically during import and provide a one-time native-asset migration CLI.
- Preserve existing rendering as an explicit fallback for ineligible content, disabled cluster rendering, and unsupported devices.
- Provide editor controls, debug views, statistics, and a reproducible tank sample.

**Non-Goals:**

- Mesh/task shaders, software cluster rasterization, or a replacement deferred lighting model.
- HZB or other occlusion culling in this change.
- Runtime cluster generation, geometry streaming, or virtual-texture-style residency management.
- Skinned meshes, transparent geometry, vegetation wind, billboards, or HISM migration to the initial cluster path.
- Cluster-specific shadow, PreZ, or secondary-view selection in the first milestone. Those passes retain their existing geometry path.
- Removing or migrating existing whole-mesh LOD chains.
- A mandatory performance target before baseline measurements exist. Correctness and useful draw/triangle statistics are the initial acceptance gates.

## Decisions

### 1. Build drawable cluster groups offline, not independent meshlet LODs

Add a CPU-only `ClusterLodBuilder` beside `MeshSimplifier`, using the imported and welded `Mesh` as input. Build once per shared mesh, after import/weld and before native save or cook. Use each submesh index buffer once; do not also process the duplicate top-level index array.

The deterministic build pipeline is:

1. Build LOD-0 clusters with `meshopt_buildMeshlets`, targeting 128 triangles and at most 128 source vertices per cluster. Compute bounds and normal cones with meshoptimizer and conservative engine-side expansion.
2. Build a shared-edge cluster adjacency graph and partition neighboring clusters into bounded groups. The vendored meshoptimizer has no grouping API, so grouping is engine code and needs no new dependency.
3. Lock group exterior boundaries, open geometric boundaries, material boundaries, and duplicated-position attribute seams. Simplify group interiors with border-preserving `meshopt_simplify`; never use the sloppy simplifier for shared cluster boundaries.
4. Re-split the simplified group into drawable clusters and link source groups/clusters to their simplified parents. Record group identity so a simplification transition is atomic.
5. Repeat until each connected region has a coarse root group. Disconnected components and material-separated regions may form a forest.

Initial groups do not cross material boundaries. This can leave material seams denser than an unrestricted Nanite builder, but it preserves batching and avoids cracks between independently simplified materials. Every output cluster has exactly one material index.

Simplification reuses source vertices and produces new index ranges. The payload therefore stores one merged LOD index array plus metadata, while the original mesh vertex streams remain the single vertex source. Positions, normals, tangents, UVs, and colors need no per-LOD duplication.

**Alternative considered:** independent per-cluster simplification or a bounds-only tree. Both are rejected because the first cracks at cluster boundaries and the second cannot draw a coarse representation without descendants.

### 2. Store group-level conservative errors and validate cuts on the CPU

Each cluster record contains:

- merged index range, material index, group index, and build level;
- conservative bounding sphere and normal cone;
- group selection error and parent-group selection error in mesh space;
- traversal/validation links and optional source-cluster ranges for diagnostics.

Each group record contains its input/output cluster ranges, parent/child group links, conservative bounds, simplification error, and monotonic selection errors. All output clusters of a group share the group's selection values so a group cannot be half selected. Each cluster has exactly one parent group; shared cluster records across parent groups are forbidden unless the record is duplicated or the predicate explicitly requires all parents to be too coarse.

Convert meshoptimizer's reported simplification error to mesh units using the exact input extent convention of the pinned API. Define each node's propagated error as `max(localSimplifyError, max(child propagated errors))`, and each group error as the maximum propagated error in its subtree. This gives both `childError <= parentError` and `descendantGroupError <= ancestorError`. Root parent error is infinite; leaf selection error is zero. Bounds include geometry, the error radius, and all descendant bounds so ancestor distance remains conservative.

A CPU reference selector evaluates one atomic predicate per group, with leaf clusters acting as individual leaf nodes:

```text
projectedError(node) <= threshold
AND projectedError(node.parentGroup) > threshold
```

Roots use the explicit infinite parent error. For cluster `N`, the self distance uses its producing group's conservative bounds, or `N`'s own bounds when `N` is a leaf. The parent term uses the parent group's own conservative bounds:

```text
dSelf        = max(distance(camera, N.selfBounds.center) - N.selfBounds.radius, epsilon)
dParent      = max(distance(camera, N.parentBounds.center) - N.parentBounds.radius, epsilon)
lambdaSelf   = N.error       * worldScale * projectionScale / dSelf
lambdaParent = N.parentError * worldScale * projectionScale / dParent
```

Every output cluster of a group therefore receives the same predicate result. Parent bounds containing all descendant bounds imply `dParent <= dSelf` along every path; combined with propagated-max errors, projected errors are monotonic from leaves to roots. This gives exactly one complete, non-overlapping cut without partial group transitions. Builder tests sample thresholds and camera transforms to verify complete group transitions, monotonic errors and bounds, valid indices/ranges, preserved locked boundaries, and crack-free legal cuts on closed, open, disconnected, and multi-material fixtures. These tests are the authority for the group/error construction, not just visual inspection.

**Alternative considered:** storing only arbitrary per-cluster simplification errors. That is insufficient because neighboring group transitions must share a consistent error schedule to form one DAG cut.

### 3. Use dense GPU cut evaluation first; keep traversal metadata for later optimization

For each visible instance, the first runtime implementation evaluates all cluster records in that mesh. This is O(total LOD clusters) per instance but avoids hierarchical queue overflow, multi-parent scheduling, and persistent-work complexity while the asset and selection rules are being validated.

The frame graph is:

```text
game-thread eligible-instance snapshot
  -> render-thread resource/budget preparation
  -> GPU instance frustum compaction
  -> GPU dense DAG-cut selection
  -> GPU selected-cluster frustum/cone culling and compaction
  -> GPU indexed indirect argument and count generation
  -> existing deferred GBuffer rasterization
  -> unchanged deferred lighting and postprocessing
```

LOD selection and visibility culling are separate kernels. Selection evaluates the atomic predicate exactly once per leaf or producing group per instance and writes selected bits plus compact selected-pair records for the corresponding clusters. Culling and command generation consume that result without re-evaluating either error term. Visibility does not influence the LOD predicate. No HZB input is consumed.

A single hysteresis-adjusted projection factor is maintained per mesh instance. The desired camera-derived factor updates the effective factor only when it exits a documented band around the previous value. Every cluster in the instance uses that same effective factor and the same user error threshold for both predicate terms, so every evaluation remains a pure DAG cut. Independent per-cluster previous-state hysteresis is forbidden because it can mix incompatible cuts. Projection uses viewport height, camera projection, conservative distance to each cluster's error sphere, and a conservative scale bound derived from the world matrix. It must not use scale-polluted piecewise world TRS getters.

The CPU reference selector remains available in tests and diagnostics. A later hierarchical GPU traversal may replace dense evaluation without changing the payload or cut semantics, and is not required for initial completion.

**Alternative considered:** immediate persistent DAG traversal. It can reduce work on huge assets but adds queue scheduling and failure modes before correctness is established. A CPU-only shipping path was also rejected because GPU selection/culling and MDI are explicit goals.

### 4. Use a versioned content-addressed payload with an inline descriptor

`Mesh::Save` stores only a small `cluster_lod` descriptor containing schema version, full source-content hash, build settings, and enablement. The generated payload uses a versioned binary `.clod` container with:

- header, source hash, settings, meshoptimizer/builder versions, and validation counts;
- merged 32-bit LOD indices;
- CPU/GPU cluster and group records with explicit alignment and endianness rules;
- optional diagnostic names and source links.

The DDC key namespaces entries with `CLOD_` and hashes every geometry/attribute stream relevant to rendering, submesh/material boundaries, build options, meshoptimizer version, and payload schema. The existing position/index-only `MeshContentHash` is not sufficient.

Import generates the payload automatically once per eligible shared mesh after import/weld and before native save. A temporary migration CLI invokes the same builder for existing native scenes and meshes. Editor rebuilds and cook validation use the same validator, but cook does not generate missing payloads during normal operation. Runtime never synthesizes missing data. Durable unpackaged payloads live under `Resource/ClusterLOD/` so migrated native assets work on a fresh checkout; the DDC is a rebuildable cache populated from the same bytes. Packaged loads resolve through `AssetBackend` from a new `cluster_meshes` manifest section. A missing, stale, or corrupt payload for an eligible static mesh logs a diagnostic and fails asset loading. Ineligible meshes and unsupported rendering backends retain their existing rendering paths. Source assets remain reimportable, while migrated native assets require the versioned descriptor and payload.

**Alternative considered:** embedding all cluster arrays in mesh JSON. It simplifies discovery but makes `.json` scenes substantially larger and duplicates binary data in every saved scene. The sidecar requires explicit cook/pak work but keeps source scenes manageable.

### 5. Batch by complete draw state and use visible records as shader inputs

Eligible opaque static units bypass whole-mesh LOD selection during gather and carry base-mesh, material, transform, and cluster-descriptor snapshots in the `FramePacket`. Skinned, transparent, wind, billboard, unsupported material, and existing HISM paths remain legacy.

A batch key includes render pass, graphics pipeline/PSO, mesh vertex buffers and merged cluster index buffer, material/descriptor set, cull state, and all bindings that differ between draws. One indirect submission never crosses incompatible state.

Compute output records contain instance ID, cluster ID, mesh ID, and batch ID. The vertex shader indexes this record to recover the world transform and per-cluster debug data; the existing per-draw world-matrix uniform is not used for cluster draws. Static mesh vertex attributes and GBuffer outputs remain compatible with the existing pass, including linear view-depth semantics.

The main GBuffer path excludes cluster units from legacy PreZ and direct opaque loops to avoid duplicates. Shadow and other secondary passes retain their existing whole-mesh behavior. This is visually conservative but does not accelerate those passes.

### 6. Add a neutral indirect batch API, not Vulkan-shaped engine calls

Add buffer usage flags for indirect and transfer-source/copy operations, buffer readback for diagnostics, and an `RHIIndirectDrawArgs` layout matching indexed indirect arguments:

```text
indexCount, instanceCount = 1, firstIndex, vertexOffset, firstInstance
```

Add an RHI indirect batch object/API that owns or references argument storage, count storage, maximum capacity, graphics pipeline, and batch bindings. The command flow has a backend preparation step followed by indexed indirect execution. Vulkan preparation is a no-op beyond validation because shared compute writes the final argument buffer. Metal preparation can translate neutral arguments into its native command representation inside the backend.

Expose capabilities for compute/storage support, indirect count, multi-draw, nonzero first instance, shader draw ID, maximum indirect count, storage range, and native indirect batch encoding. Engine code checks capabilities and never calls Vulkan or Metal APIs directly.

GPU-visible counters are cleared and advanced through RHI commands, not per-dispatch `UpdateBuffer` reallocation. Static cluster resources are uploaded and retired on the render thread. Buffer readback is only for tests and on-demand diagnostics, never for the normal frame's draw count or LOD decision.

### 7. Vulkan requires real MDI and GPU-written counts

The Vulkan backend queries and enables `multiDrawIndirect`, `drawIndirectFirstInstance`, shader draw parameters, and `drawIndirectCount` when available. It validates argument stride, `maxDrawIndirectCount`, storage limits, and count-buffer bounds. The execution call maps to `vkCmdDrawIndexedIndirectCount` or the extension equivalent.

The preferred per-draw ID is `firstInstance`, which indexes the visible-record buffer through shader base-instance semantics. A `gl_DrawID + batchOffset` permutation is an allowed alternative when its feature and translation path are validated. If neither works on a device, the GPU cluster renderer is unavailable and the mesh uses legacy rendering; silently emitting incorrect cluster IDs is not permitted.

MoltenVK is a supported Vulkan target for the existing renderer, not a reason to assume native performance. GPU cluster rendering requires indirect-count support. Devices without it, including MoltenVK, are unsupported for the cluster path and retain legacy rendering; no single-indirect emulation is retained. Runtime probes and gfxrecon inspection verify actual features and commands on supported devices. Final Vulkan acceptance requires multi-draw indirect with GPU-written counts.

### 8. Native Metal translates neutral arguments to GPU-encoded ICBs

Native Metal uses `MTLIndirectCommandBuffer` indexed commands with `baseInstance` identifying the visible record. A GPU-written `MTLIndirectCommandBufferExecutionRange` supplies the executed command count where the OS/device supports it.

Shared GLSL compute still emits neutral `RHIIndirectDrawArgs`. The Metal RHI owns an internal MSL conversion/encoding kernel that writes ICB commands, pipeline state, index/vertex buffers, and required batch bindings. This keeps platform API and MSL-specific command types inside the Metal backend while reusing the cross-compiled selection and culling shaders.

The implementation must probe ICB execution ordering, resource usage, inherited versus per-command bindings, argument-buffer requirements, and shader base-instance translation. Unsupported ICB or GPU-range behavior disables only the GPU cluster path and reports the reason. Executing full capacity with mostly empty commands is not the shipping fallback.

**Alternative considered:** per-cluster Metal indirect draws or CPU-readback counts. Both undermine the submission and latency goals and are not accepted as native Metal MDI equivalents.

### 9. Budget by worst-case selected leaves and fall back before submission

The maximum useful cut size is bounded by the mesh's leaf-cluster count. During render preparation, compute per-batch capacities from eligible instances and enforce a documented total memory/draw budget in deterministic snapshot order. Instances that would exceed the budget are moved wholesale to legacy rendering before GPU work begins; partially selected meshes are never emitted as an overflow policy.

Kernels still bounds-check every atomic append and set an overflow diagnostic if a program/asset invariant is violated. No kernel writes beyond allocated storage. `maxDrawIndirectCount` splits large batches into bounded submissions without changing visible-record IDs.

Atomic compaction order is not a rendering contract. Tests compare selected sets, not append order. Debug counters include candidate, selected, visible, drawn, overflowed, and fallback counts.

### 10. Reuse the editor debug-view and settings mechanisms

Add a persisted, default-off cluster-LOD enable setting and screen-error threshold. Import performs generation without user action. The Mesh editor displays payload status/counts and offers a diagnostic rebuild action with cluster size, reduction, and error controls.

Add mutually exclusive `Cluster LOD` and `Cluster ID` debug views through the existing viewport debug-view mechanism. LOD view uses a stable level palette; ID view hashes the selected cluster ID. A statistics section reports GPU/CPU counts and the active fallback reason. `None` restores the normal scene without stale shader state.

The tank sample provides near/far camera positions, multiple shared-mesh instances, non-uniform scale coverage, and all three materials. Vulkan gfxrecon capture/replay checks compute dispatches, barriers, indirect counts, nonzero per-draw IDs where used, and final images. Native Metal runs the same semantic and image tests after Vulkan passes.

## Risks / Trade-offs

- [Incorrect group/error construction produces cracks or overlapping LODs] -> Lock shared boundaries, make transitions atomic, enforce monotonic errors, and validate CPU cuts on targeted fixtures before GPU integration.
- [Dense all-cluster evaluation costs too much on large assets] -> Establish correctness and measure first; retain DAG links for a later hierarchical traversal without changing the file format contract.
- [Non-uniform or sheared transforms make LOD/culling non-conservative] -> Derive conservative error scale and transformed bounds from the world matrix; disable cone culling when its normal transform cannot be bounded safely.
- [Material and PSO fragmentation eliminates MDI benefit] -> Batch by full state, report batch counts, and use the tank's three materials as the minimum correctness case and Sponza for scaling measurements.
- [MoltenVK exposes features but emulates work on the CPU] -> Validate functionality through feature probes and gfxrecon, then measure native Vulkan and native Metal separately rather than assuming Vulkan-on-macOS performance.
- [Metal ICB resource and execution ordering differs from Vulkan] -> Keep conversion inside the Metal RHI, use explicit resource usage/barriers, and gate on a focused backend probe before integrating the renderer.
- [GPU queue or command capacity overflow drops geometry] -> Preallocate from leaf-based upper bounds, move whole instances to legacy rendering deterministically, and instrument every append.
- [Render-thread resource reuse races in-flight work] -> Create, upload, retain, read back, and retire cluster resources through existing RHI/render-thread lifetime mechanisms; run ASAN and render-thread-enabled tests.
- [External payload is omitted from packaged output] -> Add manifest, `AssetBackend`, pak round-trip, content-key invalidation tests, and explicit packaged-asset load failure for missing required artifacts.
- [Debug readback stalls or changes frame timing] -> Read back only on demand or in automation, associate results with a frame ID, and never use readback to drive normal rendering.
- [Legacy shadow/PreZ paths reduce end-to-end benefit] -> Document the first-milestone limitation and measure GBuffer separately; do not claim whole-frame Nanite equivalence.

## Migration Plan

1. Add the payload schema, builder, CPU validator, descriptor round trips, automatic import generation, DDC/cook/pak support, and the temporary native-asset migration CLI while runtime rendering remains unchanged.
2. Add neutral RHI capabilities, buffer readback, and Vulkan indirect support with focused backend tests.
3. Add the Vulkan cluster renderer behind a default-off setting; validate tank CPU/GPU selection, captures, and GBuffer images.
4. Add editor controls/debug views and automated sample/performance reporting.
5. Implement native Metal indirect batch encoding and run the same tests.
6. Keep cluster rendering opt-in until asset and backend validation pass. Payloads remain mandatory for eligible static meshes regardless of rendering enablement. Rollback of cluster rendering uses the existing setting; unsupported content and devices retain whole-mesh rendering.

## Open Questions

No product-scope questions block implementation. Runtime probes must establish the exact feature set of the installed Vulkan SDK/MoltenVK and the deployment-target Metal ICB capabilities; these determine capability flags and test coverage, not the proposed architecture.

## References

- NanoMesh, SIGGRAPH 2024: https://advances.realtimerendering.com/s2024/content/Cao-NanoMesh/AdavanceRealtimeRendering_NanoMesh0810.pdf
- Nanite, SIGGRAPH 2021: https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
- Vulkan indexed indirect draws: https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexedIndirect.html
- Vulkan indexed indirect-count draws: https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexedIndirectCount.html
- MoltenVK runtime capabilities: https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md
- MoltenVK indirect-draw implementation discussion: https://github.com/KhronosGroup/MoltenVK/discussions/2487
- Metal Shading Language Specification: https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf
- Metal GPU-driven execution-range example: https://github.com/JoshHrach/WWDC-Summaries/blob/main/WWDC2019/08-graphics-games-metal/601-modern-rendering-with-metal.md
