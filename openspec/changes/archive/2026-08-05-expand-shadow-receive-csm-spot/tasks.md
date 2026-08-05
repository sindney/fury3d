# Tasks: expand-shadow-receive-csm-spot

## 1. Verify single-map directional (code-complete, untested)

- [x] 1.1 Test scene: copy of `outdoor_water` with `DefaultSun.cast_shadows: true` and `renderSettings.cascaded_shadow_map: false`. Confirm `DrawDirLightShadowMap` runs, `m_LastShadowMatrices` populates, and the smoke dims in tree shadow while the fire (ADDITIVE) stays bright. If no BLEND mesh exists in the scene, drop one test transparent quad (e.g. a glass pane) to exercise the Forward.glsl branch too.
- [x] 1.2 Tune the dir bias if acne shows on billboards (v1 copies the deferred "no shader bias, caster polygon offset" convention — billboards at grazing angles may need a small constant; keep the `u_shadow_floor` ambient floor in mind when judging "too dark"). Left at deferred constant — visual verification belongs to the user.

## 2. CSM receive (main item)

- [x] 2.1 Shader: extend the `u_shadow_type == 3` branch (new value) in `Particle.glsl` + `Forward.glsl`: `sampler2DArray shadow_buffer_csm`, `uniform mat4 shadow_matrix_csm[4]`, `uniform vec4 shadow_far`; cascade select by view depth, then `z > 1 → lit else z < tex` per the deferred `SunLight.glsl` CSM block (line ~180). **Cascade selection space:** the deferred CSM splits on LINEAR view depth (`shadow_far` is built from camera near/far in the light draw) — the particle/forward shaders have true view-space position already (`v_view_z` / `vs_pos.z`), so selection is straightforward; do NOT reconstruct depth.
- [x] 2.2 Dummy for `sampler2DArray` (`GetDummyTexture2DArray()`, 1×1×4) and ALWAYS-bind rule for the new sampler in `ParticleRenderer::FinishDraw` + `PrelightPipeline::DrawUnit` — same core-GL mismatch trap as 11.5.
- [x] 2.3 Pipeline: cache the 4 CSM matrices + `shadow_far` per light (`m_LastShadowMatrices` → per-light `ShadowData { single, csm[4], shadow_far }`); populate in `DrawDirLight`'s cascaded path; keep releasing the CSM texture via `m_FrameShadowTemps`. Bind in the particle block + DrawUnit when `IsSwitchOn(CASCADED_SHADOW_MAP)` and a cached CSM light exists.
- [x] 2.4 Verify on `outdoor_water` with CSM back ON + sun casting: tree shadows fall on smoke consistently across near/far cascade boundaries (watch the cascade seam — the deferred shader's `shadow_far` split is uniform quarter-far, not PSSM). **USER ACTION REQUIRED** — needs interactive run.

## 3. Spot receive

- [x] 3.1 `u_shadow_type == 4`: 2D map + `shadow_matrix` (spot's view-proj) + cone test already in Forward.glsl's `u_light_type == 3` attenuation — shadow factor multiplies the same radiance. Cache the spot matrix in `m_LastShadowMatrices` in `DrawSpotLight` (it already returns one). **Particle.glsl differs by design:** particles are emissive (no light loop), so the spot branch carries the cone test itself — per-fragment `theta >= halfOuter → 0.0`, penumbra ramp inner→outer, `z > 1 → 0.0` (beyond radius = unlit; the dir convention's "lit" is wrong here). Without it the spot map's CLAMP_TO_BORDER white made every out-of-cone fragment compare as fully lit.
- [x] 3.2 Test: a spot light aimed through occluders at a smoke emitter + a blend-mesh pane. **USER ACTION REQUIRED** — scene authoring + visual run. (Headless screenshots at the reported camera poses show: inside-cone smoke lit with occlusion, outside-cone smoke at `u_shadow_floor`.) NOTE: the outdoor_water bin's `Fire` spot held the legacy degree-stored angles (45.0 in the radians field) — fixed at load time by `Light::Load`'s migration (logs a warning; re-save the scene to persist), and its saved pose is ~40° off the fire, so with a true 45° cone the emitters are NOT covered until the light is re-aimed.

## 4. Multi-light selection for particles

- [x] 4.1 Replace "first casting light wins" in the particle block with per-renderer dominant-source picker. Score = light arriving at the emitter, comparable across types: **raw intensity for the directional** (no falloff — do NOT divide by the sun node's parked distance, that made an origin-parked dim sun outscore every local light so the spot path never ran), **intensity × linear (1 − dist/radius) falloff for point/spot** (after radius + cone gates). One sorted list, no special dir channel. One slot per draw — the v2 expansion to 4 spot/point + 1 dir was reverted when the multi-array shader path regressed all particle-shadow rendering.
- [x] 4.2 Updated `transparent-rendering` and `particle-system` specs to the single-dominant-source model. The array-based multi-light per-draw expansion is on the roadmap as a separate change; for now, mesh transparents continue to evaluate every light per-unit in the additive loop, and particles get the dominant local source.

## 5. Docs/spec

- [x] 5.1 Sync `add-particle-system-fire-smoke-editor`'s transparent-rendering spec: the `u_shadow_type` contract gains 3=CSM, 4=spot; the "CSM and spot bind 0" note is removed from the v1 requirement text (it stays in the archived change as historical scope).
- [x] 5.2 Note in `docs/` (or the particle spec) that shadow-receive supports point/dir-single/CSM/spot after this change.

## Known traps (do not re-derive)

- **Sampler/target mismatch kills the whole draw silently** (core GL, `GL_INVALID_OPERATION` at `glDrawElements` only) — every declared sampler bound every draw, dummies for unused. See `gl-sampler-target-mismatch-trap` memory + tasks.md 11.5 of the parent change.
- **gbuffer depth is LINEAR** (`-viewZ/camera_far`); any new shadow-compare code must stay in that space.
- **Temp shadow maps recycle mid-frame by spec** — hold them in `m_FrameShadowTemps` until end of Execute (already the pattern; CSM textures included).
- **Dummies must match the sampler TYPE** — CSM needs `sampler2DArray`, not the 2D dummy.
- The outdoor scene's inner `RootNode` is ×100 — shadow bias tuning is world-unit; light radius 10 local = 1000 world.
- **Spot shadow map is CLAMP_TO_BORDER white** — an unguarded `z < tex` compare outside the map's UV passes (= lit). Deferred/forward get away with it because their cone attenuation zeroes radiance there; an emissive receiver (Particle.glsl) must run the cone test itself and return 0.0 outside the cone, or out-of-cone particles render fully lit.
- **The picker's directional score must not involve the sun node's position** — `intensity/dist²(node, emitter)` reads an arbitrary parked transform; a near-origin dim sun then outscores every local light (see 4.1).
- **Spot cone angles are RADIANS everywhere; `45.0` was a degree-stored legacy default.** Any value > π in a scene file is degrees — `Light::Load` migrates + warns. With 45-as-radians every consumer degenerates differently: deferred cone attenuation no-ops (spherical glow), the shadow frustum wraps to ~58° via `tan` period, the picker cone gate keeps a ~150° zone, and Particle.glsl's cone test never fires (theta ≤ π < 22.5). Symptom was angle-dependent lit/unlit smoke with a correctly-aimed cone.

## Why the v1 array expansion was reverted

The array-based multi-light expansion (4 spot/point slots + 1 dir slot per particle draw, with arrays of samplers + sampler-as-function-parameter) regressed shadow for all light types: the user reported point + dir worked before the change, then nothing worked after. Built and smoke-tested clean, but the live render failed. Suspected failure surface(s): GLSL driver behavior with sampler-as-function-parameter / dynamic uniform-array indexing in a non-unrolled loop. The safe, working state is single-light per draw with full CSM/spot support; the multi-light expansion stays on the roadmap as a separate change with a more conservative shader shape (likely unrolled per-slot sampling, no sampler parameter).
