# G-buffer inventory

Source revision: `2669e947c5a79839da6bccd77099518f3495ac69`.

## Current contract

- `gbuffer_depth`: `depth24`, positive linear view depth divided by camera far; 1.0 is the sky/far mask.
- `gbuffer_normal`: `rgba16` UNORM. RGB is view normal * 0.5 + 0.5; A is perceptual roughness for PBR and 1.0 for Lambert, except terrain and ocean write their actual roughness in both modes.
- `gbuffer_diffuse`: `rgba8`. RGB is diffuse/ambient-mixed linear color; A is metallic for PBR and 1.0 for Lambert, except terrain writes metallic 0.
- Opaque color attachment order is normal then diffuse. Ocean color attachment order is HDR color then normal. Depth is extracted separately by `Pass`.
- PBR `hdr_light` and `hdr_composite` remain `rgba16f`; Lambert light/composite targets remain `rgba8`.

Descriptors: `examples/Resource/Pipeline/DeferredLightingPBR.json` and `DeferredLightingLambert.json`. The compaction changes only normal to `rgba8`; diffuse and depth already have the target formats.

## Writers

- `examples/Resource/Shader/Lambert/GBuffer.glsl`: all textured/static/skinned/alpha-test/two-sided/wind/billboard/instanced permutations. The textured two-sided path intentionally retains the card normal on both faces.
- `examples/Resource/Shader/Lambert/GBufferNoTexture.glsl`: untextured variants; apply the two-sided backface flip before normal encoding.
- `examples/Resource/Shader/Terrain/DrawTerrain.glsl`: runtime material override; preserve actual roughness and metallic 0 in both lighting modes.
- `examples/Resource/Shader/Ocean/OceanSurface.glsl`: linear depth plus view normal/roughness, including debug branches. It reads a copied scene depth texture to avoid feedback.
- `examples/Resource/Shader/DrawDepthLeagcy.glsl`: `LINEAR_DEPTH` PreZ variants write matching linear depth with color writes disabled.
- `examples/Resource/Shader/Lambert/Forward.glsl` and `Particle.glsl`: matching transparent depth tests, with depth writes disabled by pipeline state. They do not write normal or diffuse.
- Ordinary shadow depth shaders are not deferred G-buffer writers.

## Readers

- Deferred lighting: `examples/Resource/Shader/Lambert/PointLight.glsl`, `SpotLight.glsl`, and `SunLight.glsl` decode depth, view normal, roughness, diffuse, and metallic.
- Combine: `examples/Resource/Shader/Lambert/Lambert.glsl` reads diffuse RGB; `PbrCombine.glsl` reads diffuse RGB and linear depth/sky mask.
- Screen-space effects: `examples/Resource/Shader/PostProcess/SSAO.glsl` reads normal, roughness, and depth in both HDR and LDR; `SSR.glsl` reads normal/depth and uses stored roughness only for HDR. Their JSON descriptors reserve `$scene`, `$gbuffer_depth`, and `$gbuffer_normal`.
- Sky/clouds: `examples/Resource/Shader/Atmosphere/SkyRayMarch.glsl` uses the far-depth mask; `VolumetricClouds.glsl` uses depth to clip the march.
- Ocean: `examples/Resource/Shader/Ocean/OceanSurface.glsl` reads copied linear scene depth and later writes normal/roughness for SSAO/SSR.
- Runtime editor grid: `engine/Fury/Pipeline.cpp` reads linear depth only.
- Editor picking: `engine/Fury/Editor/EditorPicking.cpp` has independent R32UI/depth targets and no G-buffer channel dependency.
- Reserved texture binding: `engine/Fury/DeferredPipeline.cpp` resolves `$gbuffer_*` names to raw texture objects. Decode belongs in each effect shader, not the binding layer.

## Tooling and tests

- `engine/Fury/Editor/EditorWindows.cpp`: Profiler GBuffer tab inline blit forces alpha to 1 and depth to grayscale. Normal preview needs decoded normals after octahedral packing; diffuse preview can expose metallic separately without alpha-blending it away.
- `engine/Fury/GpuDump.cpp`: raw RGBA dumps, format strings, and channel statistics. RGBA8 readback is already supported; raw dumps should remain raw evidence.
- `engine/Fury/Editor/EditorGpuProfilerWindow.cpp`: capture graph is name/format driven with raw/sRGB display only. Add decoded normal display if semantic previews are required.
- `engine/Fury/Editor/UiTestHost.cpp`: current tests check preview IDs and graph rows, not decoded pixels.
- `tests/lua/pipeline_compile_check.lua`: loads both PBR and Lambert descriptors.
- `tests/pak_pipeline.sh`: checks cooked G-buffer reflection/artifacts.
- `tests/lua/render_scenario.lua`, `tests/render_scenarios.json`, and `tests/render_baseline.py`: maintained deterministic capture framework.

## Shared helper and packaging

Add `examples/Resource/Shader/GBufferCommon.glsl` with no `#version`. Consumers one directory below `Resource/Shader` include `../GBufferCommon.glsl`; root shaders include `GBufferCommon.glsl`. Include it exactly once per translation unit. It owns octahedral normal+roughness, diffuse+metallic, and linear-depth helpers.

Runtime recursive includes are resolved by `engine/Fury/Shader.cpp`; `engine/Fury/ShaderCache.cpp` resolves the same includes for Slang/cook and hashes them into DDC keys. `engine/Fury/CookPipeline.cpp` copies shader sources and discovers scene pipelines, the Lambert baseline pipeline, Terrain/Particle/atmosphere shaders, and postprocess descriptors. Generated `.metal`, DDC, and cook-manifest files are outputs, not edit targets.

Direct inline `Shader::Compile` strings do not resolve includes. Profiler previews must move to a cooked source asset or intentionally keep a separate raw view.

## Invariants and traps

- Roughness moves from normal A to B; view normal XYZ becomes octahedral RG. Metallic remains diffuse A.
- Keep normal then diffuse opaque attachment order and HDR then normal ocean order.
- Preserve linear view depth, far=1 masks, PreZ expressions, transparent depth testing, and ocean's copied-depth path.
- Keep G-buffer attachments non-sRGB; diffuse already contains linear RGB.
- Terrain/ocean roughness must survive in LDR for SSAO even though Lambert normally writes roughness 1.
- Reserved `$gbuffer_*` names return raw textures; every consumer must use the shared decode contract.
- Current `GBuffer.glsl` has no normal-map sampler/TBN path, so existing baselines do not establish normal-map coverage.
