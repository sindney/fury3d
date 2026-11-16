## ADDED Requirements

### Requirement: The RHI SHALL represent backend-neutral indexed indirect batches

The RHI SHALL define a canonical indexed indirect argument layout containing `indexCount`, `instanceCount`, `firstIndex`, `vertexOffset`, and `firstInstance`, plus an indirect batch abstraction containing argument storage, optional GPU count storage, maximum capacity, graphics pipeline, and batch bindings. Command recording SHALL provide a backend preparation step and indexed indirect execution. Backend preparation SHALL convert the neutral representation when required; engine code outside RHI backends SHALL NOT call native indirect APIs.

#### Scenario: Shared renderer records an indirect batch

- **WHEN** the cluster renderer prepares and records a valid indirect batch
- **THEN** it uses only RHI buffer, pipeline, binding, preparation, and execution APIs
- **AND** Vulkan and Metal backends receive the same neutral argument semantics

### Requirement: Buffer usage SHALL support GPU-generated indirect data and diagnostic copies

RHI buffer creation SHALL allow storage-compatible indirect argument and count buffers and transfer-source/copy usage without replacing existing buffer types. The RHI SHALL provide the copies and barriers needed for GPU-generated counters, arguments, and readback. Per-dispatch counter changes SHALL NOT use an `UpdateBuffer` path that reallocates the underlying native buffer.

#### Scenario: Compute writes arguments consumed by a draw

- **WHEN** a compute dispatch writes indirect arguments and counts followed by indirect execution
- **THEN** the RHI records the required storage-to-indirect barrier
- **AND** the native buffer has both storage-compatible and indirect usage
- **AND** no CPU buffer reallocation occurs between dispatch and draw

### Requirement: The RHI SHALL report indirect and per-draw-ID capabilities

The capability interface SHALL report indirect-count support, multi-draw support, nonzero first-instance support, shader draw-ID support, native indirect batch encoding, maximum indirect draw count, storage-buffer limits, and applicable argument/count alignment restrictions. A capability SHALL be true only after the backend has enabled and validated the corresponding native feature. Unsupported combinations SHALL cause an explicit renderer fallback.

#### Scenario: Capability report controls cluster activation

- **WHEN** a device supports multi-draw indirect but lacks both nonzero first instance and shader draw ID
- **THEN** the capability report exposes that combination
- **AND** the cluster renderer reports itself unavailable instead of binding incorrect visible records

### Requirement: The RHI SHALL provide GPU buffer readback for diagnostics

The RHI SHALL provide buffer readback with explicit size, offset, frame completion, and lifetime semantics. Readback SHALL execute through render-thread command and synchronization mechanisms and SHALL return the requested bytes or an error. The API SHALL be used for tests and on-demand diagnostics, not as an implicit step of indirect execution.

#### Scenario: Automation reads indirect counters

- **WHEN** automation requests the count buffer associated with a completed frame
- **THEN** the RHI returns the counter bytes after the required GPU synchronization
- **AND** the values remain associated with the requested frame

### Requirement: Indirect resources SHALL obey render-thread and in-flight lifetime rules

Indirect arguments, counts, visible records, native command buffers, conversion resources, and readback allocations SHALL be created and consumed on the render thread and retained until all referencing GPU work completes. Backend preparation and execution SHALL NOT capture pointers to temporary game-thread packet storage.

#### Scenario: Packet ends after submission

- **WHEN** a frame packet is released after recording indirect work
- **THEN** every native resource needed by that work remains valid until GPU completion
- **AND** subsequent packets cannot overwrite its arguments or counts prematurely
