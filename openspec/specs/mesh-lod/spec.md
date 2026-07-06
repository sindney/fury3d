# mesh-lod

## Purpose

LOD-aware mesh data model and runtime LOD selection. A `Mesh` may carry an inline LOD chain of additional `Mesh::Ptr` objects (LOD 1..N) alongside its own LOD-0 data. The runtime picks the active LOD each frame from the camera's screen-coverage of the mesh's AABB, and the editor exposes a "Generate LODs…" action that builds the chain via `meshopt_simplifySloppy`. Together these give artists a one-click way to produce production-grade LODs from any imported mesh without authoring `_LOD0`/`_LOD1`/`_LOD2` variants by hand.

## Requirements

### Requirement: `Mesh` SHALL own its LOD chain

`Mesh` SHALL hold an ordered list of additional LOD meshes (`m_LodMeshes`, type `std::vector<std::shared_ptr<Mesh>>`) and a parallel list of screen-coverage thresholds (`m_LodThresholds`, type `std::vector<float>`). LOD 0 is the source mesh itself; LODs 1..N are the additional entries in `m_LodMeshes`. `Mesh::GetLodCount()` returns `1 + m_LodMeshes.size()`. `Mesh::GetLodMesh(0)` returns the source mesh (`this`); `Mesh::GetLodMesh(i)` for `i > 0` returns `m_LodMeshes[i-1]`. `Mesh::GetLodThreshold(0)` returns `1.0`; `Mesh::GetLodThreshold(i)` for `i > 0` returns `m_LodThresholds[i-1]`. `m_LodMeshes` is owned by the mesh and serialized inline as a `lod_meshes` sub-object on the mesh's JSON; LOD meshes are NOT registered as separate entities in the scene's `EntityManager`. A `Mesh` with `m_LodMeshes.size() == 0` has no chain (single-mesh draw path).

#### Scenario: Mesh exposes its chain

- **WHEN** a mesh has 3 additional LOD meshes with thresholds `{0.5, 0.25, 0.0}` in `m_LodMeshes`
- **THEN** `GetLodCount()` returns `4`
- **AND** `GetLodMesh(0)` returns the mesh itself
- **AND** `GetLodMesh(1..3)` returns the 3 additional meshes in order
- **AND** `GetLodThreshold(0)` returns `1.0`
- **AND** `GetLodThreshold(1..3)` returns `0.5`, `0.25`, `0.0`

#### Scenario: Single-mesh path when chain is empty

- **WHEN** a mesh has `m_LodMeshes.size() == 0`
- **THEN** `GetLodCount()` returns `1`
- **AND** the renderer always draws the source mesh (`GetLodMesh(0)`)

### Requirement: `Mesh::SetLodMeshes` SHALL validate and apply a new chain

`Mesh::SetLodMeshes(meshes, thresholds)` SHALL validate that `meshes.size() == thresholds.size()` and that the thresholds are non-increasing (LOD 1 ≥ LOD 2 ≥ … ≥ LOD N); invalid input SHALL emit `FURYE` and leave the chain unchanged. On valid input, the chain is replaced.

#### Scenario: SetLodMeshes rejects out-of-order thresholds

- **WHEN** `SetLodMeshes` is called with 3 meshes and thresholds `{0.25, 0.5, 0.0}`
- **THEN** a `FURYE` log is emitted naming the out-of-order index
- **AND** `m_LodThresholds` is not modified

#### Scenario: SetLodMeshes replaces the chain

- **WHEN** `SetLodMeshes` is called with valid inputs
- **THEN** `m_LodMeshes` and `m_LodThresholds` are replaced with the new values
- **AND** `GetLodCount()` reflects the new chain length

### Requirement: The runtime SHALL pick the active LOD each frame from the camera's screen-coverage of the mesh's AABB

`MeshRender` SHALL cache an `m_ActiveLod` (per-instance), updated by `UpdateActiveLod(cameraNode)` on each visible instance. The active LOD is the largest `i` such that `Mesh::GetLodThreshold(i) >= coverage`, where `coverage` is the fraction of the viewport occupied by the mesh's AABB projected through the active camera. When the bound mesh has no chain (`GetLodCount() <= 1`), `m_ActiveLod` is always `0` and the bound mesh is drawn every frame.

#### Scenario: Active LOD follows screen-coverage

- **WHEN** a `MeshRender` references a mesh with chain thresholds `{1.0, 0.5, 0.1}`
- **AND** the camera's screen-coverage of the mesh is `0.6`
- **THEN** `m_ActiveLod` is `1` (`0.6 >= 0.5` and `0.6 < 1.0`)

#### Scenario: Active LOD is deepest when coverage is below the lowest threshold

- **WHEN** the screen-coverage is `0.05`
- **THEN** `m_ActiveLod` is `2` (the deepest LOD)

#### Scenario: No chain falls back to LOD 0

- **WHEN** the bound mesh has `GetLodCount() == 1`
- **THEN** `m_ActiveLod` is always `0`

### Requirement: `Mesh::Save` and `Mesh::Load` SHALL serialize and restore the LOD chain inline

`Mesh::Save` SHALL emit a `lod_meshes` array (full sub-Mesh JSON for each entry) and a parallel `lod_thresholds` float array when `m_LodMeshes` is non-empty. `Mesh::Load` SHALL read both arrays (optional — pre-LOD scene files don't have them); each entry in `lod_meshes` is loaded as a sub-Mesh and attached to the parent mesh's chain via `m_LodMeshes.push_back`. The chain survives a `FileUtil::SaveFile` → `FileUtil::LoadFile` round-trip.

#### Scenario: Round-trip a 3-LOD chain

- **WHEN** a mesh has `m_LodMeshes = [Tree_LOD1, Tree_LOD2]` and `m_LodThresholds = [0.5, 0.0]`
- **AND** the scene is saved then loaded
- **THEN** the loaded mesh has `GetLodCount() == 3`
- **AND** `GetLodMesh(1)->GetName() == "Tree_LOD1"`
- **AND** `GetLodThreshold(2) == 0.0`

#### Scenario: Load a pre-LOD mesh file

- **WHEN** `Load` reads a `Mesh` JSON that has no `lod_meshes` key
- **THEN** the mesh's `m_LodMeshes` is empty
- **AND** `GetLodCount() == 1`
- **AND** no error is logged

### Requirement: The engine SHALL provide `MeshSimplifier::SimplifyMesh` to generate an LOD chain from a source mesh

`MeshSimplifyOptions` SHALL configure `lod_count` (additional LODs beyond the source, clamped 1..5), `reduction_ratio` (per-level factor in (0,1); default `0.5`), and `target_error` (meshopt error tolerance; default `0.5` for sloppy-grid reduction). `SimplifyMesh(source, opts)` SHALL return a `MeshSimplifyResult` containing N generated meshes (LOD 1..N) and a parallel threshold array. The implementation SHALL use `meshopt_simplifySloppy` (aggressive, ignores borders) so meshes with many locked border vertices (typical of FBX/glTF UV-seamed assets) still reduce visibly. Per-submesh simplification is used so multi-submesh source meshes preserve their submesh structure.

#### Scenario: SimplifyMesh reduces triangles

- **WHEN** `SimplifyMesh` is called on a 10252-vert / 15189-tri mesh with `lod_count=3, reduction_ratio=0.5, target_error=0.5`
- **THEN** the result has 3 LOD meshes with strictly decreasing triangle counts
- **AND** LOD 1 has roughly half the source's triangles
- **AND** LOD 3 has roughly an eighth of the source's triangles
- **AND** the per-LOD thresholds are non-increasing (`1-ratio > 1-ratio² > ... > 0`)

#### Scenario: SimplifyMesh handles submeshes

- **WHEN** `SimplifyMesh` is called on a source with 3 submeshes
- **THEN** each generated LOD also has 3 submeshes
- **AND** the submesh count is preserved (1:1 with the source)

### Requirement: The editor SHALL provide a "Generate LODs…" action on the mesh editor window

The mesh editor SHALL include a `Generate LODs…` button (visible regardless of whether a chain already exists). Clicking it SHALL open a modal with `Total levels` (range 2..5), `Reduction ratio` (range 0.05..0.95, default 0.5), and `Target error` (range 0.001..1.0, default 0.5) inputs. On confirm, the editor calls `SimplifyMesh` and assigns the result to the source mesh via `Mesh::SetLodMeshes`. The chain is visible to all `MeshRender`s referencing the mesh immediately.

#### Scenario: Generate LODs creates a chain

- **WHEN** the user opens the mesh editor for a mesh with no chain
- **AND** clicks `Generate LODs…` with default settings
- **AND** confirms
- **THEN** the mesh's LOD chain has 3 entries
- **AND** the LOD dropdown in the metadata block lists `LOD 0..3`