# Design: ocean-round2-polish

## Ring gaps (root cause)

`UpdateCameraFollow` snaps each piece origin to `floor(cam / cell) * cell`
with the piece's OWN cell size. Center cell 100 cm, ring cells 200/400/800:
origins differ by up to 100 cm, so a ring frame's inner edge and the
previous piece's outer edge (both `origin + R`) sit up to 100 cm apart ->
slits that show the below-horizon sky at grazing angles (the pale V/slit
artifacts in the user's wireframe shot).

Fix: snap every piece to the FINEST cell (center cell). All ring cell sizes
are multiples of it, and displacement is a pure function of world xz, so no
vertex swimming. Edges then coincide exactly.

T-junction residue: with edges coincident, the coarse piece's edge chord
between 200 cm vertices deviates from the fine piece's 100 cm-sampled edge
by the sub-cell wave detail. Ripple (8 m tile, ~8 cm amp) is the only band
fine enough to matter - so the ripple fade must be FULLY done at the ring-1
boundary: range [R0/2, R0] instead of [R0, 2*R0]. Swell (100 m tile) is
chord-identical at 200 cm sampling.

Skirt: radial fan from the last ring's outer square to the skirt radius.
Its ring spacing is ~250 m - it samples the 100 m swell band far below
Nyquist, and with the old 0.3 swell floor that aliased into the far white
glint dashes. Swell now fades to 0 across the last ring (floor removed: at
6 km the +-24 cm silhouette is ~0.03 px, so the floor bought nothing). The
skirt inner radius drops to 0.95x the last ring's outer half-extent so the
circle tucks UNDER the square everywhere (midpoints included), kept below
by the existing -2 cm/ring tuck.

## Foam + normal repetition

The visible repeat is the 100 m swell tile (crest foam ridges + normal
stripes from an elevated view). A 1024^2 rebake is 64x payload for nothing -
the fix is multi-scale sampling in the FS:

- Swell normals: second fetch of `u_nrm_swell` at uv' = rot37 * (uv * 0.62)
  + 0.37 offset, blended at 0.35 weight before the band fade - crossing-sea
  pattern breaks the strict 100 m period.
- Crest foam: `v_foam` (VS, f16) stays the base; the FS modulates with the
  swell NORMAL array's alpha (the bake stores a foam copy there - free, no
  extra fetch channel) at uv'' = rot37 * (uv * 3.73) + offset:
  `foam *= 0.55 + 0.9 * foam2`.
- Threshold: smoothstep(0.35, 0.9) -> (0.5, 1.0); default foam amount 0.6.

## Darker plate

Current noon look is dominated by body diffuse (sunCol ~3x body) plus a
0.65-cap fresnel sky reflection. Toward the reference (dark teal body,
reflection only at grazing):

- body ambient vec3(0.06,0.08,0.10) -> vec3(0.015,0.03,0.045)
- wrapped diffuse term weight: sunCol -> sunCol * 0.45
- scatter default (0.03,0.14,0.19) -> (0.025,0.11,0.15); absorb
  (0.008,0.05,0.10) -> (0.006,0.04,0.08)
- fresnel cap 0.65 -> 0.55; sky reflection weight 0.5 -> 0.4

Demo scenes rebuilt via the setup_ocean_* chain to bake the new defaults.

## Plank spin (root cause + fix)

Angular drag torque: `tau = -omega * drag * inertiaEst` with
`inertiaEst = mass * size^2` (size = |halfExtents|). Plank half extents
(100,10,25) -> inertiaEst = 128300; the real roll-axis inertia of a
200x20x50 cm box at mass 12 is m/12*(20^2+50^2) = 2900 - 44x less. At the
25 Hz fixed tick the explicit factor is `drag * inertiaEst / I * dt =
2 * 128300 / 2900 * 0.04 = 3.54` -> omega multiplies by (1 - 3.54) = -2.54
per tick: divergent flip-spin about the long axis. Sphere/cube factors are
0.24 / 1.08 - stable, which is why only the plank spins.

Fix: read the REAL local inverse inertia from Jolt
(`BodyLockRead` + `MotionProperties::GetLocalSpaceInverseInertia`), compute
the drag in body-local space per axis, and clamp the per-tick factor to
0.9 (`dw_i = -w_i * min(f_i, 0.9)`), converting back to a world torque.
Stable axes keep ~the old damping (cube 1.08 -> 0.9, 17% less, inside the
smoke test's tolerance); the plank caps at a 90%/tick kill.

Righting keeps the existing inertiaEst scaling: its spring rate
(sqrt(strength * inertiaEst / I)) stays under the 25 Hz stability limit for
all shipped shapes (cube 19 rad/s, plank 13 rad/s); documented as a
coefficient constraint.

## Droplist UX

- `ImGuiComboFlags_HeightLargest` on the combo: popup fits all rows, no
  scrollbar.
- `ImGuiSelectableFlags_DontClosePopups` on the overlay rows AND the
  Ocean/PostProcess submenu items: the popup survives item clicks and only
  closes on an outside click (standard multi-toggle behavior).

## OceanWaves asset flow

`Importer.MergeInto` strands OceanWaves in the discarded source scene (the
editor's open path does `active:Clear()` + `MergeInto`). Add the
`ForEach<OceanWaves>` transfer with the same stranding comment as
Heightmap. After that the Content Browser tile (5.3, already merged) and
the inspector picker populate.

Inspector wave-asset row mirrors the Terrain heightmap row:
`Change##wave` / `->##wave` (SelectAssetInBrowser) / `x##wave` /
`OceanWaves: <name>` + a stats line (`2 bands, 32 frames x 12.0s, tiles
10000/800 cm`) from the resolved waves; the raw path InputText goes away.

## Bake previews

`gen_ocean_assets.py` writes per band:
- `<band>_nrm_preview.png` - frame 0 of the normal array (already RGBA8 in
  [0,1], straight dump).
- `<band>_foam_preview.png` - max projection of the foam channel over
  frames, grayscale.
Via a minimal stdlib PNG writer (zlib + struct; the tool is stdlib-only).
The .f16/.u8 payloads stay authoritative (16-bit displacement range and
negative values don't survive 8-bit PNG).

Content Browser: the OW tile renders `<dir>/preview image` (swell nrm
preview next to the waves' json path) as its thumbnail when present, else
the flat placeholder. Lazy Texture load cached per OceanWaves path; not
serialized (editor-only cache).

## Risks

- The fade-range change moves ripple detail off ring 1; acceptable: ripple
  normals are distance-flattened to ~36% by 100 m anyway, and the VS
  displacement match at the boundary is what kills the grazing slits.
- Common-grid snapping changes piece origins by < 1 coarse cell vs before -
  no visual discontinuity (world-pure wave field), but buoyancy + visuals
  stay exactly in sync either way.
- Preview PNG loading in the content browser must not run headless
  (Texture::CreateFromImage needs GL): guard on the tile render path only.
