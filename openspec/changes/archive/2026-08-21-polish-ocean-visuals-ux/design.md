# Design: polish-ocean-visuals-ux

## Context

The FFT ocean shipped in add-fft-ocean (archived 2026-08-20): baked looping
FFT bands replayed on ring-LOD geometry, pass_ocean writes gbuffer depth +
normal, SSR sees water. This change is the first visual/UX review round.

## Ring seams (task 2)

Diagnosis: per-piece `u_swell_fade`/`u_ripple_fade` constants step at ring
boundaries; square ring frames make the steps V-shaped at corners; the
-2 cm/ring tuck adds a physical step. The overlap+tuck hides cracks, not the
shading step.

Fix: radial camera-distance fade in the VS replacing per-piece constants.
The fades currently scale BOTH the VS displacement and the FS band normals
(`ns.xz *= u_swell_fade; nr.xz *= u_ripple_fade;`) — the radial fade must
feed both (vertex + fragment varyings); fading only one of them produces
"flat but sparkly" far rings.

## Shading overhaul (task 4) — shadertoy analysis summary

References: Seascape (Ms2SD1) + Water (MdXyzX). Our pipeline keeps the baked
FFT as the wave function (better than both toys' analytic octaves); the gap
is all in the fragment shader. Full technique table in
shadertoy-analysis.md (archived in this change). Change list by impact-per-risk
(constants in cm, `viewDist = length(v_view_pos)`):

1. **Distance-flattened normals** (kills far tiling/shimmer):
   `n = normalize(mix(n, vec3(0,1,0), 0.8 * min(1.0, sqrt(viewDist * 1.65e-5) * 1.1)))`
   (Water's normal-flatten rescaled: full flatten ~500 m.)
2. **Distance-adaptive specular** (the glitter path — Seascape's
   `specular(..., 600/sqrt|dist|)` in GGX form):
   `rough = mix(u_roughness, 0.35, smoothstep(3000.0, 80000.0, viewDist))`,
   drop the current `* 0.15` clamp (if hot: `min(spec, 8.0)`, don't scale).
3. **Sky-gradient reflection** (fixes the dull horizon band + fake color):
   `rdir.y = abs(rdir.y)` (Water), Schlick fresnel capped at 0.65 so the body
   survives grazing, 2-color gradient on `rdir.y` (bright horizon -> deeper
   zenith) replacing the flat `skyTint`; `fres * skyCol * 0.5` (AP hazes
   further at range — water and sky share the same haze).
4. **Height-based trough darkening + crest glow**: new varying `v_height`
   from the VS (post-displacement, pre-water-level); trough darkens body,
   crest adds scatter boosted toward the sun
   (`sss = scatter * clamp(hN,0,1) * (0.15 + 0.85 * towardSun^3) * 0.6 * shadow`),
   distance-gated (~1 km).
5. **Body color restructure** (kills bright turquoise): A-style mostly-base
   color with wrapped diffuse `pow(ndl * 0.4 + 0.6, 6.0)`; component defaults
   darker — scatter (0.05,0.28,0.36) -> ~(0.03,0.14,0.19), absorb
   (0.02,0.10,0.16) -> ~(0.008,0.05,0.10).
6. Optional: ripple anti-tiling double sample (rotated mat2 + 1.37 offset
   second fetch) if far tiling still reads after 1-3.
7. Foam distance fade beyond ~200-600 m.

## SSAO-on-water gate

SSAO.glsl reads only gbuffer_normal.xyz; the alpha carries roughness. One
line: `ao = mix(1.0, ao, smoothstep(0.05, 0.3, nrm.a))` — mirrors SSR's
existing `roughFade` convention so both effects agree on "smooth = water".

Companion fix (required): PrelightPipeline binds
`u_ssr_roughness = GetSsrEnabled() ? GetRoughness() : 1.0f` — with SSR off,
water writes 1.0 and the gate stops exempting it. Change the bind to always
write `GetRoughness()` (SSR's own `roughness < 0.95` gate is unaffected).

## Fog (task 3)

With (3) landed, grazing water already converges toward the sky horizon
color; the AP LUT hazes on top. The analytic distance-fog lerp then only
needs to cover the skirt edge (raise the skirt default past the 5 km AP
range; the far ring is fully fogged before the geometry ends).

## Editor UX (task 5)

Viewport toolbar becomes one debug droplist: grid row + Ocean submenu +
PostProcess submenu (SSAO/SSR view toggles move there). Inspector spectrum
section disabled with the reason when compute is ineffective. Content
Browser lists OceanWaves (filter entry + ForEach collector + OW badge).

## Risks

- The shading constants are screenshot-tuned; keep the current values behind
  the same uniforms so scenes can roll back per-component.
- The radial fade changes far-field displacement amplitude; buoyancy samples
  the full arrays (CPU sampler is fade-free) — document that visuals and
  physics diverge by the fade at range (buoyancy gameplay stays near-field).
