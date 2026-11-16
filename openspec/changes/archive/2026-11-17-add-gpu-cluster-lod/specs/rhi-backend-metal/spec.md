## ADDED Requirements

### Requirement: Metal SHALL encode native indexed indirect command buffers from neutral arguments

The Metal backend SHALL prepare an indirect batch by converting canonical `RHIIndirectDrawArgs` into GPU-encoded `MTLIndirectCommandBuffer` indexed commands. An internal Metal-owned MSL kernel SHALL encode pipeline state, index and vertex buffers, and required batch bindings. Platform command types and native API calls SHALL remain inside the Metal backend. Each command's `baseInstance` SHALL identify the visible record through validated MSL shader semantics.

#### Scenario: Shared compute output becomes Metal ICB commands

- **WHEN** shared selection/culling compute emits neutral tank arguments and the Metal batch is prepared
- **THEN** the Metal-owned encoder writes one indexed ICB command per argument
- **AND** each command carries the correct index range, pipeline, buffers, and visible-record `baseInstance`

### Requirement: Metal SHALL execute a GPU-written command range

Where supported, Metal SHALL execute the prepared ICB using an indirect execution-range buffer written on the GPU. The range SHALL contain the compacted command count and SHALL NOT execute the full allocated capacity with empty commands. Devices without the required ICB encoding, indexed draw, base-instance, or indirect-range support SHALL disable the GPU cluster path and use legacy rendering.

#### Scenario: GPU range matches compacted count

- **WHEN** Metal command generation compacts a batch to N commands and writes the execution range
- **THEN** the render encoder executes exactly those N commands
- **AND** no CPU count readback or full-capacity empty-command execution occurs

### Requirement: Metal SHALL declare ICB resource usage and ordering explicitly

Metal batch preparation and execution SHALL make compute writes visible to ICB execution, declare resources read or written by indirect commands, and retain buffers, pipelines, argument resources, and execution-range storage until GPU completion. Render-pass transitions SHALL preserve the existing GBuffer attachment and linear-depth contracts.

#### Scenario: ICB resources survive submission

- **WHEN** a Metal cluster frame is submitted and the next frame begins
- **THEN** every resource referenced by its ICB commands remains valid until completion
- **AND** command generation is ordered before execution
- **AND** the GBuffer receives the expected cluster geometry without stale bindings

### Requirement: Metal SHALL pass cluster parity tests after Vulkan validation

The Metal implementation SHALL run the same selected-set, tank image, draw/triangle statistics, fallback, cook/pak, and debug-view tests as Vulkan. Metal validation SHALL additionally inspect ICB encoding, base-instance translation, GPU execution ranges, and resource lifetime behavior. Vulkan success SHALL NOT be treated as evidence of Metal correctness.

#### Scenario: Tank parity on native Metal

- **WHEN** the tank sample runs with matching camera and threshold on Vulkan and native Metal
- **THEN** selected cluster sets match the CPU reference on both backends
- **AND** final images agree within the established cross-backend tolerance
- **AND** Metal statistics confirm indexed ICB execution with a GPU-written range
