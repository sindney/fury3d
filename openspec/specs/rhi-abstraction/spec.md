# rhi-abstraction

## Purpose

The backend-agnostic rendering hardware interface that all engine and editor rendering goes through: device/resource/command-list abstractions, name-based uniform binding via reflection, render-thread confinement, GPU readbacks, editor GPU capture visualization, and per-backend capability reporting.

## Requirements

### Requirement: The engine SHALL route all rendering through a backend-agnostic RHI API

All engine and editor rendering SHALL go through the RHI interfaces in `engine/Fury/RHI/`: a global `RHIDevice` plus ref-counted resource handles (`RHITexture`, `RHIBuffer`, `RHISampler`, `RHIShaderModule`, `RHIGraphicsPipeline`, `RHIRenderTargetSet`) and an `RHICommandList` for draw/dispatch/blit/readback work. Engine and editor code outside `engine/Fury/RHI/` backends SHALL NOT call platform graphics APIs (OpenGL, Vulkan, Metal) directly.

#### Scenario: No raw GL calls outside the OpenGL backend

- **WHEN** the migration phases complete and `engine/Fury` is searched for direct `gl*` calls
- **THEN** matches exist only inside the OpenGLRHI backend sources and the GL loader

#### Scenario: Engine enums are the canonical state model

- **WHEN** pipeline state (compare mode, blend mode, cull mode, texture format) is specified by engine code
- **THEN** it uses engine enums from the RHI headers, and each backend maps them to its API constants internally

### Requirement: Resource identities SHALL be as wide as the widest backend handle

A GPU resource identity is not a number: on OpenGL it is a 32-bit GL name, on Metal it is the 64-bit `MTL::Texture*` / `MTL::Buffer*` itself, and on Vulkan it is a 64-bit handle. Any value that identifies a resource and crosses a layer boundary (UI toolkit, Lua binding, editor cache, serialized state) SHALL be carried in a `uintptr_t`-wide type. Narrowing a native handle to a narrower type SHALL happen only inside the backend whose API demands it.

#### Scenario: Editor image on a pointer-handle backend

- **WHEN** the editor draws a texture or a thumbnail in a UI toolkit while the active backend's native handle is a 64-bit pointer
- **THEN** the full handle reaches the toolkit and the draw binds the intended texture

#### Scenario: A 32-bit holder is a latent cross-backend crash

- **WHEN** an engine-level accessor, field, or parameter that carries a resource identity is declared
- **THEN** its type is at least as wide as `uintptr_t`, so a narrowing to a backend's native width cannot happen outside the backend

### Requirement: The RHI SHALL expose a device lifecycle with explicit frame boundaries

The `RHIDevice` SHALL be created once at engine startup for the selected backend, SHALL own swapchain/present for the main window, and SHALL provide `BeginFrame`/`EndFrame`/`Present` boundaries. GPU object destruction SHALL be deferred until the GPU is done with the resource (per-backend retirement).

#### Scenario: Frame lifecycle

- **WHEN** the engine renders a frame on any backend
- **THEN** all command lists for that frame execute between `BeginFrame` and `Present`, and `Present` displays the swapchain image

#### Scenario: Safe resource destruction

- **WHEN** a texture is released by engine code while a submitted frame still references it
- **THEN** the backend defers the native destruction until the frame has completed on the GPU

### Requirement: The RHI SHALL support the engine's existing resource and pass model

The RHI SHALL express everything the current renderer uses: 2D/3D/cube/array textures with mipmaps and the engine's texture formats (including BC-compressed KTX2 uploads), vertex/index/uniform/storage buffers, samplers with the engine's filter/wrap modes, render target sets with multiple color attachments plus depth, layered rendering (cube faces, array slices), fullscreen blits between targets, instanced and indexed draws, and compute dispatch with barriers.

#### Scenario: Gbuffer pass runs through the RHI

- **WHEN** the deferred gbuffer pass executes on any backend
- **THEN** it binds a render target set with color attachments plus depth and records instanced/indexed draws through the command list

#### Scenario: Shadow map rendering runs through the RHI

- **WHEN** CSM and point-cube shadow passes execute
- **THEN** layered/array-slice depth targets render through the RHI without engine-side API-specific code

### Requirement: The RHI SHALL provide a name-based uniform interface with reflection-driven binding

Engine code SHALL continue to set shader uniforms by name. Backends that lack loose uniforms SHALL implement them via a CPU-side constant-buffer shadow driven by compiler reflection metadata (name to offset/binding map) stored with the compiled shader. Texture bindings by name SHALL follow the same reflection metadata.

#### Scenario: Uniform set by name on a reflection-driven backend

- **WHEN** engine code sets uniform `world_matrix` on a Vulkan pipeline
- **THEN** the value lands at the reflection-reported offset in the shadow constant buffer and is visible to the shader on the next draw

#### Scenario: Missing uniform is tolerated

- **WHEN** engine code sets a uniform name the compiled shader does not contain (optimized out)
- **THEN** the set is a no-op and no error is raised

### Requirement: RHI commands SHALL execute on the render thread

All RHI device, resource, and command-list calls SHALL be confined to the engine render thread; off-thread resource work SHALL be funneled through the existing dispatch mechanism (generalized from `DispatchGL`). The `FramePacket` gather model SHALL remain unchanged. A debug thread-guard SHALL log violations.

#### Scenario: Off-thread texture upload is dispatched

- **WHEN** a texture upload is requested from the game thread while the render thread runs
- **THEN** the upload executes on the render thread via the dispatch queue, as GL uploads do today

### Requirement: The RHI SHALL provide GPU readbacks for editor and tooling

The RHI SHALL expose a readback API that copies a render target or texture region to CPU memory, completing via fence/stall. Editor picking, screenshots, thumbnails, and CLI headless captures SHALL use it instead of API-specific calls.

#### Scenario: Picking readback

- **WHEN** the editor performs a viewport pick on any backend
- **THEN** the pixel data comes from the RHI readback API and picking behaves identically across backends

### Requirement: The editor SHALL visualize GPU frame captures

The editor SHALL open `--gpu-dump` capture directories in a GPU Capture Profiler window, showing an interactive pass/postprocess node graph, pass details, and per-texture previews and statistics. The window SHALL also open from `furye Editor.lua --gpu-profiler <dir>`.

#### Scenario: Open a capture from the command line

- **WHEN** the editor starts with `--gpu-profiler` naming a directory containing `rendergraph.json`
- **THEN** the GPU Capture Profiler displays its pass graph and texture inventory without re-running the capture

### Requirement: The RHI SHALL report per-backend capabilities and conventions

The RHI SHALL expose capability queries: NDC depth convention (z in [-1,1] vs [0,1]), clip-space Y orientation, maximum texture sizes/array layers, SSBO support, and compressed-format support. Projection matrix construction SHALL consume these conventions so shaders need no per-backend source changes.

#### Scenario: Projection matches backend convention

- **WHEN** a perspective projection is built while the Vulkan backend is active
- **THEN** the matrix uses z in [0,1] and the Y orientation the backend reports, and rendered geometry is not flipped
