# Design: transparency-and-postfx

## Context

The prelight pipeline is deferred (gbuffer → additive light quads → combine → postprocess chain) on OpenGL 3.3 / GLSL. Three gaps motivate this change:

1. **Transparency machinery is orphaned.** The glTF importer sets `Material::m_Opaque=false` for non-OPAQUE alphaModes, and `RenderQuery` splits/sorts `transparentUnits` back-to-front — but neither pipeline JSON declares a `"drawMode": "transparent"` pass, and `Pass` has no depth-write control. Transparent objects are never drawn.
2. **Chain effects can't see the G-buffer.** `RunPostProcessChain()` binds the previous chain output to *every* declared input sampler, so depth/normals — required by SSAO/DOF/SSR — are unreachable.
3. **No per-instance tuning.** Effect JSONs carry default uniforms; scene `renderSettings` chain entries store only `{effect, enabled}`. The editor chain editor has no uniform editing.

Also: the Settings window's dock position is never saved. Root cause — visibility bools (`g_ShowSettings` etc.) are not persisted in the `[FuryEditor]` ini handler, so after restart the window is never `Begin()`'d and ImGui never records/restores its `[Window][Settings]` entry.

Constraints: GL 3.3 core (no compute, no `sampler2DShadow` arrays), single-file GLSL with `#ifdef VERTEX/FRAGMENT`, single-light `Shader::BindLight` (no light arrays), data-driven ethos — effects must be JSON+GLSL with no per-effect C++.

## Goals / Non-Goals

**Goals:**
- Correct glTF alphaMode semantics: OPAQUE / MASK (alpha-test vs `alphaCutoff`) / BLEND (blended forward pass).
- BLEND objects render lit, blended, back-to-front, after the deferred combine, before the postprocess chain (so DOF/CRT etc. apply to them).
- Data-driven SSAO, DOF, SSR effects with tunable uniforms, consuming G-buffer depth/normal.
- Per-scene, per-chain-entry uniform overrides, edited via an Edit-button dialog, persisted in scene JSON.
- Settings (and all editor windows') visibility persists → docking restores across restarts.
- Validated with `GlassVaseFlowers` (BLEND) and `AlphaBlendModeTest` (all three modes) sample scenes.

**Non-Goals:**
- Order-independent transparency (weighted blended, depth peeling) — back-to-front sort suffices for the target samples.
- Refraction / transmission (glTF `KHR_materials_transmission`) — GlassVaseFlowers uses plain BLEND.
- SSR fallback to reflection probes / cubemaps; half-res or temporal upsampling for SSAO.
- LDR (Lambert) pipeline gets the transparent pass too, but SSAO/SSR are HDR-chain-focused; DOF/SSAO work in LDR if the G-buffer textures exist there.
- Light arrays / clustered forward — we reuse single-light binding.

## Decisions

### D0 (amended): KHR_materials_transmission fallback + glass blending
Post-implementation review found the GlassVaseFlowers `GlassTransmission` mesh (transmission extension, no alphaMode) rendering as opaque white "default material". Two adjustments shipped:
- Importer maps `KHR_materials_transmission` (transmissionFactor > 0, no explicit BLEND) to alpha mode `BLEND` with `transparency = transmissionFactor` — cheap see-through approximation; true refraction remains a non-goal.
- The additive per-light draws now blend `ONE`/`ONE` with the forward shader premultiplying diffuse by alpha and leaving **specular unmodulated** — otherwise alpha-0 glass would be invisible even at grazing angles and ordinary glass lost its highlights.

### D1: Alpha mode on Material; MASK stays opaque, BLEND goes transparent
Extend `Material` with `alpha_mode` (enum: OPAQUE, MASK, BLEND) + `alpha_cutoff` (default 0.5), serialized alongside the existing `opaque` flag (kept, derived: `opaque = mode != BLEND`). Importer maps glTF `alphaMode`/`alphaCutoff` directly. MASK renders in the existing opaque G-buffer pass with `discard` in the fragment shader when `baseColor.a < u_alpha_cutoff` — correct depth, correct deferred lighting, zero new passes. The G-buffer shaders gain a `#ifdef ALPHA_TEST` branch; passes that need it get the define via pipeline shader defines.
- *Alternative:* MASK in the transparent pass — rejected: breaks depth prepass semantics, sorts unnecessarily, and deferred lighting would need the forward path.

### D2: BLEND via forward multi-light pass after combine
New `pass_transparent` in both pipeline JSONs, inserted **after `pass_combine`** (before `pass_final`/chain), targeting the same composite texture (`hdr_composite` / `gbuffer_light`):
- `drawMode: transparent`, `blend: ALPHA`, depth test LESS-EQUAL against `gbuffer_depth`, **new `depth_write: false` pass option** (adds `Pass::m_DepthWrite` → `glDepthMask` in `Pass::Bind/UnBind`).
- New forward shader (`ForwardPBR.glsl` / Lambert variant) using the same single-light uniform block as the deferred light shaders (`light_pos`, `light_color`, …). Execution: `DrawMode::TRANSPARENT` dispatch gains a light loop — one ambient/emissive base draw (blend ALPHA), then one additive (blend ADD, alpha-scaled) draw per light via existing `BindLight`. This reuses the single-light machinery instead of introducing light arrays.
- *Alternative:* bind all lights as uniform arrays — cleaner GPU-side but requires new light-buffer plumbing in `Shader`/`SceneManager`; deferred for later.
- *Alternative:* light transparents from the G-buffer — impossible; they're not in it.
- Approximation accepted: additive per-light contributions ignore per-light occlusion by other transparents; fine at sample scale.

### D3: Reserved-input binding for chain effects
Effect JSON `inputs` may use a `$`-prefixed reserved name: `$scene` (previous chain output — current behavior, default), `$gbuffer_depth`, `$gbuffer_normal`, `$gbuffer_diffuse`, `$hdr_light`. In `RunPostProcessChain`, each declared input resolves: `$`-name → `GetTextureByName()` (bind once, never ping-ponged); anything else → previous chain output (unchanged). `gbuffer_depth` is depth24 storing **linear view depth ÷ far** (written via `gl_FragDepth`), so effects get linearized depth from a plain `sampler2D` with no decode math. Camera/projection uniforms come from the existing `shader->BindCamera` + `u_rt_size`.
- *Alternative:* declare gbuffer inputs as separate JSON field — rejected; `$`-prefix keeps descriptor schema flat and backward compatible (old descriptors have no `$` → identical behavior).

### D4: SSAO / DOF / SSR as pure data-driven effects
Three new descriptors + GLSL files under `Resource/PostProcess/` + `Resource/Shader/PostProcess/`:
- **SSAO**: hemisphere kernel (16–32 samples, hash-noise rotation, no noise texture) over view-space positions reconstructed from `$gbuffer_depth` + `$gbuffer_normal`; uniforms: `u_radius`, `u_strength`, `u_power`, `u_bias`, sample count via define. Multi-pass (blur) is out of scope; single-pass with noise is acceptable initially, tunable via uniforms.
- **DOF**: CoC from linear depth vs `u_focus_distance`/`u_focus_range`; gather blur with `u_max_coc` cap, bokeh-ish 12-tap poisson; uniforms: focus distance/range, strength, near/far blend.
- **SSR**: view-space ray march (`u_steps`, `u_thickness`, `u_max_distance`) against linear depth, reflect view dir about decoded normal, fade by roughness (from `gbuffer_normal.a`) and screen-edge; blends reflection over `$scene`.
- Order note: these compose linear HDR intermediates; tonemap stays last via existing `EnsureTonemapInChain`.

### D5: Per-instance uniform overrides + Edit dialog
`RenderChainEntry` gains `std::unordered_map<std::string, UniformBase::Ptr> uniform_overrides`, serialized in scene `renderSettings.chain[i].uniforms` as `[{"name","value":[…]}]` (same shape as effect JSON). Missing key → effect default (forward/backward compatible). Chain runner applies defaults then overrides.
Editor: each row in the Settings-window chain editor gets an **Edit** button opening `PostProcessSettingsDialog` (ImGui modal or child window) listing the effect's declared uniforms with widget by arity — float1 → `DragFloat`, float3 → `ColorEdit3` or `DragFloat3` (heuristic: name contains "color"), float4 → `DragFloat4` — writing into the entry's override map; scene is marked dirty. A "Reset to defaults" clears overrides.

### D6: Persist window visibility in `[FuryEditor]` ini handler
Add all `g_Show*` bools to the existing `ImGuiSettingsHandler` read/write in `Editor.cpp`. On startup, restored visibility means the window is `Begin()`'d every frame → ImGui writes and restores its `[Window][Name]` dock/pos entry like every other window. Bump `kCurrentLayoutVersion`? No — that would nuke users' layouts; visibility restore is orthogonal and works with existing ini. Reset Layout keeps clearing visibility flags (existing behavior).

### D7: Engine-owned chain order + automatic tonemap (user directive)
Post-implementation review of the reorder UX surfaced two ordering bugs (see below), and the user directed: *users should not sort postprocess at all — effects are hardcoded in order; they only switch on/off; tonemapping follows the HDR switch automatically.* Shipped:

- Effect descriptors declare `"stage"` (`pre_tonemap` / `tonemap` / `post_tonemap`; inferred from `$gbuffer_*` inputs when omitted) and optional `"order"`. Canonical order: **SSAO, SSR (pre, linear HDR) → ACES (tonemap pivot) → FXAA, CRT (post, LDR)** — screen-space lighting effects compose the HDR-linear composite; AA and display simulation run on the final LDR image.
- `Pipeline::ApplyRenderSettings` ignores saved entry order, sorts by (stage, order, name), dedupes, and auto-manages tonemap: exactly one tonemap-stage effect runs in HDR (injects ACES when absent, harvesting overrides from a saved entry; duplicates collapse), none in LDR (stripped). `EnsureTonemapInChain` is gone.
- Editor chain UI is now a fixed list: per-effect **Enabled** checkbox + **Edit** (uniforms) only. The ACES row mirrors the HDR checkbox, is disabled, and explains itself via tooltip. Unresolved saved entries render red with a remove affordance.

**Bug that forced the issue — chain draws inherited pipeline GL state.** `RunPostProcessChain`'s screen-bound final draw ran with whatever state the last pipeline pass left: `pass_transparent` exits with `GL_BLEND` + `glBlendFunc(GL_ONE, GL_ONE)` (additive light loop), so a *single-effect* chain (e.g. HDR auto-ACES) **additively blended into the editor RT every frame → progressive overexposure** ("disable FXAA → blowout within seconds"; scene.bin + HDR-toggle hit the same path via the empty saved chain). Multi-effect chains only looked clean because the intermediate `chainPass->Bind(REPLACE)` reset blending before the final draw. Fixed by establishing explicit state (blend off, depth test off, depth writes off) for every chain draw. Verification: `[ACES]` frame 60 ≡ frame 300 pixel-for-pixel; 22-variant HDR/LDR × chain matrix on tank/outdoor/glass shows no blowout; fury↔furye lit-pixel parity within ~5%.


## Risks / Trade-offs

- **Forward transparent pass doubles light cost for transparent pixels** (multi-pass per light) → acceptable: transparent objects are typically few; revisit with light arrays if profiling hurts.
- **Depth texture sampling**: `gbuffer_depth` is a depth24 attachment; sampling it while it's *also* the depth attachment of the composite FBO would be undefined → transparent pass uses depth-test against gbuffer depth FBO as today (no sampling), and chain effects sample it only after the lighting stage is done (composite FBO has its own/no depth attachment — verify during implementation; if it aliases, copy depth once per frame or attach a depth stub).
- **SSR/SSAO quality** without denoise/half-res is modest → ship tunable uniforms; document that quality knobs are the upgrade path.
- **Additive alpha lighting approximation** can over-brighten overlapping transparents → documented; correct OIT is an explicit non-goal.
- **MASK spec drift**: gltf-importer spec currently says non-OPAQUE ⇒ `opaque=false`; MASK now stays opaque → spec delta updates that requirement.
- **Old scenes without `uniforms` in chain entries** → load path treats as empty overrides; no migration needed.

## Migration Plan

Purely additive: new pipeline pass (unused unless materials are non-opaque), new effect assets, optional `uniforms` field, new ini keys. Old scenes/materials load unchanged (missing `alpha_mode` ⇒ OPAQUE or derived from legacy `opaque` flag). Rollback = revert commit; scene files keep loading since unknown JSON fields are ignored.

## Open Questions

- Should DOF run pre- or post-tonemap by default in the shipped sample scene? (Pre-tonemap is HDR-correct; decide during visual tuning.)
- Does the LDR Lambert pipeline's `gbuffer_light` target have a usable depth attachment for the transparent pass — same texture naming? (Verify at implementation; name fallback already exists in chain code.)
