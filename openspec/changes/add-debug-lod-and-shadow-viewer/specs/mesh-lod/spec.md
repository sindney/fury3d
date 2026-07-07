## ADDED Requirements

### Requirement: `Mesh::GetLodCount` and `MeshRender::GetActiveLod` SHALL be stable per-frame accessors for editor debug reads

`Mesh::GetLodCount()` SHALL return the same value within a single frame's render pass (no mid-frame mutation). `MeshRender::GetActiveLod()` SHALL return the value most recently set by `UpdateActiveLod(cameraNode)` and SHALL remain valid for editor debug reads (e.g. histogram walks in the Profiler's LOD Debug section) until the next `UpdateActiveLod` call. Both accessors SHALL be `const`-qualified so editor code can read them without acquiring mutable ownership.

#### Scenario: LOD count is stable mid-frame

- **WHEN** the editor's Profiler reads `mesh->GetLodCount()` for a `MeshRender` mid-render
- **THEN** the returned value matches what `DrawUnit` saw when picking the active mesh

#### Scenario: Active LOD is readable from editor code

- **WHEN** the editor's LOD Debug section walks all `MeshRender`s in the scene and calls `render->GetActiveLod()` on each
- **THEN** the returned indices match the LODs actually drawn in the same frame

#### Scenario: Accessors are const

- **WHEN** the editor reads `mesh->GetLodCount()` and `render->GetActiveLod()` from a `const MeshRender*`
- **THEN** the calls compile and return the same value as the non-const overload