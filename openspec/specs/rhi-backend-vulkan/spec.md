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
