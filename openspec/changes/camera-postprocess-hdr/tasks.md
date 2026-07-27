## 1. Postprocess effect foundation (data-driven)

- [x] 1.1 Define a `PostProcessEffect` entity (name + shader ref + declared inputs/output format + uniform defaults) loadable via `Serializable::LoadArray`, mirroring the pipeline shader/texture/pass JSON shape
- [x] 1.2 Add a shared effect registry (`Resource/PostProcess/*.json`) and a `FileUtil` loader that registers effects into the entity manager by name for scenes to reference
- [x] 1.3 Implement effect execution as a fullscreen-quad pass sampling declared input(s) and writing declared output, using `Texture::GetTemporary`/`ReleaseTemporary` for ping-pong buffers
- [x] 1.4 Author `Resource/Shader/PostProcess/ACES.glsl` (ACES filmic tonemap, HDR sampler input → LDR output)
- [x] 1.5 Author `Resource/Shader/PostProcess/FXAA.glsl` per NVIDIA FXAA 3.11 whitepaper (luma edge detect + blend, configurable quality uniforms)
- [x] 1.6 Author `Resource/Shader/PostProcess/CRT.glsl` (scanlines/curvature/vignette driven by configurable uniforms)
- [x] 1.7 Author the three effect JSON descriptors referencing the GLSL files with default uniforms

## 2. Per-scene render settings model

- [x] 2.1 Define a `renderSettings` model (referenced pipeline path, HDR/LDR mode, CSM flag, ordered `{ effectName, enabled }` chain) with add/remove/reorder/toggle accessors
- [x] 2.2 Serialize `renderSettings` in `Scene::Save`; parse in `Scene::Load`, treating a missing block as LDR + pre-existing CSM + empty chain (legacy compatibility)
- [x] 2.3 On scene load, seed the active pipeline from `renderSettings`: HDR mode, CSM `PipelineSwitch`, and resolved effect chain
- [x] 2.4 Resolve effect references by name against the registry on load; retain unresolved entries but mark them skipped at render time

## 3. HDR prelight pipeline

- [x] 3.1 Add an HDR pipeline JSON variant (float `rgba16f` lighting/G-buffer targets, PBR lighting shaders) as a reusable asset, leaving the LDR `DefferedLightingLambert.json` untouched
- [x] 3.2 Add a PBR lighting shader variant + PBR material uniform slots (metallic/roughness/normal/occlusion) with sensible defaults for meshes lacking PBR inputs
  - Implemented as normalized Blinn-Phong (user-approved simplification): G-buffer packs metallic into `gbuffer_diffuse.a` and roughness into `gbuffer_normal.a` (`PBR` define); Sun/Point/Spot light shaders gain `PBR` variants outputting full radiance; legacy materials fall back to metallic=0 + roughness derived from the legacy `shininess` slot (exact inverse of the light-side `n = 2/r⁴−2` mapping). PBR *textures* (MR/normal/occlusion maps) are parsed into material slots by the importer but not yet sampled — follow-up.
- [x] 3.3 Extend `PrelightPipeline::Execute` to render lit scene into the HDR target, then run the render-settings enabled effect chain as chained quad passes before the final sRGB composite
- [x] 3.4 Guarantee an ACES tonemap runs before the final composite in HDR mode even when the postprocess chain is empty
- [x] 3.5 Preserve the existing "last pass = sRGB" / `RenderTarget` redirect behavior for the final LDR write

## 4. Editor: Engine settings panel

- [x] 4.1 Add HDR/LDR and CSM toggles to the Engine section of `EditorWindows::RenderSettingsWindow`, reading/writing `renderSettings`, applying to the active pipeline, and marking the scene dirty
- [x] 4.2 Add a postprocess chain editor to the Engine settings panel: per-slot name, enable checkbox, reorder, remove
- [x] 4.3 Reuse `RenderAssetPickerModal` for picking an effect by type (register a `PostProcessEffect` collect branch in `EditorAssetPicker.cpp`); mark the scene dirty on any chain edit

## 5. HDR onboarding & import

- [ ] 5.1 On switching to HDR, if the active scene has no Camera component, show `EditorConfirmDialog` prompting to add one; on accept, add a Camera via `SceneNode::ComponentRegistry`
  - **Deliberate deviation (dropped)**: an earlier revision implemented this, but the no-camera check was always wrong (the camera lives in a child node, not the root) so the dialog fired spuriously; the toggle is now single-click. See the comment block at the HDR checkbox in `EditorWindows::RenderSettingsWindow`. Left unchecked so the spec deviation stays visible.
- [x] 5.2 Make glTF material translation HDR-aware in `GltfImporter::TranslateMaterial`: map PBR metallic/roughness/normal/occlusion when the target pipeline is HDR; keep the lossy Lambert path (with discard warning) for LDR
  - Wired end-to-end: `Importer.LoadScene/LoadGltf/LoadFbx` set `opts.hdr_target` from `Pipeline::Active->IsHDRMode()` (verified by `tests/lua/smoke_hdr_import.lua`).
- [x] 5.3 Verify the fresh-scene HDR flow end-to-end: new empty scene → set HDR in Engine settings → import glTF/FBX populates PBR materials

## 6. Verification

- [x] 6.1 Round-trip test: `renderSettings` (pipeline ref, HDR/CSM, postprocess chain) survives save/reload; legacy scenes load with LDR defaults + empty chain (`tests/lua/smoke_render_settings_roundtrip.lua`)
- [x] 6.2 Visual check: ACES tonemap yields valid LDR in HDR mode; FXAA smooths edges; CRT applies its look; effects chain in user-selected order (screenshot-verified: HDR scene, empty-chain forced-ACES, ACES→CRT, 3-effect 120-frame soak, fury + furye targets)
- [x] 6.3 Confirm LDR scenes and the existing Lambert pipeline are unaffected (no regression) (`scene-ldr.json` screenshot matches legacy look; LDR smoke tests pass)

## Fixed along the way (this change's bug tail)

- **HDR "unlit" bug**: `pass_combine` fed `hdr_light` into `Lambert.glsl`, whose sampler is named `gbuffer_light`; `Shader::BindTexture` silently skips name mismatches, so the combine multiplied `diffuse × diffuse`. Fixed by the PBR combine shader (`PbrCombine.glsl`) sampling `hdr_light` by its actual name.
- **Chain sRGB encode**: `RunPostProcessChain` bound `u_gamma_correct = toRT` for *every* effect — ACES mid-chain sRGB-encoded into a linear temp and FXAA/CRT (which had no encode path) left the final write linear. Now intermediates stay linear and only the terminal effect encodes (`u_gamma_correct = isLast`), with matching encode blocks added to `FXAA.glsl` / `CRT.glsl`; `FXAA`'s `u_quality` is now a float uniform so its JSON default actually binds.
- **Sky in HDR**: PBR light shaders early-out on cleared far-depth pixels so the sun no longer lights the sky (the LDR pipeline masked this implicitly via `lighting × albedo`).
- **Chain ping-pong pool corruption (3+ effects = black/transparent viewport)**: `RunPostProcessChain` released the last read temp at the end of the final iteration *and again* in the post-loop cleanup (double-release) while the other temp leaked. The doubled pool entry let the next frame's two `GetTemporary` calls hand out the *same* texture as both ping and pong, so an intermediate effect drew a texture onto itself (feedback UB → black/alpha-0 output — "CRT attached does nothing", "grid on → scene goes transparent/black"). Rewritten as per-step acquire + release-after-consume; at most two temps live, read/write never alias, nothing leaks. Verified at frame 120 with ACES+FXAA+CRT on both the default FB and the editor RT.
- **Profiler GBuffer tab went blank in HDR**: the tab hardcoded `gbuffer_light`, which the HDR pipeline doesn't declare. It now auto-switches the list by texture existence (`hdr_light`/`hdr_composite` when present, `gbuffer_light` otherwise) and marks missing entries instead of silently showing nothing.
- **Franken-pipeline on LDR→HDR reload (dark / "no diffuse" after File→Open)**: `Pipeline::Load` only *appended* to the pipeline's EntityManager, and entities key by UUID (unique per instance) — so reloading `DefferedLightingPBR.json` into the same Pipeline that had run the Lambert one left BOTH generations of same-named textures/shaders/passes registered. Name lookups (`Pass::Load` shader/texture resolution, `GetTextureByName`, `SortPassByIndex`) returned an arbitrary generation: old `pass_light` without the PBR `gbuffer_diffuse` input, Lambert light shaders feeding `PbrCombine`, duplicate `pass_final` racing the chain skip. Symptom varied per run — flat light pools with no diffuse textures, or a nearly-black frame. `Pipeline::Load` now clears textures/shaders/passes + `m_SortedPasses` first; a pipeline JSON is always a complete definition. Verified via a scripted LDR→HDR switch (LDR scene renders frames → open HDR scene → correct render) and direct editor launch (diffuse textures visible).
- **FXAA distorted the scene**: the first FXAA was a placeholder, not the whitepaper algorithm — blend direction hardcoded horizontal regardless of the luma gradient, so vertical edges smeared sideways. Rewritten as the canonical Lottes FXAA (3×3 luma neighborhood, gradient-derived edge direction, span clamp, luma-range-validated two-stage blend); `u_quality` maps to span 4/6/8. A/B screenshots: edges smooth, zero smear.
- **EntityManager hardening** (follow-up from the franken-pipeline postmortem): `Get<T>(name)` was an O(n) scan whose winner under duplicate names was unordered_map bucket order — nondeterministic — and duplicates entered silently. `EntityManager` now keeps a per-type name→hashcode index: first-registered-wins (deterministic), O(1) lookups, self-healing on remove/rename (verify-then-rescan), and a one-shot `FURYW` when a duplicate name registers so the next collision is loud at the source instead of corrupting a pipeline downstream.
