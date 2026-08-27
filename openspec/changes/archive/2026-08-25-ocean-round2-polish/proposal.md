# Proposal: ocean-round2-polish

## Why

User review of `polish-ocean-visuals-ux` (archived 2026-08-21) on live scenes
surfaced a second round of issues that the screenshot-only verification
missed:

- **Geometric ring gaps**: each ring piece snaps its origin to a multiple of
  its OWN cell size, so neighboring pieces' shared edges sit up to half a
  coarse cell apart in world space - at grazing angles the gaps show the
  background through (visible as light slits between the square ring
  frames). The skirt's inner circle also only touches the last ring's square
  edge midpoints, and its 250 m ring sampling aliases the 100 m swell band
  into far-field glint dashes.
- **Foam reads too big and too repetitive**: the Jacobian crest-foam
  threshold is permissive and the 100 m swell tile repeats visibly from any
  elevated view; same for the swell normal pattern.
- **Water still reads cartoon-bright** vs the reference plate (deep dark
  blue-green body, sky reflection only at grazing, sun glitter path).
- **Viewport Debug droplist UX**: shows a scrollbar with 8 items, and any
  click closes the whole popup (toggling several views is tedious).
- **The plank floater spins fast**: the buoyancy angular-drag torque uses a
  crude `mass * size^2` inertia estimate; for the plank's thin-roll axis the
  estimate is ~44x the real inertia, so at the 25 Hz fixed tick the explicit
  integration overshoots (per-tick factor > 2) and the rotation diverges.
  Spheres/cubes happen to stay under the stability limit.
- **OceanWaves asset is invisible to the editor**: `Importer.MergeInto`
  (used by the editor's open-scene path) transfers textures/materials/meshes/
  clips/particles/heightmaps but not OceanWaves, so the asset is stranded in
  the discarded source scene - the Content Browser tile and the inspector
  picker both come up empty.
- **Inspector wave-asset row is a raw text field**; it should be the same
  Change / jump / clear chip as the Terrain's heightmap row, with an info
  line.
- **Wave data is not visualizable**: the bake payload is `.f16`/`.u8` binary
  arrays (required for precision); there are no image previews to eyeball.

## What Changes

- OceanComponent: snap ALL ring piece origins to the center (finest) cell
  grid so shared edges coincide exactly; ripple fade ends at the ring-1
  boundary and swell fades to 0 across the last ring (flat skirt, no
  aliased glints; the 0.3 swell floor was subpixel at 6 km and only fed
  skirt aliasing); skirt inner radius overlaps the last ring (x 0.95).
- OceanSurface FS: crest foam threshold tightened (smoothstep 0.35-0.9 ->
  0.5-1.0) + default foam amount 1.0 -> 0.6; anti-tiling via a second
  rotated (37 deg) fetch: foam modulated by the swell normal array's foam
  copy at 3.73x tile, swell normals blended with a 0.62x-tile rotated copy
  at 0.35 weight (breaks the 100 m repetition without a bigger bake - 1024^2
  bands would be 64x the payload for no physics gain).
- OceanSurface FS: darker plate constants (weaker body ambient/diffuse,
  tighter fresnel cap, lower sky-reflection weight) toward the reference
  screenshot.
- Viewport Debug droplist: `ImGuiComboFlags_HeightLargest` (no scrollbar)
  and `ImGuiSelectableFlags_DontClosePopups` on every row including the
  Ocean/PostProcess submenus (popup closes only on outside click).
- BuoyancyComponent: angular drag clamps per body-local axis using the real
  Jolt inverse inertia (BodyLockRead) so the per-tick velocity change never
  exceeds 90% - unconditionally stable for any shape; coefficients keep
  their meaning where the old behavior was already stable (sphere/cube
  unchanged within tolerance).
- LuaBindings `MergeInto`: transfer `OceanWaves` entities (same stranding
  comment as Heightmap/ParticleSystem).
- Inspector: wave-asset row becomes Change / -> / x + `OceanWaves: <name>`
  + a stats line (bands/frames/loop/tiles), mirroring the Terrain
  heightmap row.
- Bake tool: `tools/gen_ocean_assets.py` additionally writes preview PNGs
  (per-band normal frame 0 + foam max-projection) via a stdlib PNG writer;
  the payload stays .f16/.u8 (precision), PNGs are for humans; the Content
  Browser OW tile shows the swell normal preview as its thumbnail when the
  file exists.

## Non-Goals

- No higher-resolution rebake of the committed ocean asset (memory-bound;
  multi-scale sampling addresses the visible repetition).
- No displacement-path anti-tiling: buoyancy samples the same field
  (visual/physics consistency), only normals + foam get the second fetch.
- No SSR strength changes (scene postfx setting, out of scope).

## Addendum after user review (2026-08-21/24)

- The horizon "unfogged band" was the skirt being BACKFACE-CULLED since the
  beginning (BuildSkirt wound downward): the far "water" was the sky's
  below-horizon row. Winding flipped; fog now converges to the sky-view LUT
  one row above the horizon.
- Third "chop" cascade added (3 m tile, normals + foam only): fills the
  0.5-4 m gap, and its normals break the noon down-sun specular lobe into
  glints (that lobe was a white-out; now capped sun-elevation-scaled).
- Bake spectrum cutoff made tile-relative: the old fetch-relative cutoff
  had degenerated the 8 m ripple band into a single-mode plane wave (the
  visible dash repetition the user flagged).
- Crest foam thresholds the absolute summed bake value: calm stays
  foam-free, storms whitecap (storm sample bake + scene included).
- Default bake moved to the engine resource root (Engine/Ocean/ocean.json).

## Verification

Headless `./fury exec` suite green in both compute modes (plus a new plank
spin probe asserting angular velocity settles); windowed `--screenshot`
matrix on the `fury` player binary at frame 30+: grazing wireframe (no
gaps), island high oblique (no 100 m foam repeat), noon + golden-hour
infinite (darker plate), editor droplist open with room to spare.
**Archive only after the user's interactive pass confirms the look.**
