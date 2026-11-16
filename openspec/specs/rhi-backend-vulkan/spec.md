# rhi-backend-vulkan

## Purpose

The Vulkan RHI backend: instance/device/swapchain over the SFML window, SPIR-V shader modules with reflection-driven binding, clip-space and sampler conventions matching OpenGL output, and MoltenVK support on macOS via capability queries.

## Requirements

### Requirement: VulkanRHI SHALL implement the RHI over Vulkan

The Vulkan backend SHALL provide instance/device creation with the extensions required for swapchain and the engine's feature set, a swapchain over the SFML window (`sf::WindowBase` + SFML Vulkan surface helpers), per-frame command buffers recorded from RHI command lists, graphics pipelines from SPIR-V shader modules plus RHI pipeline state, and fences/semaphores for frame pacing and readbacks. Debug builds SHALL enable the Vulkan validation layers when available.

#### Scenario: Vulkan boots and renders

- **WHEN** the engine starts with `--rhi=vulkan` on a Vulkan-capable Windows/Linux machine (or macOS with MoltenVK present)
- **THEN** the engine logs the selected `vulkan` backend and renders frames through the Vulkan swapchain

#### Scenario: Validation layers in debug

- **WHEN** a debug build runs with `--rhi=vulkan` and validation layers are installed
- **THEN** validation is enabled and a deliberate misuse is reported in the log

### Requirement: VulkanRHI SHALL load SPIR-V shader blobs

The Vulkan backend SHALL create shader modules from precompiled SPIR-V blobs (cook/DDC products), using the accompanying reflection metadata for uniform and texture binding. Loose uniforms SHALL be emulated via a shadow constant buffer and per-draw descriptor updates as specified in `rhi-abstraction`.

#### Scenario: SPIR-V blob renders identically to GLSL source

- **WHEN** the same scene renders under `--rhi=opengl` (GLSL) and `--rhi=vulkan` (SPIR-V)
- **THEN** reference screenshots of the same camera/frame agree within tolerance (no missing geometry, no inverted image, matching depth)

### Requirement: VulkanRHI SHALL handle clip-space and sampler conventions correctly

The Vulkan backend SHALL use [0,1] NDC depth and correct viewport/projection Y handling so rendered images match the OpenGL output orientation, including shaders that write `gl_FragDepth`, shadow comparison samplers (sampler2DArrayShadow, samplerCubeShadow), 3D textures, and texture arrays. The vegetation PreZ depth-equality requirement SHALL hold (equal-depth fragments pass the equal test).

#### Scenario: PreZ vegetation draws under Vulkan

- **WHEN** a vegetation-heavy scene renders with PreZ enabled under Vulkan
- **THEN** instanced geometry and billboards draw correctly with no depth-fighting or missing canopies

#### Scenario: Ocean surface renders under Vulkan

- **WHEN** the ocean island scene renders under Vulkan
- **THEN** the `gl_FragDepth`-writing ocean surface composites correctly with terrain and sky

### Requirement: VulkanRHI SHALL support MoltenVK on macOS without special-casing engine code

On macOS the Vulkan backend SHALL run over a vendored MoltenVK ICD when present. MoltenVK limitations SHALL be treated as capability query results; if the ICD is absent the backend SHALL fail probing so the selection layer falls back.

#### Scenario: Missing MoltenVK falls back

- **WHEN** `--rhi=vulkan` is requested on macOS without the MoltenVK runtime
- **THEN** the error is logged and the engine falls back to `opengl`

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
