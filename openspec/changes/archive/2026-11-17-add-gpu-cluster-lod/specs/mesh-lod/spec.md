## ADDED Requirements

### Requirement: Cluster LOD SHALL take precedence only for an active eligible cluster instance

When a `MeshRender` has a valid enabled cluster payload and the active camera/backend path supports cluster rendering, opaque GBuffer submission SHALL use the selected cluster cut instead of drawing a whole-mesh LOD tier. The existing `m_LodMeshes` chain and thresholds SHALL remain serialized and available for fallback. Cluster selection SHALL NOT rewrite the chain or convert it into a flat LOD sequence.

#### Scenario: Cluster mesh retains its legacy chain

- **WHEN** a mesh with both a whole-mesh LOD chain and valid cluster data renders through the cluster path
- **THEN** its opaque GBuffer geometry comes from selected clusters
- **AND** the existing LOD meshes, thresholds, and flags remain intact for fallback

### Requirement: Existing LOD rendering SHALL remain the compatibility path

Eligible static meshes SHALL load with valid mandatory cluster data even when cluster rendering is disabled. Skinned, transparent, billboard, vegetation-wind, unsupported material, or unsupported backend instances SHALL continue to use their existing whole-mesh LOD and rendering behavior. Disabling cluster rendering SHALL restore the previous LOD selection without requiring asset regeneration. Missing or invalid mandatory cluster data SHALL fail eligible asset loading rather than selecting a legacy missing-payload path.

#### Scenario: Disable cluster rendering at runtime

- **WHEN** a mesh with both LOD representations switches from cluster rendering to disabled or unsupported mode
- **THEN** the renderer draws the existing active whole-mesh LOD tier
- **AND** no cluster indirect draw for that instance remains in the frame

#### Scenario: Billboard tier is not replaced by clusters

- **WHEN** an existing mesh reaches a flagged billboard terminal tier or uses the billboard rendering path
- **THEN** that path remains outside initial cluster eligibility
- **AND** its billboard material and atlas behavior are unchanged

### Requirement: Legacy LOD debugging SHALL NOT misrepresent cluster selection

The existing whole-mesh `LOD_DEBUG_COLORS` overlay SHALL continue to describe legacy whole-mesh draws. Cluster draws SHALL use the dedicated cluster debug views and visible-record metadata rather than reporting `MeshRender::GetActiveLod()` as their selected geometric level.

#### Scenario: Both representations are visible in one viewport

- **WHEN** legacy LOD colors are enabled while one mesh uses clusters and another uses a whole-mesh LOD chain
- **THEN** the legacy mesh retains its existing LOD tint behavior
- **AND** the cluster mesh is inspected through `Cluster LOD` rather than a fabricated whole-mesh active-LOD index
