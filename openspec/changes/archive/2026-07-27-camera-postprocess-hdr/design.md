## Context

Fury3D is a deferred (prelight) renderer. The active pipeline is data-driven: `Pipeline::Load` parses a single JSON file (`DefferedLightingLambert.json`) into three arrays — `textures` (G-buffer render targets), `shaders` (combined VS/FS `.glsl` files with `#ifdef` sections + `defines`), and `passes` (`drawMode` opaque/light/quad, input/output texture names, blend/clear modes). The final pass has empty `output` and renders to the default framebuffer or the editor `RenderTarget`, with sRGB encode via `GL_FRAMEBUFFER_SRGB` / a shader `u_gamma_correct` uniform.

Current constraints discovered:
- `Camera` (`engine/Fury/Camera.{h,cpp}`) does **not** override `Load`/`Save` — it persists nothing today.
- Texture float formats (`rgba16f`, `rgba32f`) are **already fully supported** end-to-end (`EnumUtil`, `Pass` FBO attachment).
- There is **no persistent per-scene settings object**; CSM is a runtime-only `PipelineSwitch` toggled in `EditorWindows::RenderSettingsWindow` ("Engine" section).
- The editor `RenderTarget` is hardcoded RGBA8 + DEPTH24 (fine — it receives the LDR post-tonemap result).
- `RenderAssetPickerModal` (`EditorAssetPicker.cpp`) is a reusable typed asset picker; `EditorConfirmDialog` is a reusable yes/no modal; `RenderAddComponentButton` already force-adds components via `SceneNode::ComponentRegistry`.
- glTF import (`GltfImporter::TranslateMaterial`) collapses PBR metallic-roughness into Lambert, dropping metallic/roughness/normal/occlusion.

## Goals / Non-Goals

**Goals:**
- Add a serialized, ordered, per-effect-toggleable postprocess chain in a per-scene `renderSettings` block.
- Make postprocess effects data-driven (JSON + external GLSL), loaded via the existing `Serializable`/pipeline machinery, hand- and AI-editable.
- Ship ACES tonemap, FXAA, and retro CRT sample effects.
- Add HDR rendering to the prelight pipeline (float targets + PBR lighting + mandatory tonemap before final composite).
- Persist HDR/LDR, CSM, and the postprocess chain as a per-scene `renderSettings` block.
- Map PBR inputs on glTF/FBX import when the pipeline is HDR.
- Reuse existing editor UI (asset picker, confirm dialog, add-component) for effect selection in the settings panel and the force-add-camera prompt.

**Non-Goals:**
- A full PBR IBL/probe system, bloom/DOF/motion-blur, or a node-graph postprocess editor.
- Changing the editor `RenderTarget` format (LDR result is fine).
- Runtime hot-reload of GLSL beyond what the pipeline already does.
- Migrating existing LDR scenes to HDR automatically (fresh-scene workflow is intended).

## Decisions

### D1: Postprocess effect = a reusable pipeline "quad pass" bundle defined in JSON + GLSL
Each effect is a self-contained descriptor: `{ name, shader(.glsl path + defines), uniforms[], inputs[], output-format }`. It is loaded with the same `Serializable::LoadArray` / `Shader`/`Pass` machinery already used by the pipeline, so no new parser is introduced. **Alternative considered**: hardcode effects in C++ — rejected because it breaks the "AI-friendly, hand-editable, just like a rendering feature" requirement.

### D2: Render config lives in a per-scene `renderSettings` block, not on the Camera
A serialized `renderSettings` object on the `Scene` owns: the referenced pipeline path, HDR/LDR mode, CSM enablement, and an ordered postprocess chain of `{ effectName, enabled }` entries. `Camera` serialization is left unchanged. **Alternative A — store on Camera** (`Camera::Load/Save` + effect chain, Unity model): rejected; it fights the engine's data-driven design, requires new Camera serialization + name-resolution plumbing, and the user chose scene-level ownership. **Alternative B — store in the pipeline JSON asset**: rejected because the pipeline JSON is a reusable/shared asset; per-project HDR/CSM/post would mutate a shared file. The scene `renderSettings` references the pipeline and configures it at load, keeping the pipeline JSON shareable.

### D3: Pipeline consumes the render-settings chain during composite
On scene load the `renderSettings` seed the active pipeline (HDR mode, CSM `PipelineSwitch`, resolved effect chain). `PrelightPipeline::Execute` renders the lit scene into an HDR target, then iterates the enabled effects as chained fullscreen-quad passes (ping-pong between two temporary float textures via `Texture::GetTemporary`), and the final effect (or the built-in composite) writes LDR to the default FB / `RenderTarget`. The existing "last pass = sRGB" logic is preserved for the final write. Effects resolve by name from the effect registry; unresolved entries are retained but skipped. **Alternative**: bake effects as static JSON passes in the pipeline — rejected; the chain must be dynamic/toggleable per scene at runtime.

### D4: HDR/CSM are part of `renderSettings`; tonemap is mandatory in HDR
HDR/LDR and CSM are fields of the per-scene `renderSettings` block (D2). When HDR is on, the pipeline uses `rgba16f` lighting targets and PBR shaders, and ACES tonemap is always applied at composite regardless of the user chain. CSM migrates from runtime-only `PipelineSwitch` to this persisted block (still driving the switch at load). Edited via a Pipeline/Engine settings panel that writes back to `renderSettings`. **Alternative**: separate project-settings file / imgui.ini — rejected; user wants it in the current scene ("light project settings").

### D5: HDR onboarding = fresh scene → switch to HDR → import
The intended flow: new empty scene → set HDR in Engine settings → import glTF/FBX (which now maps PBR vars). On switching to HDR, if the active scene has no Camera component, show the confirm dialog to force-add one (tonemap needs a camera). This avoids a fragile in-place LDR→HDR material migration.

### D6: HDR-aware glTF material mapping
When the target pipeline is HDR, `GltfImporter::TranslateMaterial` maps metallic/roughness/normal/occlusion/emissive to PBR material uniforms/textures instead of the lossy Lambert path. LDR import keeps the existing Lambert mapping. Selection keyed on the active pipeline's HDR flag at import time.

## Risks / Trade-offs

- **`renderSettings` is a scene-format addition** → older scenes lack the block; loader treats a missing `renderSettings` as LDR + pre-existing CSM + empty chain (backward compatible), and Save always emits it going forward. Camera serialization is untouched.
- **HDR requires PBR shaders/materials not fully present today** → scope PBR to what the lighting pass + tonemap need; ship an HDR pipeline JSON variant rather than mutating the LDR one, so LDR remains untouched.
- **Ping-pong temporaries cost VRAM/bandwidth** → use pooled `Texture::GetTemporary`/`ReleaseTemporary`; skip disabled effects; if chain empty, do a straight tonemap/composite.
- **Editor RenderTarget is RGBA8** → acceptable because tonemap outputs LDR into it; document that pre-tonemap HDR is never shown raw.
- **FXAA/CRT ordering matters** (FXAA expects LDR/sRGB input; CRT is a final look) → effect chain order is user-controlled; document recommended order (tonemap → FXAA → CRT) but do not enforce.
- **CSM moving to persisted settings** → keep the runtime `PipelineSwitch` as the single source at render time; the settings block just seeds it on load and on toggle.

## Migration Plan

1. Add `Scene` `renderSettings` serialization with LDR + pre-existing CSM + empty-chain defaults (backward compatible); no action needed for old scenes. Camera serialization unchanged.
2. On scene load, seed the active pipeline (HDR mode, CSM switch, resolved effect chain) from `renderSettings`.
3. Ship HDR pipeline JSON + postprocess GLSL/effects as new assets; LDR pipeline unchanged.
4. Editor: add HDR/LDR + CSM toggles and the postprocess chain picker to the Engine settings panel; force-add-camera confirm on HDR enable.
5. Rollback: HDR toggle off reverts to LDR pipeline path; postprocess chain is ignored if empty.

## Open Questions

- Exact PBR material uniform/shader set for the HDR lighting pass — reuse Lambert slots + add metallic/roughness, or a new PBR material template? (lean: new PBR shader variant, minimal uniform additions).
- Effect registry format/location — a shared `Resource/PostProcess/*.json` registry that scenes reference by name (decided: shared so effects are reusable across scenes/pipelines); confirm whether the registry is a single manifest file or one file per effect.
- Whether `renderSettings.pipeline` overrides the current Lua/editor `LoadPipelineFromFile` flow or supplements it (lean: scene load drives pipeline selection when `renderSettings` is present).
