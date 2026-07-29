# Proposal: transparency-and-postfx

## Why

The engine cannot render transparent materials: the glTF importer already flags `BLEND` materials as non-opaque and `RenderQuery` already sorts transparent draws back-to-front, but no pipeline pass ever executes them, so glass/foliage samples render wrong. Separately, the postprocess chain can only see the previous chain output — G-buffer depth/normals are unreachable — which blocks the standard screen-space trio (SSAO, DOF, SSR). And while effect JSONs carry default uniforms, there is no editor UI to tune them per-scene, making every look tweak a hand-edit of asset files. Finally, the Settings window never remembers its docked position across restarts.

## What Changes

- **Transparent rendering**: distinguish glTF `alphaMode` OPAQUE / MASK / BLEND; MASK becomes alpha-tested opaque (discard vs `alphaCutoff`), BLEND renders in a new forward transparent pass after the lighting/composite stage with alpha blending, depth-test on, depth-write off, sorted back-to-front (existing `RenderQuery` sort). Validated against the `GlassVaseFlowers` and `AlphaBlendModeTest` glTF sample models.
- **G-buffer inputs for chain effects**: postprocess effect descriptors may declare reserved input names (e.g. `gbuffer_depth`, `gbuffer_normal`, scene color) that the chain binds from pipeline textures instead of the previous chain output.
- **SSAO, DOF, SSR effects**: three new data-driven effects (JSON + GLSL, no C++ changes) built on the G-buffer input mechanism, with tunable uniforms (radius/strength, focal range, ray steps/thickness, etc.).
- **Per-effect settings dialog**: chain entries gain per-instance uniform overrides persisted in the scene's `renderSettings` block; the Settings window's chain editor gets an **Edit** button per entry opening a dialog with type-appropriate widgets (slider/color) for the selected effect's uniforms.
- **Fix Settings window docking persistence**: persist editor window visibility flags (via the existing `[FuryEditor]` ini handler) so the Settings window is `Begin()`'d every frame and ImGui records/restores its dock position like other windows.

## Capabilities

### New Capabilities
- `transparent-rendering`: alphaMode semantics (OPAQUE/MASK/BLEND, alphaCutoff), alpha-test discard in G-buffer shaders, forward transparent pass with blending and back-to-front ordering after lighting.
- `screen-space-effects`: SSAO, depth-of-field, and screen-space reflections as data-driven postprocess effects consuming G-buffer depth/normal inputs.

### Modified Capabilities
- `postprocess-effects`: effect descriptors may declare G-buffer/pipeline-texture inputs in addition to the previous chain output; chain entries may carry per-instance uniform overrides.
- `project-render-settings`: `renderSettings` chain entries persist per-instance uniform overrides; edited through a new per-effect settings dialog in the editor.
- `gltf-importer`: `alphaMode = "MASK"` maps to alpha-tested opaque with `alphaCutoff` instead of being conflated with BLEND.
- `editor-shell`: window visibility flags persist across restarts so window dock positions (notably Settings) are recorded and restored by ImGui.

## Impact

- **Rendering**: `PrelightPipeline` (new transparent pass execution + G-buffer binding into chain), pipeline JSONs (`DefferedLightingPBR.json`, `DefferedLightingLambert.json`), G-buffer/light shaders (alpha-test discard), `Material` (alpha mode + cutoff), `RenderQuery` (already sorts; may need camera-relative distance).
- **Assets**: new `Resource/PostProcess/{ssao,dof,ssr}.json` + GLSL shaders; new sample scenes referencing GlassVaseFlowers / AlphaBlendModeTest.
- **Serialization**: `RenderSettings`/`RenderChainEntry` (uniform overrides in scene JSON — additive, backward compatible), `Material::Save/Load` (alpha mode/cutoff).
- **Editor**: `EditorWindows.cpp` chain editor (Edit button + dialog), `Editor.cpp` ini handler (visibility persistence).
- **Backend**: OpenGL 3.3 / GLSL unchanged; no new dependencies.
