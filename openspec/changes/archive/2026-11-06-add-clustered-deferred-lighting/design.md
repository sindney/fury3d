## Context

`DeferredPipeline` is already a deferred renderer: opaque geometry writes G-buffer targets, point/spot/directional lights accumulate through fullscreen passes, and later passes handle sky, transparency, and postprocessing. Its name is referenced by C++ and serialized pipeline assets, so this change extends the existing pipeline rather than introducing a parallel `ClusteredDeferredPipeline`.

The renderer now has backend-neutral RHI, render-thread frame packets, Slang cross-compilation, and editor buffer debug views. Clustered shading must use those systems rather than adding backend-specific calls to `DeferredPipeline`. The RHI advertises compute on Metal and Vulkan, but both command-list dispatch paths are currently stubs and the cross-compiler handles only vertex/fragment stages. Those foundations are part of this change. macOS OpenGL is limited to GL 4.1 and cannot be assumed to support the required compute path.

## Goals / Non-Goals

**Goals:**

- Reduce local-light shading work with optional view-space clustered culling.
- Preserve the existing deferred path as a runtime fallback and image-comparison reference.
- Reduce G-buffer bandwidth without changing decoded data seen by existing consumers.
- Support clustered lighting on Metal and Vulkan, including cooked shaders.
- Expose cluster grid and light-count diagnostics in the editor.
- Update current scenes, pipeline assets, and scripts to use the deferred naming consistently.

**Non-Goals:**

- Cluster directional lights or change forward transparent rendering.
- Make OpenGL clustered lighting a release requirement.
- Add clustered shadows, volumetric froxel lighting, or a new global GPU allocator.
- Change the established linear-depth, HDR tonemapping, or postprocess ordering contracts.

## Decisions

### Use one deferred pipeline identity and one internal lighting strategy

Add a render-settings boolean and a `PipelineSwitch`-style effective mode to `DeferredPipeline`. The pass graph keeps its existing geometry, shadow, combine, sky, transparent, and postprocess stages. When clustering is effective, local point/spot accumulation is replaced by the clustered local-light path; global directional and ambient work remains in its current passes. When it is not effective, the existing pass sequence runs unchanged.

The class, source files, Lua binding, pipeline asset filenames, and current serialized scene references use deferred naming without aliases. A single pipeline implementation keeps A/B validation as a single-setting operation and avoids duplicate pipeline variants.

### Centralize the G-buffer packing contract

Introduce one shared GLSL packing header and one engine-side attachment description used by pipeline assets and tooling. Use this initial layout:

| Texture | Format | Decoded contents |
| --- | --- | --- |
| `gbuffer_diffuse` | `rgba8` | Existing diffuse/ambient-mixed linear RGB; metallic in A for PBR, 1.0 for Lambert |
| `gbuffer_normal` | `rgba8` | Octahedral view-space normal in RG; perceptual roughness in B; A fixed to 1.0 |
| `gbuffer_depth` | `depth24` | Linear view depth divided by camera far, written through the existing depth-output path |

The current `rgba16` normal target is 16-bit-per-channel UNORM, not half-float. Octahedral encoding reduces it from four 16-bit normal/roughness channels to four 8-bit channels, saving 4 bytes per pixel while retaining separate metallic storage in diffuse alpha. HDR lighting and composite targets remain `rgba16f`; the other G-buffer formats were already compact and keep their existing semantics.

All writers and readers include the shared helpers rather than open-coding channel swizzles or normal encoding. This includes SSAO/SSR roughness reads and ocean's normal writes. Reserved `$gbuffer_*` bindings continue to return the same texture objects, and color attachment order remains normal then diffuse. Normalization and octahedral decoding stay at the decode boundary, and no `SRGB8` attachment is introduced: lighting must not receive an implicit second sRGB decode.

### Use a fixed 16 x 8 x 24 grid with exponential slices

Divide the viewport into 16 by 8 tiles and 24 depth slices. For slice boundary `i`, view distance is:

```text
z(i) = near * pow(far / near, i / 24)
```

The cluster index is derived from pixel coordinate, linear view depth, and the same near/far/logarithm parameters in assignment, lighting, and debug shaders. The grid always covers the camera near/far range; no separate shadow or light-culling distance is introduced.

This fixed grid gives 3072 clusters, predictable resource sizes, and straightforward GPU capture inspection. Screen tiles remain stable under resolution changes while their pixel dimensions change. Exponential slices allocate more depth resolution near the camera than uniform slicing.

### Assign lights with one conservative compute pass

At packet gather, snapshot the visible point/spot lights and camera parameters already needed for rendering. On the render thread, upload a tightly defined light-parameter array containing view-space position/direction, range, color/intensity, spot angles, shadow indices, and the lighting mode data required by the existing shaders.

Each frame:

1. Clear the 3072 per-cluster counters through an RHI compute/buffer-clear operation.
2. Dispatch one assignment thread per cluster. Reconstruct the cluster's conservative view-space bounds and test every visible local light.
3. Append intersecting light indices to a fixed `3072 x 128` `uint32` index table and increment the cluster's count up to 128.
4. Execute an RHI barrier before the lighting or debug pass reads counts/indices.

Point lights use sphere-versus-cluster bounds. Spot lights first use their conservative bounding sphere, then a cone test where that improves rejection without excluding the cone's influence. Directional lights do not enter the list. Overflow drops indices beyond the per-cluster cap in deterministic packet-light order, saturates the stored count at 128, and records a per-frame diagnostic counter. This is preferable to out-of-bounds writes or silently wrapping counts; a future global allocator can replace it without changing the shading contract.

A fixed table uses about 1.5 MiB for indices at the chosen dimensions, independent of scene light count. The simple one-thread-per-cluster assignment is intentional for the first implementation; profiling can justify a more parallel assignment algorithm later.

### Evaluate local lights through one clustered lighting draw

Add PBR and Lambert clustered lighting shader permutations that decode the pixel's G-buffer data, reconstruct its cluster coordinate, and iterate only that cluster's point/spot indices. Reuse common light-attenuation, BRDF, and shadow-sampling functions with the fallback shaders so clustered and unclustered paths cannot drift into different lighting models.

Each local light appears at most once in a cluster list. Shadow resources are bound with the same explicit-sampler rules as the existing light passes. Empty clusters skip the loop. Sky/far-depth pixels retain the current early-out behavior, and directional/ambient contributions are not duplicated in the clustered pass.

Local shadow-map production must no longer depend on the legacy per-light accumulation draw. Schedule the same packet shadow plans before clustered lighting and preserve `packet.shadowResults` for the later transparent, ocean, and particle passes. The fallback path retains its current shadow behavior.

The fallback remains compiled and selectable on every backend. There is no CPU readback of cluster lists in the render loop.

### Gate on an explicit RHI feature set

Add a renderer-level capability query covering compute dispatch, shader-storage buffers, atomics/counters where used, compute-to-graphics barriers, and compute shader cross-compilation/reflection. Effective clustering is:

```text
requested by scene && backend capability set complete && required shaders/resources valid
```

Metal and Vulkan are the initial supported backends, but their current compute capability flags are not sufficient: dispatch and compute cross-compilation must be implemented and proven first. OpenGL reports the feature unavailable for this path until its implementation passes the same capability and image-parity tests; the older global `HasEffectiveCompute()` query alone is not sufficient evidence that clustered rendering is usable.

Shader compilation or resource initialization failure disables the effective mode for that packet and logs a diagnostic, leaving fallback rendering available. The user's serialized preference is not rewritten by a capability failure.

### Extend the existing debug-view selector

Add `Cluster Grid` and `Cluster Light Count` modes to the editor's existing rendering debug-view selection and `debug_view` output path:

- `Cluster Grid` draws tile boundaries and colors pixels by depth slice, with an optional slice-edge highlight based on logarithmic depth. It uses the same cluster-index helper as lighting.
- `Cluster Light Count` maps the selected pixel cluster's saturated count through a documented blue-green-yellow-red ramp and displays the maximum/overflow state in the editor.

Cluster debug selection forces cluster assignment for the displayed packet even when normal lighting would not otherwise need a separate debug computation. If effective clustering is unavailable, the UI explains why and does not sample uninitialized textures or buffers. Debug state is editor/session state, not a scene render setting.

## Risks / Trade-offs

- **Cluster and pixel coordinate mismatch causes missing or misplaced lights** -> Share depth-slice and cluster-index functions, test near/far boundaries, and A/B cameras at multiple aspect ratios on both depth conventions.
- **Conservative bounds still clip spot cones or large ranges** -> Unit-test sphere/cone intersection against CPU reference bounds and include edge-grazing lights in image comparisons.
- **More than 128 lights in one cluster loses contributions** -> Saturate deterministically, surface overflow in the editor/log, and include a stress scene; increase capacity or add a global allocator only from measured need.
- **Octahedral normal quantization causes lighting or screen-space artifacts** -> Compare silhouettes, low-roughness highlights, SSAO, and SSR against the baseline; keep the packing helper shared so precision can be increased without changing consumer semantics.
- **A missed G-buffer reader interprets packed data incorrectly** -> Route all readers through shared decode helpers and run SSAO, SSR, sky, ocean, HDR, Lambert, and GPU-capture regressions.
- **Compute-to-graphics hazards produce intermittent stale lists** -> Use explicit RHI barriers and retired resource lifetimes; validate with render-thread and ASAN runs plus repeated frame captures.
- **Cross-compiled compute reflection differs from graphics reflection** -> Compile every new permutation for Metal/Vulkan in `shaderc`, verify cooked loading, and fail capability activation cleanly on missing bindings.
- **Fixed grid wastes work on mostly empty scenes** -> Keep assignment simple and measure total frame time against fallback; the feature remains optional.
- **Editor debug views alter the image being diagnosed** -> Generate them after normal scene lighting from the same packet data and restore `None` without changing scene settings.

## Migration Plan

1. Land the shared G-buffer contract and compaction with clustering disabled; validate all existing consumers and backends.
2. Add the serialized setting, capability query, RHI resources, and shader compilation while keeping the effective feature disabled unless explicitly requested.
3. Enable the Metal clustered path and debug views after fallback A/B parity passes.
4. Enable Vulkan after the same parity, barrier, and cooked-shader checks pass.
5. Leave OpenGL on fallback. Enable it later only through the existing capability gate after a complete implementation is verified.

Users can roll back clustered lighting at runtime by disabling the setting; old scenes require no migration. The G-buffer compaction is an internal representation change and is reverted as a code/asset change if a consumer regression cannot be fixed, not exposed as another permanent rendering mode.

## Open Questions

None for the initial scope. OpenGL support and alternate grid capacities are later, measurement-driven extensions behind the same contracts.
