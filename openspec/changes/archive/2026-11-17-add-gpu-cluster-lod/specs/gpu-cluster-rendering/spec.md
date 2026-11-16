## ADDED Requirements

### Requirement: Cluster rendering SHALL be opt-in and capability-gated

The renderer SHALL use cluster rendering only for eligible opaque static `MeshRender` instances with a valid mandatory cluster payload, an enabled setting, and all required backend capabilities. Missing or invalid mandatory payloads SHALL fail asset loading before renderer activation. Incompatible materials or passes, unsupported projections, disabled compute, and missing indirect features SHALL select the existing rendering path with a reported reason. Enabling the setting SHALL NOT change unsupported meshes or scene load success.

A device lacking indirect-count support is unsupported for GPU cluster rendering and SHALL use the existing rendering path with an explicit capability diagnostic. No single-indirect-command emulation path is retained. Native Vulkan acceptance requires multi-draw indirect with GPU-written counts.

#### Scenario: Unsupported device retains the scene

- **WHEN** cluster rendering is enabled on a backend without the required indirect-count or per-draw-ID support
- **THEN** eligible meshes render through their legacy paths
- **AND** the renderer reports the unavailable capability
- **AND** no uninitialized indirect command is executed

### Requirement: Cluster work SHALL consume render-thread-owned immutable frame inputs

Eligible instances SHALL be snapshotted with base-mesh resources, material state, world transforms, camera parameters, and cluster descriptors in the `FramePacket`. Cluster buffers, counters, indirect batches, readbacks, and native backend resources SHALL be created, accessed, retained, and retired according to render-thread RHI rules. Toggling or rebuilding a mesh SHALL NOT mutate resources referenced by an in-flight packet.

#### Scenario: Toggle during render-thread execution

- **WHEN** cluster rendering is toggled while another frame is executing
- **THEN** each packet uses one consistent mode and resource set
- **AND** no game-thread scene access or premature GPU resource destruction occurs

### Requirement: GPU instance compaction SHALL precede cluster selection

A compute pass SHALL test eligible instance bounds against the active camera frustum and compact surviving instances into a GPU-visible list. Cluster selection SHALL iterate only compacted instances and SHALL retain the original stable instance identifier for transforms, hysteresis, debug statistics, and visible-record generation.

#### Scenario: Off-screen instance consumes no cluster work

- **WHEN** an eligible tank instance lies completely outside the camera frustum
- **THEN** it is absent from the compacted instance list
- **AND** none of its clusters are selected or emitted for that camera

### Requirement: GPU selection SHALL evaluate one conservative DAG cut per visible instance

The selection pass SHALL evaluate one atomic predicate per producing group, with each leaf cluster acting as an individual leaf node. Self distance SHALL use the producing group's conservative bounds, or the leaf cluster's own bounds for a leaf. Parent distance SHALL use the parent group's own conservative bounds. Each term SHALL use the same instance scale, projection factor, and user threshold. Scale SHALL be a conservative bound derived from the world matrix rather than scale-polluted piecewise TRS accessors. Root and leaf sentinels SHALL follow the asset contract. Containing parent bounds and propagated-max errors SHALL make projected errors monotonic along every root-to-leaf path.

The predicate SHALL be evaluated exactly once per leaf or producing group per instance, writing selected bits for every cluster owned by that node plus selected-pair records consumed by later passes. Culling or command generation SHALL NOT re-evaluate the predicate. Hysteresis SHALL adjust only one effective projection factor per instance, applied identically to every node and both predicate terms; independent per-cluster sticky selection is forbidden.

#### Scenario: CPU and GPU select the same cut

- **WHEN** the CPU reference and Vulkan selector process the same payload, camera, world matrix, viewport, threshold, and effective projection factor
- **THEN** their selected cluster ID sets match exactly
- **AND** every selected group transition is complete

#### Scenario: Parent projection uses parent distance

- **WHEN** a child and parent have different conservative bound distances
- **THEN** the self term uses the child node's own group distance
- **AND** the parent term uses the parent group's own distance

#### Scenario: Hysteresis cannot mix cuts

- **WHEN** camera motion crosses a cluster transition repeatedly within the hysteresis band
- **THEN** the instance retains or updates its single effective projection factor as documented
- **AND** no ancestor and descendant from the same path are selected together

### Requirement: Visibility culling SHALL be separate from LOD selection

The culling pass SHALL consume selected cluster records and reject clusters outside the camera frustum or outside a conservatively transformed back-facing normal cone. It SHALL NOT alter the LOD threshold, selected bit, or parent predicate. Non-uniform or sheared transforms that cannot produce a conservative normal cone SHALL disable cone rejection for that instance rather than risk missing geometry. HZB occlusion input SHALL NOT be required.

#### Scenario: Selected but off-screen cluster is culled

- **WHEN** a cluster belongs to the selected DAG cut but its conservative bounds lie outside the frustum
- **THEN** it consumes no visible-cluster record or indirect draw
- **AND** the selected cut used for other clusters is unchanged

#### Scenario: Unsafe cone transform is conservative

- **WHEN** an instance transform cannot conservatively bound the transformed cone angle
- **THEN** frustum culling still runs
- **AND** normal-cone rejection is skipped for that instance

### Requirement: Indirect batches SHALL preserve complete render state and visible-record identity

The renderer SHALL partition indirect work by render pass, graphics pipeline, mesh vertex/index resources, material and descriptor bindings, cull state, and every other draw-varying state. Each emitted indexed argument SHALL have `instanceCount = 1` and a validated per-draw identifier that locates one visible record containing instance, cluster, mesh, and batch identifiers. Cluster vertex shaders SHALL obtain transforms and debug metadata from that record rather than the legacy per-unit world uniform.

#### Scenario: Tank materials remain in separate compatible batches

- **WHEN** wheel, body, and glass materials require different pipeline or descriptor state
- **THEN** their visible clusters are emitted into compatible batches
- **AND** no indirect submission crosses incompatible material or PSO state

#### Scenario: Per-draw ID recovers the correct record

- **WHEN** an indexed indirect command executes with the backend's supported first-instance or draw-ID mechanism
- **THEN** the vertex shader reads exactly the visible record that produced the command
- **AND** the resulting world transform, material, and cluster debug ID match that record

### Requirement: GPU-written draw counts and arguments SHALL respect backend limits

Command generation SHALL write bounded indexed indirect arguments and per-batch GPU counts through storage operations followed by the required barriers. The renderer SHALL honor argument stride, count-buffer alignment, `maxDrawIndirectCount`, storage-range, and native batch-capacity limits, splitting submissions when necessary without changing visible-record identity. Normal rendering SHALL NOT read the draw count back to the CPU.

#### Scenario: Large batch exceeds one submission limit

- **WHEN** a valid batch contains more commands than the backend's maximum indirect draw count
- **THEN** the renderer issues bounded indirect submissions covering the compacted commands
- **AND** every visible cluster is drawn exactly once with the correct record ID

### Requirement: Capacity overflow SHALL move whole instances to legacy rendering deterministically

Render preparation SHALL size selected, visible, argument, and count storage from leaf-cluster-based upper bounds and a documented memory/draw budget. Instances that cannot fit SHALL be moved wholesale to legacy rendering in deterministic snapshot order before GPU submission. Every GPU append SHALL remain bounds-checked and set an overflow diagnostic on invariant failure; partial cluster cuts SHALL NOT be used as an overflow strategy.

#### Scenario: Budget cannot fit another tank instance

- **WHEN** adding the next eligible instance would exceed the configured indirect budget
- **THEN** that complete instance renders through the legacy path
- **AND** earlier accepted instances retain complete cluster cuts
- **AND** statistics report the fallback and no out-of-bounds write occurs

### Requirement: Cluster draws SHALL integrate with the existing deferred GBuffer without duplication

Cluster draws SHALL write the same GBuffer attachments, material decoding, and linear view-depth semantics as legacy opaque static draws. Eligible cluster units SHALL be excluded from legacy direct opaque and initial PreZ loops so they are not drawn twice. Deferred local-light assignment, lighting, sky, and postprocessing SHALL remain unchanged. Initial shadow and secondary-view passes MAY retain their existing whole-mesh geometry path, and the renderer SHALL NOT claim camera cluster selection accelerates them.

#### Scenario: Clustered tank matches LOD-zero GBuffer

- **WHEN** the tank renders at a threshold selecting only LOD-zero clusters and the legacy path renders the same source geometry
- **THEN** their GBuffer and final images agree within the established backend comparison tolerance
- **AND** no unit appears in both legacy and indirect opaque draws

#### Scenario: Clustered lighting remains downstream

- **WHEN** clustered local lighting is enabled with cluster geometry rendering
- **THEN** geometry clusters feed the existing GBuffer
- **AND** light-cluster assignment and deferred shading use their existing inputs without a second lighting model
