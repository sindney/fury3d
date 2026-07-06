## MODIFIED Requirements

### Requirement: The mesh editor window SHALL expose an LOD dropdown that lets the user preview each level of the bound mesh

The mesh editor window's metadata block SHALL include a dropdown listing every `LOD <index>` of the mesh's `LodGroup` (e.g. `LOD 0`, `LOD 1`, `LOD 2`). When the user selects a dropdown entry, the 3D preview pane SHALL render the corresponding `Mesh::Ptr` from the `LodGroup` instead of the highest-detail mesh. The dropdown is preview-only; the selection is held on the per-window state and is NOT written back to the `Mesh` or the `LodGroup`. When the mesh has no `LodGroup`, the dropdown is hidden.

#### Scenario: Dropdown lists every LOD
- **WHEN** the user opens the mesh editor for a mesh whose `LodGroup` has 3 entries
- **THEN** the dropdown lists `LOD 0`, `LOD 1`, `LOD 2`
- **AND** the default selection is `LOD 0`

#### Scenario: Selecting an LOD updates the preview
- **WHEN** the user picks `LOD 1` in the dropdown
- **THEN** the 3D preview pane renders the `LodGroup::GetMesh(1)` mesh
- **AND** the metadata block's per-LOD stats (vertex/index counts) update to reflect the picked LOD

#### Scenario: No dropdown for single-mesh assets
- **WHEN** the user opens the mesh editor for a mesh with no `LodGroup`
- **THEN** no LOD dropdown is rendered
- **AND** the preview pane renders the single `Mesh` as today

#### Scenario: Preview selection is window-local
- **WHEN** the user picks `LOD 2` in the mesh window, then closes and re-opens the window
- **THEN** the dropdown is reset to `LOD 0`
- **AND** the `Mesh`'s `LodGroup` is unchanged
