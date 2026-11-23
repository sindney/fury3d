# Fury3D — Vegetation (Kraut trees, LODs, billboards, instancing, wind)

> Kraut trees use unlit color atlases and object-space normal atlases for
> runtime lighting. Mesh export and atlas baking share the canopy normal
> model. LOD coverage is unsaturated; tier 0 owns coverage >= threshold(1).
> `FURY_LOD_JITTER=0` disables per-instance transition jitter.

## Overview

Trees are generated offline by the vendored Kraut-CLI toolchain
(`engine/ThirdParty/Kraut` submodule, `furye-cli kraut` wraps it) and imported
through the regular glTF importer with a kraut-aware postprocess. Rendering
adds: per-tier LOD chains with a billboard terminal tier, two-sided
alpha-cutout foliage shading, leaf-shaped shadows (alpha-tested depth
passes), wind sway from vertex-color weights, and ISM/HISM instanced
rendering for forests.

![Island with instanced palms](../screenshots/ocean_island_trees_grove.png)

## The toolchain

```
furye-cli kraut generate <descriptor.tree> [--seed N] [--out dir]
furye-cli kraut import   <tree.glb> [output.json|.bin]
```

- `generate` runs KrautCLI's `--format glb` export (per-LOD meshes
  `<tree>_LOD<n>`, billboard quad `<tree>_Billboard`, `COLOR_0` wind weights,
  PBR materials, `asset.extras.kraut` with LOD thresholds + billboard atlas
  grid + provenance), copies the referenced textures next to the glb
  (`.dds` resolves to `.tga`/`.png` siblings), bakes the billboard atlas with
  KrautPreview (`<stem>_BillboardAtlas.png` and `<stem>_BillboardNormals.png`,
  matching rows of orthographic views), and writes per-tier preview screenshots. Deterministic per
  descriptor + seed.
- `import` reads the glb into a scene fragment: the LOD chain keeps the
  extras' thresholds, the billboard becomes the flagged terminal tier,
  foliage materials get MASK + two-sided + wind flags, and the tree is
  scaled meters -> cm at its root (the engine's world unit is cm).

The CMake `kraut_tools` target builds the binaries next to `fury`/`furye`
(POST_BUILD copy). `-DFURY_WITH_KRAUT=OFF` skips the toolchain;
`-DFURY_WITH_KRAUT_PREVIEW=OFF` builds KrautCLI without KrautPreview (no
SDL2 fetch; atlas/previews skipped with a warning at generate time).

Committed samples: `examples/Resource/Trees/{Descriptors,PalmTree2,Tree1}/`
+ `trees.json`, baked by `tools/gen_tree_assets.py` (`--check` verifies the
committed fragments, glb exports, and textures are current). A fresh checkout without the Kraut
submodule still renders the samples. The bake also tone-maps the kraut
content leaf textures (bright lime by default) toward deep palm green via
`FOLIAGE_TONE` -- applied to the foliage textures AND the billboard atlas so
the terminal tier stays color-consistent.

After changing the exporter or baker, regenerate and re-import the tree.
Project scenes embed their mesh and material records: refresh those records
as well as the standalone tree fragment. Replacing only the color PNG cannot
add the normal-atlas binding. Imports reject missing normal atlases and
unsupported normal spaces or canopy-model versions.

**sRGB caveat**: albedo textures must carry `srgb: true` in the scene's
texture records or the HDR pipeline samples gamma-encoded values as linear
(over-bright, over-saturated). The glTF importer sets this correctly on
baseColor textures, but records baked by early pipeline iterations can be
stale -- check the record, and note that a scene's material blocks embed
their OWN texture records (fixing the entity-level entry alone doesn't fix
the material's; `Material::SetTextureSRGB`/`GetTextureSRGB` bindings exist
for that).

## Rendering pieces

- **LOD chain + billboard flag**: `Mesh::m_LodBillboardFlags` (parallel to
  `m_LodMeshes`, serialized as `lod_billboard_flags`). `IsLodBillboard(i)`
  marks the terminal tier; that tier renders via the BILLBOARD shader
  variant and is skipped in shadow passes (the deepest non-billboard tier
  casts instead). The billboard tier's material travels on the mesh
  (`Mesh::m_BillboardMaterial`, `lod_billboard_material`) because renderer
  material slots key to LOD 0's submesh layout. LOD thresholds come from
  `extras.kraut.lod_thresholds` (Kraut distances converted via the tree's
  bounding radius at reference fov 0.7854) with a synthesized ramp fallback.
  Tier selection (`MeshRender::PickLodForCoverage`): threshold(i) is the
  coverage below which tier i activates (tier 0 owns coverage >= t1); the pick is
  dithered per object -- `MeshRender::ComputeLodJitter` applies a stable
  position-hashed coverage jitter (±15%) so neighbors swap tiers at
  different distances instead of popping in lockstep. `FURY_LOD_JITTER=0`
  disables; `FURY_LOD_JITTER=0.4` widens the band for a visible speckle
  demo. Debug view: `LOD_DEBUG_COLORS` switch tints per tier and works
  headless (`docs/BUILD_FLAGS.md`).
- **Billboard shader** (`BILLBOARD` define in `GBuffer.glsl`): the VS builds
  a cylindrical (upright) camera-facing quad from the node/instance position
  and picks the atlas cell by camera azimuth around the trunk axis
  (`u_billboard_atlas` = cols/rows from the material). Cell *k* of the atlas
  shows the tree from azimuth `((k+0.5)/cols - 0.5) * 2pi` around +Z — the
  same formula the KrautPreview `--atlas` baker uses. The normal atlas stores
  signed object-space normals encoded as RGB = N * 0.5 + 0.5. Its texture is
  linear data (`srgb: false`). The shader applies the instance inverse-transpose
  and view rotation, so lighting follows tree rotation and scene lights.
  `u_billboard_pivot` aligns the quad with the atlas's bounding-box center.
  The terminal tier activates below at most 0.12 screen coverage.
- **Foliage shading**: `Material::m_TwoSided` disables culling, and MASK
  applies the 0.4 alpha cutoff. `Tools/KrautCLI/FoliageShading.h` supplies
  the crown-dome normals for both glb meshes and atlas baking. The glb
  declares `extras.kraut.canopy_normals = "crown_dome_v1"`; the importer
  preserves these authored normals. Older unmarked exports receive the
  dome model on import. Billboard leaves are expanded into stable radial
  cards for both static export and baking. Atlas views use the same raster
  texture sources as the meshes, bake without directional lighting, and
  dilate RGB into transparent borders without changing alpha.
- **Billboard fallback**: assets without a normal atlas use
  `normalize(mix(cameraForward, up, u_billboard_up_bias))`. An explicit
  `extras.kraut.billboard.up_bias` overrides the default; otherwise the
  importer derives it from mean canopy normal Y. Grass supplies 1.0.
  The conversion is `m / (m + sqrt(1 - m*m))`, with m clamped to [0,1].
- **Vegetation pre-Z**: flagged mesh materials write depth, then shade at
  EQUAL. Billboards draw once in the ordinary phase even when pre-Z is
  active. A missing matching alpha/wind/skinning depth variant disables
  the pre-phase for the frame. Shader changes rebind mesh attributes;
  cached and uncached draws use the same instancing variant selection.
- **Wind**: `Material::m_WindEnabled` selects the WIND vertex-shader variant
  (gbuffer + depth, so shadows sway in sync). Displacement is pre-projection
  from `vertex_color` weights (Kraut packs R=branch sway, G=leaf flutter,
  B=phase, A=color variation) with `u_time` (engine clock, `Engine.GetTime`)
  + `u_wind_params` = scene `renderSettings.wind_params`
  (dirX, dirZ, strength, frequency). Meshes without a Colors channel are
  rigid under wind (zero weights).
- **Vertex colors**: `Mesh::Colors` vec4 channel (`vertex_color` attribute),
  populated from glTF `COLOR_0` (vec3/vec4, float or normalized ints).

## Grass (offline baker, no Kraut)

Ground cover is too light for the Kraut pipeline: `tools/gen_grass_assets.py`
bakes `examples/Resource/Trees/Grass/` directly — blade-silhouette alpha
textures (PIL), a hand-rolled `GrassClump.glb` speaking the kraut contract
(`_LOD0` 3 crossed cards, `_LOD1` 2 cards, `_Billboard` 1 quad with a 1x1
atlas, `COLOR_0` wind weights, `extras.kraut` thresholds), imported via the
stock `fury kraut import` into `GrassClump.bin`. Grass vertex normals are
all +Y so clumps light like the terrain; billboard tier reuses the tree
billboard shader path.

Island layer: `tools/gen_island_grass.py` computes splat-aware placement
(dominant grass splat channel + slope/height gates) in **meadow patches**
(habitat seeds -> gaussian blobs; uniform density can't read dense at island
scale) -> `Projects/ocean/grass_points.lua`;
`Projects/ocean/populate_island_grass.lua` merges the clump fragment into a scene
copy and bakes a `GrassField` InstancedMeshRender (45k instances, no shadow
casting). Wind waves roll across the field via the position-phase term in
the WIND variant (`dot(worldPos.xz, windDir) * 0.00785`, ~800 m wavelength).

Scale notes: instance world matrices + AABBs are cached (rebuilt on edit or
owner move), so the per-frame cost is a frustum test + coverage eval per
instance: ocean_island with 45k grass + 2.3k trees runs ~3.6 ms avg
(278 fps, 123 draw calls). Beyond ~50k VISIBLE instances the remaining
frustum/coverage pass becomes the cost center -- per-cell cluster culling
is the documented next step.
Headless harnesses: `tests/lua/scenes/grass_field_perf.lua` (FURY_GRASS_N),
`tests/lua/scenes/grass_island_check.lua` (FURY_SCENE/FURY_CAM/FURY_TOD_HOURS).

**ToD in scenes with SkyAtmosphere**: when a sky component has
`sun_from_tod` on (ocean_island does), it overwrites the bound sun node's
rotation every frame -- drive time-of-day via `SkyAtmosphere:SetTimeHours()`
(and `SetAutoAdvance(false)` to pin), not by rotating the light node.

## Instanced static meshes (ISM/HISM)

`InstancedMeshRender` (component, serialized): one shared mesh (with LOD
chain) + per-submesh materials + a list of instance TRS transforms.
Per frame per pass: per-instance frustum culling against cached world AABBs
(the component holds one coarse octree entry covering all instances), then
HISM mode buckets visible instances per LOD tier using `MeshRender`'s
screen-coverage rule per instance (ISM mode draws all at one tier picked
from the aggregate bounds), then one `glDrawElementsInstanced` per
(component, tier, submesh). Shadow passes draw instanced casters at the
deepest non-billboard tier (billboard-terminated chains only -- plain LOD
chains cast from LOD 0, their coarse tiers self-shadow badly), honoring the
per-component `cast_shadows` flag. `cull_distance` (cm, 0 = no cap) drops
instances entirely past a draw distance -- small clutter like grass uses
~80 m instead of paying for far billboards; the cap edge is jittered per
instance (same position hash as the LOD dither) so it reads as a staggered
band, not a hard line.

Instance matrices stream via:
- **SSBO** (GL 4.3+, preferred): shader storage buffer read by
  `gl_InstanceID` (`INSTANCE_SSBO` define). *Untested at writing — see the
  status note above.*
- **Divisor VBO** (GL 3.3/4.1 fallback, e.g. macOS): matrices as four vec4
  attributes (`instance_row0..3`) with `glVertexAttribDivisor(1)`; the
  divisor state is torn down after each draw so shared mesh VAOs never leak
  instance state into non-instanced draws.

The editor inspector exposes mesh/material slots, the ISM/HISM toggle,
instance add/remove/duplicate, and a seeded scatter-over-terrain helper
(std::mt19937, cross-platform deterministic).

![2500 instanced cubes, 2 gbuffer draws](../screenshots/instanced_field_2500.png)

## Validation (headless, `fury` + fixed-frame screenshots)


Run the contract and visual regression checks after changing tree shading:

```sh
ctest --test-dir build -R vegetation_math --output-on-failure
python3 tests/foliage_lod_assets.py
python3 tools/gen_tree_assets.py --check
python3 tests/foliage_lod_render.py --backend metal
python3 tests/foliage_lod_render.py --backend vulkan
```

The visual checks compare sun/rotation setups and a distant mip level for both sample trees,
plain and instanced pre-Z/cache parity, partial depth-shader sets, SSBO and
vertex-attribute instance streams, and every natural LOD band. Captures and
metrics go under `test-artifacts/foliage-lod-<backend>/`. Asset checks reject
collapsed foliage, transparent source textures, mismatched atlas silhouettes,
nonlinear normal textures, absolute sample paths, and import-time rebending.
`gen_tree_assets.py` decodes TGA RLE across scanlines and fails on empty
foliage; it does not accept Pillow's permissive truncated-image fallback.

- `tests/lua/scenes/instancing_smoke.lua` — 2500 instanced cubes: instance count
  asserted, draw calls flat (< 200; per-instance submission would be >2500).
- `tests/lua/kraut_import_roundtrip.lua` — import a kraut glb, save, reload:
  chain length / thresholds / billboard flag / vertex colors / material
  flags all survive.
- `tests/lua/pipeline_compile_check.lua` — both pipeline JSONs load (every
  new shader variant compiles; SSBO variants skip cleanly on GL < 4.3).
- `tests/lua/scenes/tree_preview.lua` — LOD tiers transition with distance
  (prints the active tier per node), billboard tier renders the atlas.
- `tests/lua/scenes/vegetation_perf.lua` — ocean_island with ~2300 trees:
  avg frame ms + flat draw-call assert.
- `tests/lua/scenes/lod_transition_check.lua` — dithered LOD transitions: a line
  of same-distance instances splits across tier buckets with jitter on vs
  flipping in lockstep with `FURY_LOD_JITTER=0` (batch readout via Lua
  `GetBatchCount`/`GetBatchInfo`); debug-tint speckle:
  `screenshots/lod_jitter_speckle.png`.
- `tests/lua/scenes/grass_field_perf.lua` — 3k/12k/48k grass instances:
  ~0.7 us/instance/frame, draw calls flat (6) at every scale.
- `tests/lua/scenes/grass_island_check.lua` — island grass layer: ~160 fps avg,
  ~121 draw calls with 80k grass + 2.3k trees; `FURY_TOD_HOURS` for
  time-of-day variants. Shots: `screenshots/island_grass_{vista,close}
  [_tod2].png`.
- `tests/lua/scenes/billboard_pop_check.lua` — dolly across the mesh->billboard
  boundary while `--screenshot-series "out.png,N,interval"` captures a
  contact-sheet atlas (the CLI's temporal-capture mode; see docs/CLI.md) —
  per-cell ink/luminance continuity verifies the swap doesn't pop.

| LOD 0 (8 m) | Billboard tier (60 m) | Wind t=0 vs t=2 |
|---|---|---|
| ![](../screenshots/kraut_tree_lod0.png) | ![](../screenshots/kraut_tree_billboard.png) | ![t0](../screenshots/kraut_wind_t0.png) ![t2](../screenshots/kraut_wind_t2.png) |

Billboard atlas (8 cylindrical views of PalmTree2, baked by
`KrautPreview --atlas`):

![Billboard atlas](../screenshots/kraut_billboard_atlas.png)

![Island grass layer](../screenshots/island_grass_vista.png)

## Known limits / follow-ups

- The SSBO instance path is preferred but untested on real GL 4.3+
  hardware (macOS dev machines only exercise the divisor fallback);
  validate with a screenshot A/B on Windows before trusting it there.
- Cylindrical atlases have eight azimuth views and one elevation. Camera
  elevation, view-cell switches, and silhouette simplification can still
  produce visible transitions. Normal atlases do not encode canopy depth
  or replace missing branches.
- PalmTree2's deep mesh tiers omit the lower canopy: fronds start around
  2.57 m in LOD2 and 2.67 m in LOD3, versus near ground in LOD0/LOD1.
  This generator/asset issue remains out of scope for the shading fix.
- Kraut impostor modes (octahedral-ish multi-view) are richer than the
  cylindrical billboard atlas but a bigger shader lift; not in v1.
- Scatter placement is script/editor-helper driven; an editor paint tool
  is a follow-up.
- Wind is a sway approximation (no branch-hierarchy propagation); the
  weights are Kraut branch-structure derived and tuned in the exporter's
  per-type sway table.
