# Tasks: transparency-and-postfx

## 1. Material alpha mode

- [x] 1.1 Add `AlphaMode` enum (OPAQUE/MASK/BLEND) + `m_AlphaMode`, `m_AlphaCutoff` to `Material`; derive `m_Opaque` as `mode != BLEND`; extend `Material::Save/Load` (legacy `opaque`-only files upgrade: true→OPAQUE, false→BLEND)
- [x] 1.2 Update `GltfImporter` to map `alphaMode`/`alphaCutoff` onto the new fields (MASK keeps opaque=true)
- [x] 1.3 Add `ALPHA_TEST` define branch (`discard` vs `u_alpha_cutoff` bound from material) to G-buffer fragment shaders; pass enables define for MASK materials

## 2. Transparent pass

- [x] 2.1 Add `Pass::m_DepthWrite` (JSON `depth_write`, default true) → `glDepthMask` in `Pass::Bind/UnBind`
- [x] 2.2 Write `ForwardPBR.glsl` (HDR) and Lambert forward variant: material uniforms + `ALPHA_TEST` support + single-light block matching deferred light shader conventions; ambient/emissive base path
- [x] 2.3 Extend `DrawMode::TRANSPARENT` dispatch in `PrelightPipeline::Execute`: base draw (blend ALPHA) + per-light additive draw (blend ADD, `BindLight`), back-to-front order from `RenderQuery`
- [x] 2.4 Declare `pass_transparent` (after combine, target composite texture, `depth_write: false`, blend ALPHA) in `DefferedLightingPBR.json` and `DefferedLightingLambert.json`; verify composite FBO has depth attachment from gbuffer (attach `gbuffer_depth` if missing)
- [x] 2.5 Build sample scenes: import `GlassVaseFlowers` and `AlphaBlendModeTest` glTFs; verify vase glass transmits, leaves cut out, all three alphaMode rows behave per reference

## 3. G-buffer inputs for chain effects

- [x] 3.1 Extend `RunPostProcessChain` input binding: `$`-prefixed reserved names (`$scene`, `$gbuffer_depth`, `$gbuffer_normal`, `$gbuffer_diffuse`, `$hdr_light`) resolve via `GetTextureByName`, bound read-only, never released; unresolvable → skip effect with warning
- [x] 3.2 Verify depth-attachment aliasing: confirm composite FBO does not hold `gbuffer_depth` while chain effects sample it; add a one-time depth copy or stub attachment if it does

## 4. Screen-space effects

- [x] 4.1 `ssao.json` + `SSAO.glsl`: hemisphere kernel + hash-noise rotation, view-space reconstruction from `$gbuffer_depth`/`$gbuffer_normal`; uniforms `u_radius`, `u_strength`, `u_power`, `u_bias`
- [x] 4.2 `dof.json` + `DOF.glsl`: CoC from linear depth vs `u_focus_distance`/`u_focus_range`, poisson gather capped at `u_max_coc`, `u_strength`
- [x] 4.3 `ssr.json` + `SSR.glsl`: view-space ray march (`u_steps`, `u_thickness`, `u_max_distance`), roughness fade from `gbuffer_normal.a`, screen-edge fade, composite over `$scene`
- [x] 4.4 Visual pass: order SSAO/DOF/SSR before ACES in an HDR sample scene; confirm linear intermediates (no double sRGB) and tune defaults

## 5. Per-effect settings dialog

- [x] 5.1 Add `uniform_overrides` map to `RenderChainEntry`; serialize as `uniforms: [{"name","value":[…]}]` in `RenderSettings::Save/Load` (missing field → empty overrides)
- [x] 5.2 Apply overrides after descriptor defaults in `RunPostProcessChain`; unknown names ignored; overrides never mutate the shared descriptor
- [x] 5.3 Add Edit button per chain entry in the Settings window chain editor → `PostProcessSettingsDialog`: list declared uniforms with DragFloat/DragFloat3/ColorEdit3/DragFloat4 widgets, reset-to-defaults button, marks scene dirty

## 6. Window docking persistence fix

- [x] 6.1 Add all `g_Show*` visibility bools to the `[FuryEditor]` ini settings handler read/write in `Editor.cpp`
- [x] 6.2 Verify: dock Settings next to Viewport → quit → relaunch restores dock; closed window stays closed; Reset Layout still closes Settings/Profiler

## 7. Validation

- [x] 7.1 Build `furye` + run editor smoke: load both sample scenes in HDR and LDR, confirm transparents render and chain effects apply to them
- [x] 7.2 Round-trip test: save scene with chain overrides + alpha-mode materials, reload, confirm values preserved
- [x] 7.3 Run `openspec validate transparency-and-postfx --strict`

## 8. Follow-up fixes (post-implementation review)

- [x] 8.1 `KHR_materials_transmission` fallback: importer maps transmission → BLEND with `transparency = transmissionFactor` (GlassVaseFlowers transmission vase no longer renders as opaque "default material")
- [x] 8.2 Glass blending: additive light draws blend ONE/ONE; forward shader premultiplies diffuse by alpha, specular unmodulated
- [x] 8.3 Fix Content Browser "conflicting ID" ImGui warning when a mesh and material share a name (tile ID now type-qualified)
- [x] 8.4 Fix `replace_active_scene` dropping chain uniform overrides on File → Open (`RenderSettings::CopyChainFrom`)
- [x] 8.5 Copy GlassVaseFlowers + AlphaBlendModeTest into `examples/glTF-Sample-Assets/Models/` for stable relative paths
- [x] 8.6 Fix nondeterministic `Material::SetTexture` texture flags (unordered-map iteration could give multi-texture MASK materials NORMAL flags → "shader not found", missing flowers); priority is now diffuse > specular > normal
- [x] 8.7 Diagnose green overlay: not a clearing bug — the import modal's ×10000 scale suggestion puts the editor camera inside the 22m-tall flowers; rendering is correct at that scale/camera
- [x] 8.8 Split MASK alpha-test into a dedicated shader variant (`ShaderTexture::ALPHA_TEST` bit OR'd at draw time + `ALPHA_TEST` define variants in both pipeline JSONs) instead of a runtime branch on every opaque pixel
- [x] 8.9 Fix effect Edit dialog never opening: `OpenPopup` was called inside the row's `PushID(i)` scope while `BeginPopupModal` ran after `PopID` — popup IDs never matched. OpenPopup now runs after the loop at matching ID depth (CRT itself was fine — its defaults were just invisible; bumped CRT.json to a visible retro look)
- [x] 8.10 Add `--auto-confirm` launcher flag: `Editor.RequestConfirmDialog` fires the default (Yes) selection immediately so modals (import unit-scale) never block headless automation
- [x] 8.11 Reorganize examples into project folders: `Projects/{tank,outdoor,james,sponza,glass}` — each holds its scene (.bin/.json) + textures + source FBX; engine-shared assets (Shader/Pipeline/PostProcess) stay under `Resource/`; Editor.lua default scene is `Projects/tank/scene.bin`, startup resolution tries `Projects/` prefix, dialogs default to `Projects/`
- [x] 8.12 Add `--focus` launcher flag: `Launcher.GetFlag("auto_focus")` in Lua; Editor.lua frames the whole scene's combined AABB after open (max-dim fit, 1.1 margin, far-plane bump), same math as frame-selection; play_transparency.lua honors it too
- [x] 8.13 DOF quality rework: per-pixel rotated taps (kills directional ghosting), asymptotic CoC curve (no instant clamp on deep scenes); verified "black after FXAA" was defaults+content, not a pipeline bug (depth binding proven correct via visualization)
- [x] 8.14 SSR test scene: `Projects/outdoor/outdoor.bin` gains a mirror puddle (roughness 0.1) + floating emissive box via `Material.Create`/`Scene.AddMaterial`/`Scene.AddMesh` Lua bindings; chain [SSAO, SSR, ACES] — reflections verified

## 9. PostFX hardening round 2 (HDR-toggle, encode, DOF removal)

- [x] 9.1 Remove DOF entirely (descriptor + shader + chain entries + spec requirement → REMOVED with migration note): too scale-sensitive for the tuning UX available
- [x] 9.2 Fix "pp has no effect when HDR toggled on": the chain only runs when `HasHDRComposite()`, and toggling HDR with the Lambert pipeline loaded left no composite — the HDR checkbox now reloads the matching stock pipeline JSON (only when the current path is one of the two stock pipelines or empty)
- [x] 9.3 Enforce ACES terminal in HDR (`Pipeline::EnsureTonemapInChain`): strip all ACES entries and re-append one at the END — tonemap is mandatory in HDR and mid-chain ACES made the final sRGB encode order-dependent
- [x] 9.4 Add terminal sRGB encode block to SSR/SSAO shaders (they previously washed out when last in chain — LDR white blowout) + SSR sky-hit rejection (was blending black sky, darkening scenes) + tighter roughFade (ends at 0.5)
- [x] 9.5 Reflective-ground test scene: `Projects/outdoor/outdoor_water.json` — ground material set to water-like (roughness 0.05), fence/grass reflections verified; puddle nodes removed from this variant (outdoor.bin keeps them)
- [x] 9.6 PP on/off matrix on tank + outdoor: LDR chains all good ([CRT], [CRT, SSR], [SSAO, CRT]); HDR single-ACES good; HDR multi-chains exposed 9.7

- [x] 9.7 **verify HDR + chain correctness across fury and furye**: with the pp_matrix.lua harness now correctly applying chains (was silently dropping them), sanity-check that toggling HDR on/off and varying the chain produces reasonable output in both `fury` and `furye`. Resolved in round 11: (a) no blowout/darkening across a 22-variant matrix (tank/outdoor/glass × HDR/LDR × 5-8 chains, white% ≤ 0.42 everywhere); (b) fury↔furye lit-pixel parity within ~5%; (c) canonical order + auto-ACES proven by A/B (`[ACES,FXAA]`≡`[FXAA,ACES]`, `[FXAA]`≡`[FXAA,ACES]` injected, LDR `[ACES,CRT]`≡`[CRT]` stripped — all sub-pixel diffs); (d) both pipelines run their chain (LDR CRT/FXAA/SSAO/SSR all visibly applied). The two reported bugs were one root cause — see 11.1.

## 10. Validation

- [x] 10.1 Build `furye` clean, `openspec validate transparency-and-postfx --strict` passes
- [x] 10.2 (after 9.7) re-run pp matrix on tank + outdoor + glass; archive change — user-verified live; passes; ready to archive.

## 11. Canonical chain order + auto tonemap (user directive)

- [x] 11.1 Root-cause + fix the "disable FXAA → progressive overexposure" bug (also: scene.bin + HDR-toggle blowout — same path): `RunPostProcessChain`'s screen-bound final draw inherited GL state — `pass_transparent` leaves `GL_BLEND` + `glBlendFunc(ONE,ONE)` from its additive light loop, so a single-effect chain (HDR auto-ACES) additively accumulated into the editor RT every frame. Multi-effect chains masked it (intermediate `chainPass->Bind(REPLACE)` reset blending). Fix: every chain draw now sets explicit state (blend off, depth test off, depth mask off); verified frame 60 ≡ frame 300.
- [x] 11.2 Hardcode canonical chain order: effect descriptors declare `stage` (`pre_tonemap`/`tonemap`/`post_tonemap`, inferred from `$gbuffer_*` inputs when omitted) + optional `order`; `ApplyRenderSettings` sorts by (stage, order, name), dedupes, ignores saved order. Canonical: SSAO, SSR → ACES → FXAA, CRT. `EnsureTonemapInChain` removed.
- [x] 11.3 Auto tonemap: exactly one tonemap-stage effect runs in HDR (ACES injected when absent, saved overrides harvested, dupes collapse), stripped in LDR; entry `enabled` ignored for tonemap stage.
- [x] 11.4 Chain editor UI: fixed list of registered effects in canonical order with stage group headers, per-row Enabled checkbox + Edit (uniform overrides, entry created on demand); ACES row mirrors HDR state, disabled with tooltip; unresolved saved entries render red with a remove button. Add/Remove/Up/Down UI removed (RenderSettings API kept for Lua/scripting).
- [x] 11.5 Verification: 22-variant matrix (tank/outdoor/glass × HDR/LDR × chains) in furye — no blowout, means within ~1/255 per scene; fury parity spot-checks (lit-pixel means within ~5%); UI screenshots in HDR-on/off states; spec deltas updated (postprocess-effects, project-render-settings, screen-space-effects).

## 12. Hardening round 3 (state hygiene, SSR/SSAO scale, debug views)

- [x] 12.1 GL state hygiene: `Pass::UnBind` now restores engine defaults for everything `Bind` declares (blend off, depthMask true, depthFunc LESS, cull off) so pass state can't leak into unrelated draws; new `GLStateGuard` RAII for in-pass deviations (transparent additive light loop). Chain + debug draws keep explicit per-draw state declaration (Vulkan-PSO-shaped). Regression: scene pixel-identical (only UI chrome in diff).
- [x] 12.2 SSR fixed for meter-scale scenes: ray marcher rewritten as depth sign-crossing detection (old loop broke on open-space flight within 1-2 steps; hits needed luck within 0.2 units in FRONT of a surface), silhouette-gap rejection via `u_thickness`; defaults now meter-scale (`u_max_distance` 1000, `u_thickness` 25); outdoor_water.json's Material_Ground gains the `roughness_factor` 0.05 that 9.5 documented but never wrote (legacy shininess derived ~0.49 → SSR faded to invisible). Verified: fence/grass reflections on the wet ground at a low camera angle.
- [x] 12.3 SSR with HDR off: chain runner compiles LDR variants with the `LDR` define (cache key `path|ldr`); SSR falls back to `u_ldr_roughness` (0.35) since the Lambert gbuffer packs no roughness.
- [x] 12.4 SSAO meter-scale defaults (`u_radius` 30, `u_bias` 1.0 — was 0.5/0.025, sub-cm, near-invisible at outdoor scale; glass vase keeps its saved 0.02/0.001 overrides). Debug view shows textbook AO (contact darkening, white open surfaces) — the earlier "whole-scene dim" was CRT in the ALL chain + tiny-radius noise, not the AO term.
- [x] 12.5 Buffer debug views: `DEBUG_VIEW` shader branch in SSAO/SSR; viewport toolbar gains single-select debug-view combo (None/SSAO/SSR) rendered standalone into a `debug_view` texture (effect need not be chain-enabled); viewport presents it, Profiler GBuffer tab lists it too. Toolbar: Grid moved into the multi-select overlays combo, Snap toggle moved to Settings → Editor.
- [x] 12.6 GBuffer tab legibility: previews blitted through a shader forcing alpha=1 (PBR metallic/roughness packed in gbuffer alphas made ImGui::Image blend them invisible in HDR) and replicating depth (was red-only). Profiler window now hidden by default.
- [x] 12.7 Effect help metadata: descriptor `description` (chain-row tooltip) + per-uniform `tip`/`min`/`max` (Edit-dialog hover tip with range + value clamping); shipped on all five stock effects with world-unit guidance.
- [x] 12.8 PBR specular upgraded from normalized Blinn-Phong to GGX (user-directed after the "circular white dots" review): Trowbridge-Reitz NDF (pi-less — light_color carries 1/π) + Schlick-GGX Smith visibility + Schlick Fresnel, `a = r²`; replaced in SunLight/PointLight/SpotLight/Forward PBR blocks. Glossy floors now show elongated long-tail highlights instead of circular dots (verified before/after on outdoor_water); tank/glass scenes unchanged within noise. Legacy shininess→roughness mapping kept but is no longer an exact round-trip (documented in GBuffer.glsl). SSR thickness acceptance also floored at one ray step (scale-robust).

