# Tasks: expand-shadow-receive-csm-spot

## 1. Verify single-map directional (code-complete, untested)

- [ ] 1.1 Test scene: copy of `outdoor_water` with `DefaultSun.cast_shadows: true` and `renderSettings.cascaded_shadow_map: false`. Confirm `DrawDirLightShadowMap` runs, `m_LastShadowMatrices` populates, and the smoke dims in tree shadow while the fire (ADDITIVE) stays bright. If no BLEND mesh exists in the scene, drop one test transparent quad (e.g. a glass pane) to exercise the Forward.glsl branch too.
- [ ] 1.2 Tune the dir bias if acne shows on billboards (v1 copies the deferred "no shader bias, caster polygon offset" convention — billboards at grazing angles may need a small constant; keep the `u_shadow_floor` ambient floor in mind when judging "too dark").

## 2. CSM receive (main item)

- [ ] 2.1 Shader: extend the `u_shadow_type == 3` branch (new value) in `Particle.glsl` + `Forward.glsl`: `sampler2DArray shadow_buffer_csm`, `uniform mat4 shadow_matrix_csm[4]`, `uniform vec4 shadow_far`; cascade select by view depth, then `z > 1 → lit else z < tex` per the deferred `SunLight.glsl` CSM block (line ~180). **Cascade selection space:** the deferred CSM splits on LINEAR view depth (`shadow_far` is built from camera near/far in the light draw) — the particle/forward shaders have true view-space position already (`v_view_z` / `vs_pos.z`), so selection is straightforward; do NOT reconstruct depth.
- [ ] 2.2 Dummy for `sampler2DArray` (`GetDummyTexture2DArray()`, 1×1×4) and ALWAYS-bind rule for the new sampler in `ParticleRenderer::FinishDraw` + `PrelightPipeline::DrawUnit` — same core-GL mismatch trap as 11.5.
- [ ] 2.3 Pipeline: cache the 4 CSM matrices + `shadow_far` per light (`m_LastShadowMatrices` → per-light `std::vector<Matrix4>` or a small struct {matrices[4], far}); populate in `DrawDirLight`'s cascaded path (the data already exists as `cascadedShadowData`); keep releasing the CSM texture via `m_FrameShadowTemps`. Bind in the particle block + DrawUnit when `IsSwitchOn(CASCADED_SHADOW_MAP)` and a cached CSM light exists.
- [ ] 2.4 Verify on `outdoor_water` with CSM back ON + sun casting: tree shadows fall on smoke consistently across near/far cascade boundaries (watch the cascade seam — the deferred shader's `shadow_far` split is uniform quarter-far, not PSSM).

## 3. Spot receive

- [ ] 3.1 `u_shadow_type == 4`: 2D map + `shadow_matrix` (spot's view-proj) + cone test already in Forward.glsl's `u_light_type == 3` attenuation — shadow factor multiplies the same radiance. Cache the spot matrix in `m_LastShadowMatrices` in `DrawSpotLight` (it already returns one).
- [ ] 3.2 Test: a spot light aimed through occluders at a smoke emitter + a blend-mesh pane.

## 4. Multi-light selection for particles

- [ ] 4.1 Replace "first casting light wins" in the particle block with nearest-by-influence (e.g. max `intensity / distance²` to the emitter center, or simply nearest casting light). Keep mesh transparents as-is (they already evaluate every light per-unit in the additive loop).
- [ ] 4.2 Document the remaining approximation: ONE shadow source per frame for particles (like Unity's single dominant-light shadow on cheap particles).

## 5. Docs/spec

- [ ] 5.1 Sync `add-particle-system-fire-smoke-editor`'s transparent-rendering spec: the `u_shadow_type` contract gains 3=CSM, 4=spot; the "CSM and spot bind 0" note is removed from the v1 requirement text (it stays in the archived change as historical scope).
- [ ] 5.2 Note in `docs/` (or the particle spec) that shadow-receive supports point/dir-single/CSM/spot after this change.

## Known traps (do not re-derive)

- **Sampler/target mismatch kills the whole draw silently** (core GL, `GL_INVALID_OPERATION` at `glDrawElements` only) — every declared sampler bound every draw, dummies for unused. See `gl-sampler-target-mismatch-trap` memory + tasks.md 11.5 of the parent change.
- **gbuffer depth is LINEAR** (`-viewZ/camera_far`); any new shadow-compare code must stay in that space.
- **Temp shadow maps recycle mid-frame by spec** — hold them in `m_FrameShadowTemps` until end of Execute (already the pattern; CSM textures included).
- **Dummies must match the sampler TYPE** — CSM needs `sampler2DArray`, not the 2D dummy.
- The outdoor scene's inner `RootNode` is ×100 — shadow bias tuning is world-unit; light radius 10 local = 1000 world.
