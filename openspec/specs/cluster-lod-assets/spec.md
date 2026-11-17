# cluster-lod-assets Specification

## Purpose
TBD - created by archiving change add-gpu-cluster-lod. Update Purpose after archive.

## Requirements

### Requirement: Cluster LOD generation SHALL consume eligible static mesh topology exactly once

The engine SHALL provide a deterministic CPU-only cluster LOD builder invoked automatically for eligible static triangle meshes after import and weld. The builder SHALL consume each submesh index buffer as the canonical topology and SHALL NOT also process the duplicate top-level index array. It SHALL build once per shared `Mesh`, preserve the source vertex streams and submesh-to-material mapping, and reject skinned or non-triangle inputs without modifying the source mesh. Eligible static meshes SHALL receive a validated payload before native save without requiring user action.

#### Scenario: Shared tank mesh builds once

- **WHEN** cluster LOD is generated for the six tank nodes sharing `Tank.005`
- **THEN** the builder runs once for `Tank.005`
- **AND** its three source submeshes retain their material assignments
- **AND** the source vertices and legacy submesh indices remain available for fallback rendering

#### Scenario: Ineligible input is rejected safely

- **WHEN** the builder receives a skinned mesh or non-triangle topology
- **THEN** it reports the unsupported input
- **AND** it returns no partial cluster payload
- **AND** the source mesh remains unchanged

### Requirement: LOD zero SHALL be partitioned into bounded drawable clusters

The builder SHALL partition source geometry into spatially coherent indexed clusters targeting approximately 128 triangles with a configurable validated limit and at most 128 referenced source vertices per cluster. Every cluster SHALL reference one material, a range in the merged cluster index allocation, conservative bounding geometry, and a normal cone. Cluster construction SHALL use the vendored meshoptimizer meshlet and bounds facilities or equivalents validated against them.

#### Scenario: Tank LOD zero respects cluster limits

- **WHEN** the tank is built with the default 128-triangle target
- **THEN** every generated LOD-0 cluster contains no more than the configured triangle and vertex limits
- **AND** every cluster references exactly one of the tank material sections
- **AND** all source triangles are represented exactly once across LOD-0 clusters

### Requirement: Parent levels SHALL contain drawable group-simplified geometry

The builder SHALL group adjacent clusters through shared-edge adjacency, lock group exterior and shared geometric boundaries, simplify group interiors with border-preserving simplification, and re-split the result into drawable clusters linked to their sources. Groups SHALL NOT cross material boundaries in the initial representation. Open boundaries, material boundaries, and duplicated-position attribute seams SHALL remain locked when necessary to prevent cracks. The process SHALL repeat to one or more coarse roots; disconnected regions MAY form a forest. Independent per-cluster simplification and bounds-only parent nodes SHALL NOT be used.

#### Scenario: Simplified group remains drawable

- **WHEN** a group of neighboring clusters is simplified and re-split
- **THEN** its parent clusters contain valid indexed geometry covering the group's interior
- **AND** the locked group boundary is preserved
- **AND** parent group and child group links are recorded

#### Scenario: Multiple materials do not simplify across a seam

- **WHEN** adjacent tank triangles belong to different material sections
- **THEN** the initial grouping does not combine their material sections
- **AND** simplification does not move their shared material boundary

### Requirement: Cluster errors and bounds SHALL preserve a unique conservative DAG cut

Every cluster SHALL have exactly one parent group unless it is a root. All output clusters of a group SHALL share that group's selection-error values. Node error SHALL be the maximum of its local simplification error and all child propagated errors, and group error SHALL bound every propagated error in its subtree. Leaf error SHALL be zero and root parent error SHALL be infinite. Parent bounds SHALL conservatively contain their own geometry, error radius, and all descendant bounds. Meshoptimizer error values SHALL be converted to mesh units using the pinned API's documented extent convention.

#### Scenario: Errors are monotonic along every path

- **WHEN** the builder completes any parent/child chain
- **THEN** each child error is less than or equal to its parent error
- **AND** each descendant group error is less than or equal to the corresponding ancestor error
- **AND** each parent bounding volume contains all descendant bounds

#### Scenario: Root and leaf sentinels are explicit

- **WHEN** the runtime inspects a leaf and a root
- **THEN** the leaf selection error is zero
- **AND** the root parent selection error is the defined infinite sentinel

### Requirement: The builder SHALL validate topology and legal cuts before publishing a payload

Validation SHALL check index and range bounds, cluster limits, material ownership, single-parent links, DAG acyclicity, monotonic errors and bounds, finite metadata except documented sentinels, and complete group transitions. A CPU reference selector SHALL sample multiple error thresholds and verify that selected nodes form exactly one non-overlapping cut with preserved locked boundaries on closed, open, disconnected, and multi-material fixtures. A failed validation SHALL discard the generated payload and report the failing invariant.

#### Scenario: Closed grid has no crack at sampled cuts

- **WHEN** a closed grid fixture is built and the CPU selector samples thresholds from LOD zero through the roots
- **THEN** each selected cut is complete and non-overlapping
- **AND** no new open boundary appears along a locked internal group boundary

#### Scenario: Invalid parent error blocks publication

- **WHEN** validation finds a parent error smaller than its child error
- **THEN** the builder reports the parent and child identifiers
- **AND** no cache entry or mesh descriptor is published

### Requirement: Cluster payloads SHALL be versioned, content-addressed binary sidecars

`Mesh` serialization SHALL store an optional cluster descriptor containing schema version, build settings, enablement, and a full source-content hash. The merged 32-bit LOD indices, cluster records, group records, and diagnostic links SHALL reside in a versioned binary `.clod` payload rather than inline scene JSON. Cluster indices SHALL reference the original mesh vertex streams. The content key SHALL cover every render-relevant vertex stream, all submesh indices and material boundaries, build options, meshoptimizer version, builder version, and payload schema.

#### Scenario: Descriptor survives JSON and BIN round trips

- **WHEN** a mesh with a cluster descriptor is saved and loaded through both native JSON and BIN scene formats
- **THEN** the descriptor's hash, settings, version, and enablement are preserved
- **AND** the source mesh and legacy LOD chain are preserved
- **AND** the cluster arrays are not duplicated into the scene document

#### Scenario: Attribute-only change invalidates the payload

- **WHEN** normals, UVs, colors, or another render-relevant stream changes without changing positions or indices
- **THEN** the cluster content key changes
- **AND** the previous payload is not accepted as current

### Requirement: Loading SHALL validate the mandatory descriptor and payload

The editor and runtime SHALL resolve payloads through the durable `Resource/ClusterLOD/` source store, the DDC cache, or packaged `AssetBackend`, validate the header, source hash, sizes, alignment, and payload invariants, and attach the result to the shared mesh. The durable source payload SHALL be written during import or migration so a fresh checkout does not depend on a host DDC. A missing descriptor or a missing, stale, truncated, or incompatible payload for an eligible static mesh SHALL produce a diagnostic and fail asset loading. Runtime loading SHALL NOT invoke the builder or substitute whole-mesh rendering for invalid mandatory data. Ineligible meshes do not require a descriptor or payload.

#### Scenario: Missing payload fails asset loading

- **WHEN** a scene contains an eligible static mesh with a cluster descriptor but its payload is absent from the source store, DDC, and pak
- **THEN** the asset load fails and identifies the missing content key
- **AND** no runtime generation or legacy missing-payload path is started

#### Scenario: Packaged payload attaches to embedded mesh

- **WHEN** a packaged scene embeds a mesh with a descriptor and the pak contains the matching `cluster_meshes` artifact
- **THEN** the runtime loads the artifact through `AssetBackend`
- **AND** attaches it to every shared instance of that mesh
