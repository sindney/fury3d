## ADDED Requirements

### Requirement: Vulkan SHALL query and enable the features required for cluster MDI

The Vulkan backend SHALL query and enable `multiDrawIndirect`, `drawIndirectFirstInstance`, shader draw parameters, and `drawIndirectCount` when supported, and SHALL expose `maxDrawIndirectCount`, argument stride, storage limits, and count-buffer requirements through RHI capabilities. Device creation SHALL NOT require unrelated optional features for legacy rendering. MoltenVK SHALL be handled through the same queries without engine-side device-name special cases.

#### Scenario: MoltenVK reports its actual feature set

- **WHEN** the engine initializes Vulkan through MoltenVK
- **THEN** the logged capabilities reflect the queried and enabled features and limits
- **AND** cluster activation uses those values rather than assuming desktop Vulkan support

### Requirement: Vulkan SHALL execute indexed MDI with GPU-written counts

For a prepared indirect batch, Vulkan SHALL record `vkCmdDrawIndexedIndirectCount` or its supported extension equivalent using the canonical argument layout, validated stride, bounded maximum draw count, and GPU count buffer. Batches exceeding `maxDrawIndirectCount` SHALL be split into valid submissions. Devices without indirect-count support are unsupported for the cluster path and use legacy rendering.

#### Scenario: GPU count controls the submitted draws

- **WHEN** compute compacts a tank batch to N commands and writes N into the count buffer
- **THEN** Vulkan executes exactly N indexed indirect commands without a CPU count readback
- **AND** commands beyond N in the allocated capacity are not executed

### Requirement: Vulkan per-draw identification SHALL be correct and capability-gated

The preferred path SHALL encode the visible-record index in `firstInstance` and consume it through validated shader base-instance semantics. An alternative path MAY use `gl_DrawID` plus a batch offset when shader draw parameters and translation are validated. Nonzero indirect first instance SHALL be used only when `drawIndirectFirstInstance` is enabled. If neither mechanism works, the cluster renderer SHALL be unavailable and legacy rendering SHALL continue.

#### Scenario: Base instance selects the visible record

- **WHEN** two compacted Vulkan commands reference different visible records through nonzero `firstInstance`
- **THEN** each vertex shader invocation reads the record selected by its own command
- **AND** SPIR-V reflection/translation and rendered output confirm the IDs are not both zero

### Requirement: Vulkan SHALL synchronize and retain GPU-generated indirect resources

The Vulkan backend SHALL add indirect and transfer usage to native buffer allocation, record compute-write to indirect-read and readback barriers, and retain argument, count, visible-record, and readback buffers through submitted GPU work. Count and argument generation SHALL NOT rely on host-visible `UpdateBuffer` reallocation.

#### Scenario: Capture shows the required dependency

- **WHEN** a Vulkan cluster frame is captured with gfxrecon
- **THEN** command generation completes before indirect consumption under the recorded barriers
- **AND** replay observes valid initialized arguments and counts
- **AND** no retired allocation is reused by the captured frame

### Requirement: Vulkan cluster validation SHALL include semantic and capture checks

The Vulkan backend SHALL pass focused indirect API tests, CPU/GPU selected-set comparisons, tank GBuffer image comparisons, and gfxrecon capture/replay checks with validation enabled where available. Tests SHALL verify multi-draw execution, GPU-written counts, and the selected per-draw-ID mechanism rather than inferring them from a final image alone.

#### Scenario: Indirect feature smoke test proves MDI

- **WHEN** the focused Vulkan test emits multiple known indexed commands and a GPU-written count
- **THEN** readback or image output proves every expected command executed exactly once
- **AND** the capture identifies the indirect-count call and its arguments
