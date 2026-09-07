# mesh-lod (delta)

## ADDED Requirements

### Requirement: `Mesh` LOD tiers SHALL support a billboard flag

`Mesh` SHALL hold a per-tier billboard flag list (`m_LodBillboardFlags`, `std::vector<bool>`) parallel to `m_LodMeshes` (LOD 0 is never a billboard). `Mesh::IsLodBillboard(i)` SHALL return the flag for tier `i` (false for out-of-range or empty lists). The flags SHALL validate against the chain in `SetLodMeshes` (either empty or matching size; mismatch emits `FURYE` and leaves the chain unchanged) and SHALL serialize inside the mesh's `lod_meshes` sub-object. Renderers SHALL draw flagged tiers via the billboard shader path (see `vegetation-rendering`) and skip them in shadow passes.

#### Scenario: Flagged terminal tier

- **WHEN** a mesh has 3 LOD meshes with billboard flags `{false, false, true}`
- **THEN** `IsLodBillboard(0)` and `IsLodBillboard(1)` return false
- **AND** `IsLodBillboard(2)` returns true
- **AND** the flags survive a scene save/load round-trip

#### Scenario: Flag/chain size mismatch rejected

- **WHEN** `SetLodMeshes` is called with 3 meshes, 3 thresholds, and 2 billboard flags
- **THEN** a `FURYE` log is emitted
- **AND** the existing chain and flags are unchanged

### Requirement: Shadow passes SHALL draw the deepest non-billboard LOD tier instead of LOD 0

When a shadow pass draws a mesh with a LOD chain, it SHALL select the deepest tier whose billboard flag is not set (rather than always drawing LOD 0), bounding shadow cost for high-detail vegetation. Meshes without a chain SHALL draw LOD 0 as before.

#### Scenario: Forest shadows use the deep tier

- **WHEN** a mesh with tiers `{LOD0, LOD1, LOD2, billboard}` casts into a shadow map
- **THEN** the shadow draw uses LOD2 geometry

#### Scenario: Single-mesh shadows unchanged

- **WHEN** a mesh with no LOD chain casts a shadow
- **THEN** the shadow draw uses its LOD 0 geometry
