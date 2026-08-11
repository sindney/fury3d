# heightmap-terrain

## Purpose

Unity-Terrain-inspired heightmap terrain: a `Terrain` component that builds
chunked LOD meshes from a 16-bit heightmap asset at load, shades them with a
4-layer slope/height splat in the gbuffer pass, answers runtime height
queries, and pairs with `BodySetup`'s heightfield shape for collision. A
stdlib-only Python generator produces the startup resources (heightmap,
ground-layer textures, splatmap, plus the sky feature's noise/moon textures).

## ADDED Requirements

### Requirement: The heightmap asset SHALL be a 16-bit raw file with a JSON sidecar

A heightmap asset SHALL consist of `<name>.r16` (little-endian uint16
heights, row-major, resolution `N x N` with `N = 2^k + 1`) and `<name>.json`
holding `resolution`, `worldSizeX`, `worldSizeZ`, `heightScale` (all world
units in cm). Paths SHALL resolve against the owning scene's working
directory, matching the existing texture-path convention.

#### Scenario: Load a generated heightmap

- **WHEN** a `Terrain` references `Terrain/height.r16` in a scene whose working dir contains `Terrain/height.r16` + `Terrain/height.json`
- **THEN** heights load with the advertised resolution and world extents
- **AND** a missing or malformed sidecar fails the load with a logged error (no crash, component stays empty)

### Requirement: Terrain SHALL be a serializable component that builds chunked LOD meshes at load

`Terrain` SHALL be a `Component` registered in `SceneNode::ComponentRegistry`
(name `Terrain`) holding: heightmap path, splatmap path, four layer entries
(name, albedo texture path, tiling size in cm), chunk count per side, and LOD
count. On load (and on parameter change in the editor) it SHALL build a
`chunkCount x chunkCount` grid of chunk meshes, each chunk with a geomipmap
LOD chain (LOD i sampling every `2^i` grid points) plus crack-hiding skirts,
and SHALL hang them off internal child nodes with `MeshRender` components so
culling, LOD selection, shadows, and depth passes work through existing
machinery. Chunk meshes SHALL be runtime-built only: they SHALL be flagged so
scene serialization skips them (rebuilt on load), keeping scene files small.

#### Scenario: Terrain appears from a saved scene

- **WHEN** a scene with a configured `Terrain` is saved, reloaded, and rendered
- **THEN** the terrain surface is visible, lit, shadowed, and octree-culled like ordinary meshes

#### Scenario: Saved scene contains no chunk meshes

- **WHEN** a scene with a built `Terrain` is saved
- **THEN** the serialized scene contains the `Terrain` component and its parameters but none of the generated chunk meshes or their nodes

#### Scenario: LOD count respected

- **WHEN** a terrain is built with LOD count 3
- **THEN** each chunk mesh has a 3-entry LOD chain and distant chunks render lower LODs

### Requirement: Terrain SHALL shade with a 4-layer splat in the deferred pipeline

The terrain gbuffer shader SHALL blend four albedo layers (grass, rock, mud,
snow by convention) by the RGBA splatmap weights, each tiled by its layer
tiling size, deriving per-layer roughness from the albedo alpha channel, and
SHALL write view-space normal (from the heightmap gradient, smoothed) +
roughness and albedo + metallic=0 into the gbuffer like any opaque PBR
surface. All four layer samplers plus the splatmap SHALL be bound every draw
(dummies for missing layers).

#### Scenario: Slope and height select the layer

- **WHEN** the generator writes splat weights by the slope/height rule and the terrain renders
- **THEN** steep slopes show rock, low flats show grass, the waterline band shows mud, and high altitudes show snow, with soft transitions

### Requirement: Terrain SHALL answer runtime height queries

`Terrain::GetHeight(worldX, worldZ)` SHALL return the bilinearly interpolated
terrain height in world cm (accounting for the component's world transform),
and SHALL be exposed to Lua. Queries outside the terrain bounds SHALL clamp
to the edge height.

#### Scenario: Spawn placement sits on the surface

- **WHEN** a script places a node at `(x, terrain:GetHeight(x, z), z)`
- **THEN** the node rests on the rendered terrain surface at that point (within one heightmap texel of error)

#### Scenario: Out-of-bounds query clamps

- **WHEN** a script queries beyond the terrain edge
- **THEN** the returned height equals the nearest edge sample

### Requirement: A Python generator SHALL produce all terrain and sky startup resources

`tools/gen_terrain_assets.py` SHALL be stdlib-only (no third-party Python
packages) and SHALL generate, from a deterministic seed: the heightmap
(`.r16` + `.json` + preview PNG) from fBm Perlin noise with an optional
flatten-disc mask around a chosen center; the splatmap PNG from slope/height
rules (slope above threshold -> rock, below waterline band -> mud, above
snowline -> snow, else grass, with feathered transitions); four tileable
ground albedo textures (grass/rock/mud/snow, noise-synthesized, roughness
variation in alpha); the cloud coverage+detail noise RGBA texture (tileable);
and the moon albedo texture. All outputs SHALL land under a caller-chosen
directory (default `examples/Projects/outdoor/Terrain/`).

#### Scenario: Re-run reproduces identical assets

- **WHEN** the generator runs twice with the same seed and arguments
- **THEN** every output file is byte-identical

#### Scenario: Splatmap rule verified numerically

- **WHEN** the generator runs with a known heightmap
- **THEN** a steep-slope texel has rock as its dominant weight and a flat lowland texel has grass (assertable in the generator's self-check output)

### Requirement: Terrain SHALL have an editor inspector section

`EditorNodeProperties` SHALL render a `Terrain` section (registered in
`ComponentRenderTable` and the Add-Component menu) exposing the heightmap and
splatmap paths, the four layer entries, chunk/LOD counts, a rebuild action,
and a read-only height probe, marking the scene dirty on edit.

#### Scenario: Edit layer tiling and rebuild

- **WHEN** the user changes a layer's tiling and triggers rebuild
- **THEN** chunk meshes regenerate and the viewport reflects the new tiling without restarting the editor

## MODIFIED Requirements

(none)

## REMOVED Requirements

(none)
