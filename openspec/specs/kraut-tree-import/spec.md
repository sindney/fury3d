# kraut-tree-import

## Purpose

The Kraut -> fury bridge: a glTF-binary export contract (per-LOD meshes, billboard quad + atlas, vertex-color wind weights, PBR materials, kraut metadata in glTF `extras`) imported through the existing GltfImporter with a kraut-aware postprocess that builds `Mesh` LOD chains, billboard terminal tiers, and foliage materials. Plus the baked sample assets shipped under `examples/Resource/Trees/`.

## Requirements

### Requirement: The bridge format SHALL be glb with kraut extras

Kraut-exported trees SHALL be a single `.glb` containing: one mesh node per LOD tier named `<tree>_LOD<n>` (LOD 0 = full detail), a billboard quad node named `<tree>_Billboard` with its atlas texture, `COLOR_0` vertex colors carrying wind weights, PBR materials (baseColor with MASK alpha where cutout is needed, normal, metallic-roughness) with textures, and `asset.extras.kraut` carrying at minimum `lod_thresholds` (screen-coverage floats, non-increasing), `billboard` (atlas grid dimensions + facing mode), and generation provenance (`seed`, `descriptor`).

#### Scenario: Export contains the full LOD chain and metadata

- **WHEN** a tree with 4 LOD tiers and a billboard is exported via the kraut glb path
- **THEN** the glb contains nodes `<tree>_LOD0..3` and `<tree>_Billboard`
- **AND** `asset.extras.kraut.lod_thresholds` is present and non-increasing
- **AND** `asset.extras.kraut.billboard` names the atlas grid dimensions

### Requirement: Import SHALL reuse GltfImporter and build the LOD chain from kraut extras

Importing a kraut glb SHALL route through the existing `GltfImporter` (which auto-groups `<base>_LOD<n>` nodes into a LodGroup). A kraut postprocess step SHALL then: replace any synthesized thresholds with `extras.kraut.lod_thresholds` when present, attach the billboard quad as the terminal LOD tier with its billboard flag set, and set foliage material properties (two-sided, alpha cutoff) from the exported materials. The result SHALL be a `Mesh` with `GetLodCount()` equal to the exported tier count plus the billboard tier.

#### Scenario: Imported tree has the exported chain

- **WHEN** a kraut glb with LOD thresholds `{0.4, 0.15, 0.05}` and a billboard tier is imported
- **THEN** the resulting `Mesh` LOD thresholds are exactly `{0.4, 0.15, 0.05}` for tiers 1-3
- **AND** the terminal tier is the billboard quad with its billboard flag set

#### Scenario: Missing extras fall back to synthesized thresholds

- **WHEN** a glb with `_LOD<n>` nodes but no kraut extras is imported
- **THEN** the importer uses the existing synthesized linear thresholds and sets no billboard flag

### Requirement: Meshes SHALL carry a vertex-color channel populated from COLOR_0

`Mesh` SHALL own an optional vec4 `Colors` vertex buffer exposed to shaders as the `vertex_color` attribute. `GltfImporter` SHALL read `COLOR_0` (vec3 or vec4, normalized integer or float) into this channel. Meshes without `COLOR_0` SHALL have an empty channel and shaders SHALL behave as before.

#### Scenario: Wind weights survive import

- **WHEN** a kraut glb whose LOD0 mesh has `COLOR_0` is imported
- **THEN** the resulting `Mesh` has a `Colors` buffer with one vec4 per vertex matching the export
- **AND** the gbuffer shader binds `vertex_color` for that mesh

### Requirement: Sample tree assets SHALL ship as engine resources with a generator script

`tools/gen_tree_assets.py` SHALL drive the toolchain (checked-in `.tree` descriptors, fixed seeds, KrautCLI export, `fury kraut import` validation) to bake committed assets under `examples/Resource/Trees/` (glb + textures + a `trees.json` index), so the samples work without building Kraut. At least two tree types (one broadleaf, one palm/conifer) SHALL be provided. `ocean_island.bin` SHALL be updated to include instanced trees placed on the island.

#### Scenario: Fresh checkout renders the sample trees

- **WHEN** the engine is built from a fresh checkout without the Kraut submodule
- **AND** `ocean_island.bin` is loaded
- **THEN** the committed sample trees render with LODs, foliage shading, and billboards

#### Scenario: Regenerating samples is one command

- **WHEN** the user runs `python3 tools/gen_tree_assets.py` with the Kraut tools built
- **THEN** the assets under `examples/Resource/Trees/` are regenerated deterministically from the checked-in descriptors
