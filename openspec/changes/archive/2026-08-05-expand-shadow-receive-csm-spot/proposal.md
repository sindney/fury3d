# Proposal: expand shadow-receive to CSM + spot (particles & transparents)

## Intent

The shadow-receive path shipped in `add-particle-system-fire-smoke-editor`
(2026-08-03) covers **point lights** (cube map, tested on `outdoor_water`'s
Fire light) and **single-map directional** (code complete, visually untested)
for both `ParticleShader` (ALPHA systems, `RendererModule.receiveShadows`)
and mesh transparents (Forward.glsl per-light additive loop). This change
closes the remaining gaps: **cascaded shadow maps (CSM)** and **spot
lights**, plus the missing verification for single-map directional.

## Why now

`outdoor_water`'s `renderSettings` has `cascaded_shadow_map: true` — so the
moment `DefaultSun.cast_shadows` is flipped on, the sun's shadows go through
the CSM path and transparents/particles receive **nothing** (they bind
`u_shadow_type = 0` by design today). The sun is the scene's main shadow
source; CSM receive is the practically important gap. Spot completes the
light-type matrix.

## Current state (what exists)

- `u_shadow_type` uniform contract: 0 none, 1 point cube, 2 directional 2D.
  `ParticleShader` (`examples/Resource/Shader/Lambert/Particle.glsl`) and
  `Forward.glsl` both implement branches 1 and 2.
- Point: 512² DEPTH24 cube, radial-distance compare with distance-scaled
  bias; outside the light's radius counts as lit (both shaders).
- Single-map dir: deferred `SunLight.glsl` convention (`z > 1 → lit,
  else z < tex`, caster polygon offset instead of shader bias); matrix
  cached per light in `Pipeline::m_LastShadowMatrices` (deferred
  view-space convention; particles pre-multiply by the camera view matrix).
- Shadow-map temporaries are released at END of `Execute`
  (`m_FrameShadowTemps`) — the temp pool recycles by spec mid-frame.
- Every declared sampler is bound every draw (1×1 dummies
  `GetDummyCubeTexture()` / `GetDummyTexture2D()`) — core GL kills
  `glDrawElements` on sampler/target mismatch. **Any new sampler type
  added here (sampler2DArray for CSM) needs its own dummy.**
- Light selection: first shadow-casting light with a cached map feeds the
  whole particle block (mesh transparents evaluate per light in the
  additive loop already). CSM + spot bind `u_shadow_type = 0` by design.

## Scope

1. **Verify single-map directional** end-to-end (it is code-complete but
   never visually exercised — flip `DefaultSun.cast_shadows` on with CSM
   OFF in a test scene; smoke + a blend-mesh test subject dim in tree
   shadow).
2. **CSM receive** for particles + mesh transparents (the main item).
3. **Spot receive** (cone falloff + 2D map).
4. **Multi-light selection** for particles (v1 takes the first casting
   light; pick by influence instead).

## Non-goals

- Soft particles (depth-fade against gbuffer depth).
- Per-light additive accumulation for particles (particles keep the
  single-tint emissive model; this change only widens which lights can
  shadow them).
- Point-light shadow for ADDITIVE systems (they stay light-emitting).
