## MODIFIED Requirements

### Requirement: The mesh editor SHALL expose an LOD dropdown that lets the user preview each level of the bound mesh

The mesh editor window's metadata block SHALL include a dropdown listing every `LOD <index>` of the mesh's LOD chain (e.g. `LOD 0`, `LOD 1`, `LOD 2`), plus an `Auto` entry as the default selection. When the user selects `LOD i`, the 3D preview pane SHALL render the corresponding `Mesh::Ptr` from the chain (`mesh->GetLodMesh(i)`) instead of the runtime-driven LOD. When the user selects `Auto`, the preview pane SHALL restore runtime-driven LOD selection (i.e. the LOD the runtime would auto-pick for the current camera distance). The dropdown is preview-only; the selection is held on the per-window state and is NOT written back to the `Mesh`. The selection is restored when the window is reopened. When the mesh has no LOD chain (its `Mesh::GetLodCount() <= 1`), the dropdown is hidden.

#### Scenario: Dropdown lists every LOD plus Auto

- **WHEN** the user opens the mesh editor for a mesh whose LOD chain has 3 entries
- **THEN** the dropdown lists `Auto`, `LOD 0`, `LOD 1`, `LOD 2`
- **AND** the default selection is `Auto`

#### Scenario: Selecting an LOD forces the preview to render that LOD

- **WHEN** the user picks `LOD 1` in the dropdown
- **THEN** the 3D preview pane renders `mesh->GetLodMesh(1)`
- **AND** the metadata block's per-LOD stats (vertex / index counts) update to reflect the picked LOD
- **AND** moving the orbit camera closer or further does NOT switch the preview to a different LOD

#### Scenario: Selecting Auto restores runtime-driven LOD

- **WHEN** the user picks `Auto` in the dropdown
- **THEN** the preview pane selects the active LOD from the camera distance (the existing runtime-driven behavior)
- **AND** moving the orbit camera closer / further swaps the rendered LOD

#### Scenario: No dropdown for single-LOD assets

- **WHEN** the user opens the mesh editor for a mesh with no LOD chain
- **THEN** no LOD dropdown is rendered
- **AND** the preview pane renders the single `Mesh` as today

#### Scenario: Preview selection persists across reopen

- **WHEN** the user picks `LOD 2` in the mesh window, then closes and re-opens the window
- **THEN** the dropdown is restored to `LOD 2`
- **AND** the preview pane renders `mesh->GetLodMesh(2)` immediately on reopen
- **AND** the `Mesh`'s LOD chain is unchanged

### Requirement: The mesh editor SHALL render LOD thresholds as an inline (non-collapsible) table

The mesh editor window SHALL render LOD thresholds as a flat (non-collapsible) inline table whenever the bound mesh has more than one LOD (`mesh->GetLodCount() > 1`). The table SHALL have one row per interior LOD (LOD 1 .. LOD N−1). Each row SHALL display the index label (`"LOD i"`) and an `InputFloat` widget clamped to `[0.0, 1.0]` (the widget shows its own value). The deepest LOD row (LOD N) SHALL NOT expose a slider (its threshold is locked at `0.0`); it SHALL display a read-only `"0.000"` value instead. No `TreeNode`, `CollapsingHeader`, `TreeNodeEx`, or any other collapsible widget SHALL wrap the table. Editing a slider SHALL write the updated value into the mesh's LOD chain via `Mesh::SetLodMeshes`. The mesh's save path SHALL persist the new threshold.

#### Scenario: Threshold slider updates the chain

- **WHEN** the user opens the mesh editor for a mesh with a 4-entry chain
- **AND** types `0.250` into the LOD 2 input
- **THEN** the mesh's `GetLodThreshold(2)` returns `0.250`
- **AND** the change persists to disk when the scene is saved

#### Scenario: Single-LOD mesh skips the threshold table

- **WHEN** the mesh editor is open for a mesh with `GetLodCount() == 1`
- **THEN** the LOD thresholds table is not rendered

#### Scenario: Deepest LOD row is locked

- **WHEN** the mesh editor renders the LOD thresholds table for a 4-LOD mesh
- **THEN** the LOD 3 row has no slider
- **AND** the LOD 3 row's threshold value reads `0.000`

### Requirement: The Mesh editor SHALL list submeshes as an inline table

The Mesh editor's metadata panel SHALL render submeshes as a flat (non-collapsible) inline table. The table SHALL have a header row with columns `Index`, `Name`, `Indices`, `Triangles`, and one data row per submesh. The `Name` column SHALL show the submesh's placeholder label `Submesh <i>` (the engine has no submesh name field). The `Indices` column SHALL show `sm->Indices.Data.size()`. The `Triangles` column SHALL show `sm->Indices.Data.size() / 3`. No `TreeNode`, `CollapsingHeader`, `TreeNodeEx`, or any other collapsible widget SHALL wrap the table or any row. The total counts row above the table (`Vertices`, `Indices`, `Triangles`, `Submeshes`) SHALL remain unchanged.

#### Scenario: Mesh with three submeshes renders three data rows

- **WHEN** the Mesh editor is open for a mesh whose `GetSubMeshCount() == 3` with names `["Body", "Wheel_L", ""]` and indices `[900, 600, 300]`
- **THEN** the table renders 3 data rows
- **AND** row 1 shows `0`, `Submesh 0`, `900`, `300`
- **AND** row 2 shows `1`, `Submesh 1`, `600`, `200`
- **AND** row 3 shows `2`, `Submesh 2`, `300`, `100`
- **AND** no row is collapsible

#### Scenario: Mesh with no submeshes omits the table

- **WHEN** the Mesh editor is open for a mesh with `GetSubMeshCount() == 0`
- **THEN** the submeshes table is not rendered
- **AND** the total-counts row above shows `Submeshes: 0`
