## ADDED Requirements

### Requirement: The LOD dropdown SHALL preview the selected LOD

The Mesh editor's LOD dropdown SHALL preview the user-selected LOD in the preview pane. When the dropdown selects a specific LOD index `i` (where `i ∈ [0, GetLodCount())`), the preview pane SHALL render `mesh->GetLodMesh(i)` for the duration of the selection. When the dropdown selects "Auto" (the default), the preview pane SHALL restore runtime-driven LOD selection (i.e. the preview draws the LOD the runtime would auto-pick for the current camera distance). The selection is stored on the per-popup state (keyed by `popup_id`) and is restored when the editor window is reopened.

#### Scenario: Selecting LOD 2 forces the preview to render LOD 2

- **WHEN** the LOD dropdown is set to `"LOD 2"`
- **THEN** the preview pane renders `mesh->GetLodMesh(2)`
- **AND** changing the orbit camera distance does not switch the preview to a different LOD

#### Scenario: Selecting "Auto" restores runtime-driven LOD

- **WHEN** the LOD dropdown is set to `"Auto"`
- **THEN** the preview pane selects the active LOD from the camera distance (the existing runtime-driven behavior)
- **AND** moving the orbit camera closer / further swaps the rendered LOD

#### Scenario: Selection persists across reopen

- **WHEN** the user selects `"LOD 1"`, closes the Mesh editor, and double-clicks the mesh tile again to reopen it
- **THEN** the LOD dropdown is restored to `"LOD 1"`
- **AND** the preview pane renders `mesh->GetLodMesh(1)` immediately on reopen

#### Scenario: Single-LOD mesh hides the dropdown

- **WHEN** the bound mesh has `GetLodCount() == 1`
- **THEN** the LOD dropdown is not rendered (the preview always renders the single mesh)

### Requirement: The LOD preview override SHALL NOT affect the scene's runtime LOD selection

The Mesh editor's per-popup LOD preview override SHALL only affect the preview pane within that editor window. It SHALL NOT mutate `mesh->m_ActiveLod` (which is owned by `MeshRender` per visible instance) and SHALL NOT change the LOD color overlay used by the `LOD_DEBUG_COLORS` pipeline switch in the main scene viewport.

#### Scenario: Mesh editor LOD override does not leak into the main scene

- **WHEN** the Mesh editor LOD dropdown is set to `"LOD 2"`
- **AND** the same mesh is also visible in the main scene viewport
- **THEN** the main scene's `MeshRender::GetActiveLod()` continues to follow the runtime's coverage-based selection