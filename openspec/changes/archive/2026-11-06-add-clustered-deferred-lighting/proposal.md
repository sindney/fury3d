# add-clustered-deferred-lighting

## Why

The Deferred pipeline already uses deferred shading, but evaluates local lights with separate fullscreen accumulation draws and spends G-buffer bandwidth on redundant or oversized channels. Clustered light assignment can make shading cost scale with visible clusters and their overlapping lights, while a compact G-buffer reduces bandwidth on every deferred frame.

## What Changes

- Add an optional clustered deferred lighting path: build a view-space 3D cluster grid, assign point and spot lights to clusters, and evaluate each pixel's local lights from its cluster in the lighting pass. Directional lights, shadows, ambient/emissive, sky, transparency, and postprocessing retain their existing passes and behavior.
- Retain the existing per-light deferred accumulation path as a supported fallback and A/B reference. Clustered lighting is disabled by default, can be toggled in render settings, and automatically falls back when the active backend lacks the required capabilities.
- Target Metal and Vulkan for the initial clustered implementation. OpenGL continues to use the fallback; OpenGL clustering may be enabled only after its compute, storage-buffer, barrier, and shader-cross-compilation path is validated.
- Compact the deferred G-buffer by removing redundant data and using formats that match each channel's precision requirements. Preserve the established G-buffer texture names and decoded semantics consumed by lighting, SSAO/SSR, sky, ocean, and debug tooling.
- Add editor debug views for the cluster grid and per-cluster light-count heatmap, alongside the existing render debug-view controls. Debug views use the same cluster assignment and camera depth reconstruction as the lighting pass.
- Rename `PrelightPipeline` to `DeferredPipeline` and correct the deferred pipeline asset filenames and serialized references throughout current scenes and scripts.

## Capabilities

### New Capabilities

- `clustered-deferred-lighting`: View-space cluster construction and light assignment, backend-gated clustered shading with fallback, compact G-buffer contracts, and cluster visualization.

### Modified Capabilities

- `project-render-settings`: Persist and edit the clustered-lighting enablement while resolving unsupported requests to the fallback path.
- `hdr-pipeline`: Define how the compact G-buffer and optional clustered local-light evaluation preserve HDR/PBR shading and tonemapping behavior.

## Impact

- **Renderer**: `DeferredPipeline`, frame-packet light/camera data, deferred pipeline JSON descriptors, G-buffer/lighting shaders, and RHI compute/storage-buffer/barrier usage.
- **Backends and shaders**: Metal and Vulkan compute support and cross-compiled shader permutations; OpenGL remains on the existing deferred fallback. Cooked shader discovery and DDC inputs must include the new compute shaders and defines.
- **Settings and editor**: scene `renderSettings`, Engine settings controls, viewport debug-view selection, and GPU capture/debug texture exposure.
- **Naming and compatibility**: current scenes and pipeline asset references use `DeferredPipeline` and `DeferredLighting*.json`; archived OpenSpec documents retain their historical names. The new setting defaults off, and reserved G-buffer names remain stable.
- **Validation**: clustered-versus-fallback image comparisons, G-buffer consumer regressions, backend shader compilation, render-thread/ASAN checks, and light-count/performance captures at multiple resolutions and depth ranges.
- **Out of scope**: replacing forward transparency, clustered directional-light culling, tiled deferred variants, and a mandatory OpenGL clustered path.
