# Tasks

Conventions (carried from add-fft-ocean): ASCII-only source
(`tests/check_engine_ascii.py`); LF endings; every sampler bound every draw;
headless verification via `./fury exec`, visual via windowed `--screenshot`
(solo Bash call per shot - chained runs came back content-swapped twice);
engine units are cm; scene builders re-runnable, never write their source;
compute gates on `HasEffectiveCompute()` and tests must pass without it.
Player.lua hooks: `FURY_CAM="px,py,pz,yawDeg,pitchDeg[,farCm]"`, `FURY_TOD`,
`FURY_OCEAN_DEBUG=0..3`.

## 1. Done-in-prior-session (record for the archive)

- [x] 1.1 Night sky: `SkyAtmosphere` ctor defaults `moon_texture` /
  `cloud_noise` to the `Engine/Texture/Sky/` copies (Engine/ prefix resolves
  to the engine resource root from any scene working dir);
  `setup_ocean_base.lua` sets both explicitly. Root cause: the moon disc
  only draws when a texture is bound (`u_moon_enabled = enabled && tex`) and
  the ocean scene never set one - not an engine bug. Verified: night shot
  shows clouds + moon.
- [x] 1.2 Editor camera far plane: Editor.lua default 5000 -> 500000 cm,
  live slider "Far Plane (cm)" in Settings -> Camera (via
  `Editor.SetCameraSettings({controls={...}})` - note the table wraps a
  `controls` array and min/max keys, not vmin/vmax), frame-selection far
  never shrinks below the user setting. Verified: editor opens ocean_base
  and renders the horizon.

## 2. Ring seams (island V-lines)

Diagnosis (confirmed in code): the infinite mode's per-piece
`u_swell_fade`/`u_ripple_fade` uniforms are constant per ring piece, so
displacement + band normals STEP at every ring boundary; the ring frames are
square, so the steps read as V-shaped lines at the corners (user screenshot:
dark V lines in island water). The -2 cm/ring vertical tuck adds a matching
physical step. The overlap+tuck trick hides geometric cracks but not the
shading step.

- [x] 2.1 Replace the per-piece band fades with a smooth radial fade in the
  VS: `camDist = length(worldXZ - u_cam_xz)`; `rippleFade = 1 -
  smoothstep(rippleStart, rippleEnd, camDist)` (and same shape for swell
  further out), with the ranges derived from the ring radii (ripple fades
  across ring 1's band, swell tapers to ~0.3 at the skirt - the horizon
  keeps broad undulation). Band fades become camera-radial: no corners, no
  steps. Remove the per-piece fade uniforms from DrawOcean (or keep them as
  multipliers defaulting 1). IMPORTANT (from the shadertoy analysis): the
  current fades scale BOTH the VS displacement and the FS band normals
  (`ns.xz *= u_swell_fade; nr.xz *= u_ripple_fade;`) - the radial fade must
  feed both via a varying; fading only one gives "flat but sparkly" far
  rings.
- [x] 2.2 Verify: island + infinite screenshot pairs show no V-lines at
  shading resolution at multiple camera poses; wireframe debug still shows
  the ring structure; headless probes unaffected.

## 3. Distance fog

- [x] 3.1 OceanSurface FS: the sky-gradient reflection (4.3) already
  converges grazing water toward the sky horizon color and the AP LUT hazes
  on top; add the analytic distance lerp on top so far water fully converges
  (fade in from ~1 km), fallback analytic fog behind a define.
- [x] 3.2 Raise the default skirt radius past the AP range (4 km -> ~6 km) so
  the far water is fully fogged before the geometry ends. Re-check the
  vertex budget.

## 4. Water shading overhaul (target: reference plate)

Reference plate (user): deep desaturated blue-green body, choppy wave
silhouettes, bright sun-glitter path toward the sun, dark troughs with
subtle crest glow, horizon fog. The shadertoy breakdown (Seascape Ms2SD1 +
Water MdXyzX) with quoted formulas is in design.md; snippets below are
GLSL-ready (constants in cm, `viewDist = length(v_view_pos)`). The baked FFT
stays the wave function (better than both toys' analytic octaves) - this is
all fragment-side plus one new varying.

- [x] 4.1 Distance-flattened normals (kills far tiling/shimmer):
  `n = normalize(mix(n, vec3(0,1,0), 0.8 * min(1.0, sqrt(viewDist * 1.65e-5) * 1.1)))`
  after the existing u_normal_strength flatten.
- [x] 4.2 Distance-adaptive specular (the glitter path; Seascape's
  `600/|dist|` shininess in GGX form):
  `rough = mix(u_roughness, 0.35, smoothstep(3000.0, 80000.0, viewDist))`;
  drop the current `* 0.15` clamp on spec (if hot: `min(spec, 8.0)`).
  Consider near default roughness 0.08 -> 0.12.
- [x] 4.3 Sky-gradient reflection (fixes the dull horizon band + fake light
  color): `rdir.y = abs(rdir.y)`; Schlick fresnel capped `min(fres, 0.65)` so
  the body survives grazing; 2-color gradient on `rdir.y` (bright horizon ->
  deeper zenith) replaces the flat `skyTint`; `fres * skyCol * 0.5`.
  IMPLEMENTATION NOTE: with the atmosphere enabled the gradient comes from
  the per-frame sky-view LUT (u_skyview_lut, bound in DrawOcean) - the true
  sky color at any TOD, and the fog convergence target matches the sky pass
  exactly (an analytic gradient could not: golden-hour horizons are HDR
  warm). The 2-color gradient remains as the no-atmosphere fallback.
- [x] 4.4 Height-based trough darkening + crest glow: new VS varying
  `v_height` (post-displacement, pre-water-level); trough darkens body
  (`body *= 1.0 - 0.35 * clamp(-hN, 0.0, 1.0)`); crest adds sun-through
  scatter (`sss = scatter * clamp(hN,0,1) * (0.15 + 0.85 * towardSun^3) * 0.6
  * shadow`), distance-gated ~1 km. (`hN = clamp(v_height * 0.005, -1, 1)`.)
- [x] 4.5 Body color restructure + defaults (kills bright turquoise): wrapped
  diffuse `dif = pow(ndl * 0.4 + 0.6, 6.0)`; bodyLit = `body * (vec3(0.06,
  0.08, 0.10) + dif * shadow * sunCol)`. Component defaults darker: scatter
  (0.05,0.28,0.36) -> ~(0.03,0.14,0.19); absorb (0.02,0.10,0.16) ->
  ~(0.008,0.05,0.10). Keep current values reachable per-component.
- [x] 4.6 Optional anti-tiling: rotated/offset second ripple-normal sample
  (mat2 ~37 deg + 1.37 scale + constant offset, 0.6 weight) before the fade/
  flatten if 8 m period still reads after 4.1-4.3. Plus foam distance fade
  beyond ~200-600 m.
  DONE: foam distance fade implemented; the double-sample was skipped - the
  verification shots show no residual 8 m tiling after 4.1-4.3 (ripple band
  fades out by 64 m and the distance flatten covers the rest).
- [x] 4.7 SSAO gate off water: SSAO.glsl reads only gbuffer_normal.xyz; the
  alpha carries roughness - add `ao = mix(1.0, ao, smoothstep(0.05, 0.3,
  nrm.a))` (mirrors SSR's roughFade convention). REQUIRED companion:
  PrelightPipeline binds `u_ssr_roughness = GetSsrEnabled() ? GetRoughness()
  : 1.0f` - with SSR off the gate stops exempting water; change the bind to
  always write `GetRoughness()` (SSR's own `roughness < 0.95` gate is
  unaffected).
- [x] 4.8 Verify: infinite + island screenshots at noon and golden hour;
  close-range water shows no AO patches; wave silhouettes + glitter path
  visible at 3 m camera height; TOD response intact.

## 5. Editor UX

- [x] 5.1 Viewport toolbar: single debug droplist - the grid row stays, add
  an "Ocean" submenu (Off / Foam Mask / Displacement Heatmap / Ring
  Wireframe -> sets the selected/all ocean components' debugView), and move
  the SSAO View / SSR View toggles under a "PostProcess" submenu. One
  droplist hosts all debug views.
- [x] 5.2 Inspector: when `HasEffectiveCompute()` is false, render the ocean
  "Spectrum (GPU generation)" section disabled (BeginDisabled) with the
  resolved reason visible, so it's clearly not editable here.
- [x] 5.3 Content Browser: list OceanWaves as an asset type - filter list
  entry, `em->ForEach<OceanWaves>` collector, "OW" tile badge (Heightmap
  precedent); the inspector's OceanWaves picker (CollectOceanWaves) already
  exists.

## 6. Hygiene + verify

- [x] 6.1 tests/check_engine_ascii.py + LF endings on touched files; full
  tests/lua suite green in both compute modes.
- [x] 6.2 Update docs/OCEAN.md (radial fade, fog, shading plate, debug menu
  home, SSAO gate) + regen docs/LUA_API.md if bindings changed.
- [x] 6.3 Final screenshot set for the change + sync + archive.
