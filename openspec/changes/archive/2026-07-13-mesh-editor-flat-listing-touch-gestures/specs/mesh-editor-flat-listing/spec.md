## ADDED Requirements

### Requirement: The Mesh editor SHALL list LOD thresholds as an inline table

The Mesh editor's metadata panel SHALL render LOD thresholds as a flat (non-collapsible) inline table whenever the bound mesh has more than one LOD (`mesh->GetLodCount() > 1`). The table SHALL have one row per interior LOD (LOD 1 .. LOD N−1, where LOD 0 and LOD N are fixed), with the following columns: index label (`"LOD i"`), current threshold value (read-only text, formatted `%.3f`), and a `SliderFloat` widget clamped to `[0.0, 1.0]`. The deepest LOD row SHALL NOT expose a slider (its threshold is locked at `0.0`); it SHALL display a read-only `"0.000"` value instead. No `TreeNode`, `CollapsingHeader`, `TreeNodeEx`, or any other collapsible widget SHALL wrap the table.

#### Scenario: Single-LOD mesh skips the table

- **WHEN** the Mesh editor is open for a mesh with `GetLodCount() == 1`
- **THEN** the LOD thresholds table is not rendered

#### Scenario: Multi-LOD mesh renders one row per interior threshold

- **WHEN** the Mesh editor is open for a mesh with 4 LODs (`GetLodCount() == 4`) and thresholds `{1.0, 0.5, 0.25, 0.0}`
- **THEN** the table renders exactly 3 rows (for LODs 1, 2, 3)
- **AND** each interior row shows the current threshold as read-only text plus a `SliderFloat` clamped to `[0.0, 1.0]`
- **AND** the LOD 3 row has no slider (deepest LOD is locked at `0.0`)

#### Scenario: Editing a threshold updates the mesh

- **WHEN** the user drags the slider on the LOD 2 row to `0.30`
- **THEN** `mesh->GetLodThreshold(2)` reads `0.30` on the next frame

### Requirement: The Mesh editor SHALL list submeshes as an inline table

The Mesh editor's metadata panel SHALL render submeshes as a flat (non-collapsible) inline table. The table SHALL have a header row with columns `Index`, `Name`, `Indices`, `Triangles`, and one data row per submesh. The `Name` column SHALL show the submesh's display name (or `<unnamed>` when empty). The `Indices` column SHALL show `sm->Indices.Data.size()`. The `Triangles` column SHALL show `sm->Indices.Data.size() / 3`. No `TreeNode`, `CollapsingHeader`, `TreeNodeEx`, or any other collapsible widget SHALL wrap the table or any row. The total counts row above the table (`Vertices`, `Indices`, `Triangles`, `Submeshes`) SHALL remain unchanged.

#### Scenario: Mesh with three submeshes renders three data rows

- **WHEN** the Mesh editor is open for a mesh whose `GetSubMeshCount() == 3` with names `["Body", "Wheel_L", ""]` and indices `[900, 600, 300]`
- **THEN** the table renders 3 data rows
- **AND** row 1 shows `0`, `Body`, `900`, `300`
- **AND** row 2 shows `1`, `Wheel_L`, `600`, `200`
- **AND** row 3 shows `2`, `<unnamed>`, `300`, `100`
- **AND** no row is collapsible

#### Scenario: Mesh with no submeshes omits the table

- **WHEN** the Mesh editor is open for a mesh with `GetSubMeshCount() == 0`
- **THEN** the submeshes table is not rendered
- **AND** the total-counts row above shows `Submeshes: 0`