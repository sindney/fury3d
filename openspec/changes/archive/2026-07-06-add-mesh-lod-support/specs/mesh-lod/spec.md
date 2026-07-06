## ADDED Requirements

### Requirement: The engine SHALL define a `LodGroup` type that holds an ordered list of meshes and per-LOD screen-coverage thresholds

`LodGroup` SHALL be a non-`Entity` value type that owns an ordered list of `Mesh::Ptr` (LOD 0 is highest detail) and a parallel list of `float` screen-coverage thresholds in the range `[0.0, 1.0]`. The list of meshes and the list of thresholds MUST have the same length. A `LodGroup` with 0 entries MUST be treated as invalid (an empty group is never assigned to a `MeshRender`). A `LodGroup` with 1 entry SHALL behave identically to a plain `Mesh` (the renderer always picks the single entry regardless of screen coverage). Thresholds MUST be in non-increasing order (LOD 0 ≥ LOD 1 ≥ … ≥ LOD N-1); the API SHALL reject out-of-order thresholds with a `FURYE` log and the `LodGroup` MUST keep its last valid state.

#### Scenario: Construct a 3-LOD group
- **WHEN** the user constructs a `LodGroup` with 3 `Mesh::Ptr`s and thresholds `{1.0, 0.5, 0.1}`
- **THEN** `LodGroup::GetLodCount() == 3`
- **AND** `LodGroup::GetMesh(0)`, `GetMesh(1)`, `GetMesh(2)` return the three meshes in order
- **AND** `LodGroup::GetThreshold(0..2)` return `1.0`, `0.5`, `0.1` respectively

#### Scenario: Reject out-of-order thresholds
- **WHEN** the user constructs a `LodGroup` with thresholds `{0.5, 1.0, 0.1}`
- **THEN** a `FURYE` log is emitted naming the out-of-order index
- **AND** the thresholds are not modified (the group keeps its previous valid state)

#### Scenario: Single-LOD group behaves as a plain mesh
- **WHEN** a `LodGroup` holds 1 `Mesh::Ptr`
- **THEN** `MeshRender::GetActiveLod()` always returns index 0 regardless of the camera

### Requirement: `MeshRender` SHALL accept an optional `LodGroup` and SHALL choose the active LOD per frame

`MeshRender` SHALL expose `SetLodGroup(const LodGroup&)` and `GetLodGroup() const`. When a `LodGroup` is bound with ≥2 entries, the engine's per-frame draw path SHALL compute the active LOD index by taking the largest `i` such that `LodGroup::GetThreshold(i) >= screen_coverage`, where `screen_coverage` is the fraction of the viewport occupied by the model's AABB projected through the active camera. The active LOD index SHALL be cached on the `MeshRender` (`GetActiveLod()`) and SHALL be used by `Shader::BindMesh` / `BindSubMesh` to select which `Mesh::Ptr` is drawn. When no `LodGroup` is bound, `MeshRender` SHALL fall back to its current single-`Mesh` draw path and `GetActiveLod()` SHALL return 0.

#### Scenario: Active LOD is the highest matching threshold
- **WHEN** a `MeshRender` has a 3-LOD group with thresholds `{1.0, 0.5, 0.1}` and the current screen coverage is `0.6`
- **THEN** `GetActiveLod()` returns `1` (because `0.6 >= 0.5` and `0.6 < 1.0`)

#### Scenario: Active LOD is the deepest when coverage is below the lowest threshold
- **WHEN** a `MeshRender` has a 3-LOD group with thresholds `{1.0, 0.5, 0.1}` and the current screen coverage is `0.05`
- **THEN** `GetActiveLod()` returns `2` (the deepest LOD)

#### Scenario: LOD 0 selected when coverage is at or above the highest threshold
- **WHEN** a `MeshRender` has a 3-LOD group with thresholds `{1.0, 0.5, 0.1}` and the current screen coverage is `1.0`
- **THEN** `GetActiveLod()` returns `0`

#### Scenario: No LodGroup falls back to single-mesh draw
- **WHEN** a `MeshRender` has no `LodGroup` bound (or the group is empty)
- **THEN** `GetActiveLod()` returns 0
- **AND** the bound single `Mesh` is drawn every frame

### Requirement: `MeshRender` SHALL persist its `LodGroup` through Save / Load

`MeshRender::Save` SHALL write a `lod_group` object containing `thresholds` (float array, length N) and `meshes` (string array of registered `Mesh` names, length N) when a `LodGroup` is bound with ≥1 entries. `MeshRender::Load` SHALL read `lod_group` as optional; if present and non-empty, it SHALL resolve each mesh name via `Scene::Manager()->Get<Mesh>(name)` and call `SetLodGroup`. If any mesh name in `meshes` fails to resolve, the load SHALL emit `FURYE` and reject the whole record. Save / Load MUST be backward-compatible: scene files written before this feature load unchanged (`lod_group` is absent and the `MeshRender` is treated as a single-mesh `MeshRender`).

#### Scenario: Round-trip a 3-LOD group
- **WHEN** a `MeshRender` has a 3-LOD group with thresholds `{1.0, 0.5, 0.1}` and meshes `["Tree_LOD0", "Tree_LOD1", "Tree_LOD2"]`
- **THEN** `Save` writes a `lod_group` key with `thresholds: [1.0, 0.5, 0.1]` and `meshes: ["Tree_LOD0", "Tree_LOD1", "Tree_LOD2"]`
- **AND** `Load` of the same JSON reconstructs the same 3-LOD group

#### Scenario: Load rejects unresolved LOD mesh names
- **WHEN** `Load` reads a `lod_group` whose `meshes` array contains `"DoesNotExist"`
- **THEN** a `FURYE` log names the missing mesh
- **AND** the `MeshRender` is rejected (the whole `Load` returns false)

#### Scenario: Load a pre-LOD scene file
- **WHEN** `Load` reads a `MeshRender` JSON that has no `lod_group` key
- **THEN** the `MeshRender` keeps its single-`Mesh` binding
- **AND** `GetActiveLod()` returns 0
- **AND** no error is logged

### Requirement: The MeshRender body in the editor's scene inspector SHALL display the active LOD and the configured thresholds

The scene-inspector MeshRender body SHALL render a read-only "Active LOD" readout (current `MeshRender::GetActiveLod()` value) and a list of `{threshold, mesh name}` pairs drawn from the bound `LodGroup`. When no `LodGroup` is bound, the panel SHALL display "LOD: (single mesh)" and hide the threshold list. The thresholds are read-only in v1.

#### Scenario: Display active LOD and thresholds for a 3-LOD group
- **WHEN** the user selects a `MeshRender` whose `LodGroup` is `{1.0, 0.5, 0.1}` × `{Tree_LOD0, Tree_LOD1, Tree_LOD2}` and the active LOD is 1
- **THEN** the panel shows "Active LOD: 1"
- **AND** it lists `1.000 → Tree_LOD0`, `0.500 → Tree_LOD1`, `0.100 → Tree_LOD2`

#### Scenario: Display single-mesh fallback
- **WHEN** the user selects a `MeshRender` with no `LodGroup`
- **THEN** the panel shows "LOD: (single mesh)"
- **AND** no threshold list is drawn
