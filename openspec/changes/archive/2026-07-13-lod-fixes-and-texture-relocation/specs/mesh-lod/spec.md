## MODIFIED Requirements

### Requirement: The runtime SHALL pick the active LOD each frame from the camera's screen-coverage of the mesh's AABB

`MeshRender` SHALL cache an `m_ActiveLod` (per-instance), updated by `UpdateActiveLod(cameraNode)` on each visible instance. The active LOD is the **first** `i` walking `i = 0..count-1` such that `coverage >= Mesh::GetLodThreshold(i)`; when no threshold is met, `m_ActiveLod` is the deepest LOD (`count - 1`). `coverage` is the fraction of the viewport occupied by the mesh's AABB projected through the active camera, and it SHALL be computed from the mesh's **world-space** AABB — the owning `SceneNode`'s world transform (translation, rotation, scale) SHALL be applied to the model-space AABB before projection, so a mesh that is not at the world origin or not at unit scale still yields correct coverage. When the bound mesh has no chain (`GetLodCount() <= 1`), `m_ActiveLod` is always `0` and the bound mesh is drawn every frame.

The previous comparison (`Mesh::GetLodThreshold(i) >= coverage`) was inverted: because `GetLodThreshold(0)` is fixed at `1.0` and `coverage` is clamped to `<= 1.0`, the first iteration was always true, pinning every mesh to LOD 0 regardless of camera distance. The corrected predicate is `coverage >= Mesh::GetLodThreshold(i)`.

#### Scenario: Active LOD follows screen-coverage

- **WHEN** a `MeshRender` references a mesh with chain thresholds `{1.0, 0.5, 0.1}`
- **AND** the camera's screen-coverage of the mesh is `0.6`
- **THEN** `m_ActiveLod` is `1` (`0.6 < 1.0` so LOD 0 is skipped; `0.6 >= 0.5` so LOD 1 is picked)

#### Scenario: Active LOD is deepest when coverage is below the lowest threshold

- **WHEN** the screen-coverage is `0.05`
- **THEN** `m_ActiveLod` is `2` (no threshold is met, so the deepest LOD is used)

#### Scenario: Zooming the camera out advances the active LOD

- **WHEN** the camera starts close enough that coverage is `0.9` (`m_ActiveLod == 0`)
- **AND** the camera zooms out until coverage drops to `0.3`
- **THEN** `m_ActiveLod` changes from `0` to a deeper LOD across the zoom
- **AND** the LOD-debug color and the drawn triangle count change accordingly

#### Scenario: Coverage uses the world-space AABB

- **WHEN** a mesh's owning node is translated far from the world origin and scaled to `0.5`
- **AND** the camera is positioned so the mesh's projected world-space AABB covers `0.2` of the viewport
- **THEN** `coverage` is computed as `0.2` (using the world-transformed AABB, not the model-space AABB)
- **AND** the active LOD reflects that coverage

#### Scenario: No chain falls back to LOD 0

- **WHEN** the bound mesh has `GetLodCount() == 1`
- **THEN** `m_ActiveLod` is always `0`
