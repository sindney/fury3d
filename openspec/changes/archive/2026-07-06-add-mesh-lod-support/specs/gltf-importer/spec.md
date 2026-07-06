## MODIFIED Requirements

### Requirement: The importer SHALL accept glTF models that use the `MSFT_lod` extension and translate each LOD chain into a `LodGroup`

`HasUnsupportedFeatures` MUST NOT reject the `MSFT_lod` extension. When a glTF node carries an `extensions.MSFT_lod` object with a `lod` array, the importer SHALL read each entry's `mesh` (int) and `screen coverage` (float, 0..1) and emit a `LodGroup` keyed to that node's `MeshRender` with the same number of entries and the same threshold order (highest-detail first). The `LodGroup`'s `Mesh::Ptr`s SHALL be the engine `Mesh` records the importer already created for the referenced glTF meshes. The first entry's threshold is taken from the source if present, else defaulted to `1.0`; the last entry's threshold is defaulted to `0.0` if missing.

#### Scenario: MSFT_lod is no longer an unsupported extension
- **WHEN** a glTF file declares `"extensionsRequired": ["MSFT_lod"]` and at least one node has an `extensions.MSFT_lod` object
- **THEN** the importer does NOT reject the file
- **AND** the importer logs `FURYI` listing the recognized extension

#### Scenario: MSFT_lod chain populates a LodGroup
- **WHEN** a glTF node has `extensions.MSFT_lod.lod = [{"mesh": 0, "screen coverage": 1.0}, {"mesh": 1, "screen coverage": 0.5}, {"mesh": 2, "screen coverage": 0.1}]`
- **THEN** the emitted `MeshRender` for that node has a 3-entry `LodGroup` with thresholds `{1.0, 0.5, 0.1}` and meshes referencing the engine `Mesh` records created from glTF meshes `0`, `1`, `2`

#### Scenario: Missing screen-coverage values fall back to defaults
- **WHEN** a glTF node has an MSFT_lod entry with no `screen coverage` field
- **THEN** that entry's threshold is `1.0` for the first LOD and `0.0` for the last LOD; intermediate entries are interpolated linearly
- **AND** a `FURYW` warning is logged naming the node and the synthesized threshold

## ADDED Requirements

### Requirement: The importer SHALL build a `LodGroup` from glTF meshes that share a name suffix of the form `_LOD<N>`

When the importer's mesh pass produces a set of engine `Mesh` records whose names match the pattern `<base>_LOD<N>` (e.g. `Tree_LOD0`, `Tree_LOD1`, `Tree_LOD2`), the importer SHALL group them into a `LodGroup` ordered by `N` ascending. The thresholds SHALL default to a `1.0 → 0.0` linear ramp with one entry per `LOD<N>`. The grouped meshes SHALL still be registered as `Mesh` entities (callers may reference them by name), AND a `LodGroup` referencing them SHALL be assigned to every `MeshRender` that originally bound the highest-detail (`LOD0`) mesh. Groups are only formed when at least 2 matching `<base>` names exist; a single `Foo_LOD0` mesh is kept as a plain `Mesh` and logs `FURYD`.

#### Scenario: Group by name suffix
- **WHEN** a glTF model declares meshes named `Tree_LOD0`, `Tree_LOD1`, `Tree_LOD2`
- **THEN** the importer emits a 3-entry `LodGroup` with thresholds `{1.0, 0.5, 0.0}` and meshes `[Tree_LOD0, Tree_LOD1, Tree_LOD2]`
- **AND** the `MeshRender` for any glTF node that bound `Tree_LOD0` is given that `LodGroup`
- **AND** a `FURYI` log line names the group and its base mesh name

#### Scenario: Single _LOD<N> mesh is not grouped
- **WHEN** a glTF model declares only `Foo_LOD0` (no `Foo_LOD1`)
- **THEN** no `LodGroup` is formed
- **AND** `Foo_LOD0` is a plain `Mesh` bound to its `MeshRender` as today
- **AND** a `FURYD` log notes the lone `_LOD<n>` mesh

#### Scenario: Non-contiguous LOD indices are accepted
- **WHEN** a glTF model declares `Tree_LOD0` and `Tree_LOD2` (no `Tree_LOD1`)
- **THEN** the importer emits a 2-entry `LodGroup` with thresholds `{1.0, 0.0}` and meshes `[Tree_LOD0, Tree_LOD2]`
- **AND** a `FURYW` warns about the gap
