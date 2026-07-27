## Why

Fury3D currently renders in LDR only and has no way to attach screen-space effects. Modern workflows (Unity-style) expect an HDR pipeline with tonemapping plus a configurable, data-driven postprocess stack. We want HDR rendering, a per-project render settings model, and postprocess effects configured through JSON + external GLSL — identical in spirit to how the prelight pipeline already defines shaders/textures/passes — so they remain editable by hand and AI-friendly. Rather than attaching this state to the Camera component, we store it in a per-scene `renderSettings` block that configures the active pipeline, keeping the pipeline JSON a reusable shared asset and matching the engine's existing serialization design.

## What Changes

- **Per-scene render settings block**: scenes gain a `renderSettings` object serializing the referenced pipeline, HDR/LDR mode, CSM enablement, and an ordered postprocess chain. This is the single home for project-level render configuration. Backward compatible — scenes without it default to LDR + pre-existing CSM behavior and an empty chain.
- **JSON + GLSL configurable postprocess effects**: define effects as data — a `.glsl` fullscreen-quad shader plus JSON metadata (inputs/outputs, uniforms, format) — loaded through the existing `Serializable`/`Pipeline::Load` machinery. Ship three sample effects: **ACES tonemapping** (HDR→LDR), **FXAA** (NVIDIA FXAA 3.11 whitepaper), and a **retro CRT** effect.
- **Pipeline/Engine settings panel**: HDR/LDR, CSM, and postprocess chain selection (pick/reorder/toggle effects, reusing the asset-picker modal) are edited in the Engine settings section — not the Camera inspector.
- **HDR prelight pipeline**: G-buffer/light targets switch to float formats (`rgba16f`), and a tonemap step runs before the final sRGB composite. HDR requires PBR materials/shaders in the lighting pass.
- **HDR-aware import**: when the active pipeline is HDR, glTF/FBX import maps PBR metallic-roughness inputs (metallic, roughness, normal, occlusion, emissive) instead of dropping them.
- **Force-add camera on HDR switch**: HDR tonemapping needs a camera for view/projection; if the active scene has no Camera component, prompt (reusing the confirm dialog) to add one.

## Capabilities

### New Capabilities
- `project-render-settings`: Per-scene `renderSettings` block (referenced pipeline, HDR/LDR, CSM, ordered postprocess chain) persisted with the scene and edited in the Engine settings panel.
- `postprocess-effects`: Data-driven postprocess effect definitions (JSON + external GLSL) with three shipped samples — ACES tonemapping, FXAA, retro CRT.
- `hdr-pipeline`: Prelight pipeline HDR rendering using float render targets, PBR lighting, and a mandatory tonemap step before final composite.

### Modified Capabilities
- `gltf-importer`: material translation maps PBR metallic-roughness inputs for HDR pipelines instead of collapsing to Lambert-only.
- `scene-round-trip`: scenes now round-trip the `renderSettings` block (pipeline ref, HDR/CSM, postprocess chain).

## Impact

- **Engine**: new render-settings model serialized on `Scene`; `PrelightPipeline`/`Pipeline` (HDR path, postprocess-chain execution, tonemap injection, HDR/CSM switches driven from settings); `Pass`/`Texture` float targets (formats already supported); new postprocess effect type + loader; `GltfImporter` PBR mapping.
- **Editor**: `EditorWindows` Engine settings (HDR/LDR + CSM + postprocess chain picker), reusing `EditorAssetPicker` and `EditorConfirmDialog`.
- **Assets**: new `examples/Resource/Shader/PostProcess/*.glsl` (ACES, FXAA, CRT) and effect JSON descriptors; HDR variant of `DefferedLightingLambert.json` (kept as a reusable shared asset).
- **Serialization**: scene JSON gains a `renderSettings` block; backward compatible with older scenes. Camera component serialization is unchanged (no postprocess state on the Camera).
