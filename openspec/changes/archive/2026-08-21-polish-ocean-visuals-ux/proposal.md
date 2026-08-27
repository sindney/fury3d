# Proposal: polish-ocean-visuals-ux

## Why

The FFT ocean (archived 2026-08-20) works end-to-end, but the first visual
review round surfaced a batch of polish items across the editor UX, the water
look, and one geometry artifact:

- The night sky in ocean scenes is pure dark: the moon disc only draws when a
  texture is bound (`u_moon_enabled = enabled && texture`), and the ocean
  scene never set one (the terrain scene sets it explicitly).
- The editor camera's far plane is hardcoded 5000 cm (50 m) in Editor.lua -
  it clips the ocean demos' horizons; there is no user control.
- The island scene shows dark V-shaped lines in the water: the infinite
  mode's per-piece band fades step at the (square) ring-frame boundaries,
  and the -2 cm/ring vertical tuck adds a physical step - a shading seam,
  not a crack.
- The infinite ocean ends in a visible skirt-edge line: no fog lerps the
  water into the horizon haze.
- The water reads light/fake/repetitive vs the target plate (deep
  blue-green body, choppy silhouettes, sun glitter path, crest glow,
  horizon fog), and SSAO darkens the water at close range.
- The baked ocean asset (`ocean.json`) is invisible to the Content Browser
  (can't be viewed/selected like images/materials).
- The inspector's GPU spectrum section is editable even where compute never
  exists (macOS) - no signal to the user that it's dead.
- Ocean debug views live only on the component; the viewport's debug
  droplist should host them (plus SSAO/SSR under a PostProcess submenu).

## What Changes

- SkyAtmosphere: engine-root default moon/cloud texture paths (Engine/
  prefix resolves from any working dir); ocean scene sets them explicitly.
  **[DONE 2026-08-20, verified night shot]**
- Editor.lua: editor camera far default 5000 -> 500000 cm, live "Far Plane
  (cm)" slider under Settings -> Camera via `Editor.SetCameraSettings`;
  frame-selection never shrinks far below the user setting.
  **[DONE 2026-08-20, verified editor opens + renders the scene]**
- OceanComponent/OceanSurface: replace per-piece constant band fades with a
  smooth radial camera-distance fade in the vertex shader (kills the
  square-ring shading seams; also improves distance aliasing).
- OceanSurface FS: analytic distance fog lerping toward the AP/sky horizon
  color; skirt radius default beyond the AP range so the far edge is fully
  fogged.
- OceanSurface FS visual overhaul toward the reference plate (shadertoy
  Seascape Ms2SD1 + Water MdXyzX - see design.md): darker absorb/scatter,
  sharper GGX sun + glitter term, crest-height scatter glow, ripple normal
  distance fade; SSAO gated off low-roughness (water) pixels.
- Viewport toolbar: single debug droplist - Grid row, Ocean submenu
  (Off/Foam/Displacement/Wireframe drives the component's debugView),
  PostProcess submenu (SSAO View / SSR View - moved from their current
  home).
- Content Browser: OceanWaves listed as an asset type (filter list,
  collector, "OW" badge) mirroring Heightmap; pairs with the inspector's
  OceanWaves picker.
- Inspector: the ocean Spectrum (GPU generation) section is disabled with
  the resolved reason when `HasEffectiveCompute()` is false.

## Non-Goals

- No new geometry model (rings stay; the seam fix is shading-side).
- No per-frame GPU wave simulation (the compute contract is unchanged:
  GPU-bake-at-load only).
- No refraction / underwater view / foam trails (carried over from
  add-fft-ocean non-goals).

## Verification

Per the add-fft-ocean conventions: headless `./fury exec` for logic,
windowed `--screenshot` for visuals, the full tests/lua suite green in both
compute modes, `tests/check_engine_ascii.py` clean, LF endings. Screenshot
pairs for the seam fix (island water) and the shading overhaul (infinite +
island, day + golden hour).
