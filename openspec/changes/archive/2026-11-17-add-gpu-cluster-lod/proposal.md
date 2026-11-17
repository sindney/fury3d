## Why

High-triangle static meshes are still rendered as whole-mesh LOD tiers, so visibility, geometric detail, and draw submission cannot adapt below mesh granularity. A GPU-driven cluster pipeline will let dense assets such as the tank retain nearby detail while reducing distant geometry and CPU draw submission, with Vulkan first for capture-based debugging and native Metal parity after the Vulkan path is established.

## What Changes

- Add an offline, versioned cluster-LOD representation for static meshes: approximately 128-triangle drawable clusters, grouped simplification with locked shared boundaries, a parent/child LOD DAG, conservative screen-error metadata, material assignments, bounds, and normal cones.
- Add deterministic cluster-LOD generation during import using the vendored meshoptimizer library, validation, serialization, durable source payload storage, DDC caching, cook manifest, and pak support. Every eligible static mesh receives a mandatory payload before native save; missing, stale, or corrupt payloads fail asset loading.
- Extend `MeshRender` with an opt-in cluster path that coexists with the current whole-mesh LOD chain and billboard tiers. Skinned, transparent, and otherwise incompatible meshes continue through their existing paths.
- Add GPU instance compaction, error-based DAG-cut selection, cluster frustum and normal-cone culling, visible-cluster compaction, and indexed indirect command generation feeding the existing deferred GBuffer and lighting passes. HZB occlusion culling is not part of this change.
- Extend the RHI with the storage, counter, indirect-draw, and per-draw-ID capabilities required by the cluster path.
- Implement and validate Vulkan first, including MoltenVK capability handling and gfxrecon capture/replay tests. Then implement native Metal using GPU-encoded indirect command buffers and GPU-driven execution ranges where supported.
- Add a temporary migration CLI for existing native scenes and meshes. Migrated assets use the same mandatory payload contract as freshly imported assets; no legacy native cluster-free format remains supported.
- Add editor cluster-LOD enablement, screen-error controls, LOD/cluster debug views, statistics, and fallback diagnostics.
- Add a reusable tank-based sample and automated CPU/GPU selection comparisons, image comparisons, draw/triangle statistics, serialization tests, and backend validation.

## Capabilities

### New Capabilities

- `cluster-lod-assets`: Offline construction, validation, persistence, and loading of drawable cluster LOD DAGs for static meshes.
- `gpu-cluster-rendering`: Opt-in GPU cut selection, cluster culling, indirect submission, deferred GBuffer integration, resource limits, and safe fallback behavior.
- `cluster-lod-debugging`: Editor controls, cluster visualizations, runtime statistics, and tank/capture-based diagnostics.

### Modified Capabilities

- `mesh-lod`: Define precedence and interoperability between cluster LOD, existing whole-mesh LOD chains, and billboard tiers.
- `rhi-abstraction`: Add backend-neutral indirect indexed draws, GPU draw counts, per-draw identification, and required capability queries.
- `rhi-backend-vulkan`: Implement the Vulkan indirect-count path and enforce `multiDrawIndirect`, first-instance/draw-ID, count, stride, and limit requirements, including MoltenVK.
- `rhi-backend-metal`: Implement native Metal indirect command buffers and GPU-driven execution ranges with explicit capability fallback.
- `asset-cooking`: Cook and cache cluster mesh payloads and required shader permutations, version their cache keys, and include them in packaged output.

## Impact

- Mesh data and serialization, cluster generation/validation, `MeshRender`, deferred geometry submission, frame-packet snapshots, and render-thread resource lifetime management.
- RHI command and capability APIs, Vulkan and Metal backends, shader cross-compilation/reflection, and GPU readback used by tests and diagnostics.
- Editor mesh/viewport UI, DDC/cook/manifest/pak flow, Lua/headless rendering tests, sample scenes, and gfxrecon-based Vulkan validation.
- Uses the existing vendored meshoptimizer dependency; no new runtime dependency is planned.
- Existing source assets remain importable. Native scenes and meshes are migrated once to the versioned cluster descriptor/payload contract; eligible static meshes require valid cluster data. Cluster rendering defaults to fallback until compatible backend capabilities are present, independently of payload availability.
