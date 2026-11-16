## ADDED Requirements

### Requirement: Import SHALL generate cluster data transparently and the editor SHALL expose runtime controls

Import SHALL generate and validate cluster payloads for eligible static meshes without requiring a Mesh editor action. The Mesh editor SHALL display build, cache, validation, and fallback status for the bound mesh and MAY provide a diagnostic rebuild action with validated cluster-size, reduction, and error options. The viewport or render settings SHALL provide persisted, default-off cluster enablement and a positive screen-space error threshold. Rebuilding a shared mesh SHALL update its descriptor through the normal save/cook flow without mutating in-flight render resources.

#### Scenario: Import the tank cluster payload

- **WHEN** the user imports the shared tank mesh
- **THEN** the importer invokes the common builder once without requiring confirmation
- **AND** the Mesh editor displays validated cluster/group counts
- **AND** the mesh receives the resulting content-keyed descriptor
- **AND** all shared tank instances use the payload on their next eligible packet

#### Scenario: Default remains legacy rendering

- **WHEN** the editor starts without a prior cluster setting
- **THEN** cluster rendering is disabled
- **AND** existing meshes render through their previous paths

### Requirement: The editor SHALL visualize selected cluster LOD and identity

The existing mutually exclusive viewport debug-view mechanism SHALL add `Cluster LOD` and `Cluster ID` entries. `Cluster LOD` SHALL color selected clusters by a stable build-level palette, and `Cluster ID` SHALL color them by a deterministic hash of the selected cluster identifier. Both views SHALL derive color from the same visible record used for geometry. Returning to `None` SHALL restore normal GBuffer shading without stale debug uniforms or storage bindings.

#### Scenario: Inspect near and far LOD levels

- **WHEN** the user selects `Cluster LOD` and moves between the tank sample's near and far cameras
- **THEN** visible cluster colors reflect the levels selected by the GPU cut
- **AND** distant views use coarser-level colors when the error threshold permits

#### Scenario: Disable the debug view

- **WHEN** the user returns from `Cluster ID` to `None`
- **THEN** normal material shading resumes on the next presented frame
- **AND** no cluster debug tint remains active

### Requirement: Debug views SHALL report unsupported and fallback states

Selecting a cluster debug view while cluster rendering is disabled or unavailable SHALL present a valid scene or fallback image and a specific reason, such as disabled compute, missing indirect count, missing per-draw ID, or unsupported Metal ICB range. Mandatory payload failures SHALL be reported by asset loading before debug selection. The editor SHALL NOT sample uninitialized cluster buffers.

#### Scenario: Vulkan lacks a required feature

- **WHEN** `Cluster LOD` is selected and the Vulkan device lacks both supported per-draw-ID mechanisms
- **THEN** the editor reports that cluster rendering is unavailable because per-draw identification is unsupported
- **AND** the legacy tank remains visible

### Requirement: Cluster statistics SHALL be frame-associated and on demand

The editor and automation SHALL expose candidate instance, selected cluster, visible cluster, indirect command, batch, drawn triangle, capacity overflow, and fallback counts. GPU buffer readback SHALL occur only on demand or in automation, SHALL carry the associated frame identifier, and SHALL NOT drive the normal frame's selection or draw count. Statistics SHALL distinguish indirect cluster draws from legacy fallback draws.

#### Scenario: Read statistics for a captured frame

- **WHEN** automation requests statistics for a completed tank frame
- **THEN** the returned counters are associated with that frame
- **AND** selected, visible, and indirect counts are consistent with the captured draw arguments
- **AND** no readback is inserted into unrelated normal frames

### Requirement: A reusable tank sample SHALL exercise shared multi-material cluster rendering

The change SHALL provide a reproducible tank sample or automation scene using the existing tank asset, with all three material sections, multiple shared-mesh instances, near and far camera views, and transformed instances including non-uniform scale. The sample SHALL support baseline legacy, cluster, and debug-view captures without modifying the checked-in source asset.

#### Scenario: Run the tank comparison

- **WHEN** automation runs the tank sample at its near and far cameras
- **THEN** it captures legacy and cluster images plus selected/drawn cluster and triangle statistics
- **AND** the shared mesh payload is built or loaded once
- **AND** all material sections and instance transforms are exercised

### Requirement: Vulkan diagnostics SHALL support capture and replay validation

The Vulkan path SHALL be testable with the repository's gfxrecon CLI workflow. Capture metadata or accompanying semantic output SHALL identify the frame, selected/culled counts, indirect batch counts, draw-count values, and per-draw-ID mode. Validation SHALL capture and replay a cluster frame and inspect compute dispatches, barriers, indexed indirect-count commands, and final output. A capture tool failure SHALL be reported distinctly from a renderer failure.

#### Scenario: Capture and replay the tank

- **WHEN** the Vulkan tank sample is captured with gfxrecon and replayed through the CLI tool
- **THEN** the capture contains the expected selection/culling dispatches and indexed indirect-count draws
- **AND** replay completes without validation or replay errors attributable to the cluster path
- **AND** the final image agrees with the non-captured run within the established tolerance
