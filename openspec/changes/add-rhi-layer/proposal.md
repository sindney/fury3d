# add-rhi-layer

## Why

The engine's renderer is hardwired to OpenGL: raw `gl*` calls are spread across the pipeline code and shaders exist only as runtime-compiled GLSL. macOS GL is frozen at 4.1 (no compute beyond what we emulate, no BC7, deprecated by Apple), and there is no path to Vulkan or Metal. An RHI abstraction (UE-style: one unified API, per-platform backend implementations) unblocks modern backends and, via Slang cross-compilation of the existing GLSL set, keeps a single shader source feeding OpenGL, Vulkan (SPIR-V) and Metal (MSL).

## What Changes

- Introduce a backend-agnostic **RHI layer** (device, textures, buffers, shaders/pipelines, framebuffers/render targets, samplers, command recording/submission, fences) that all engine rendering code targets. **BREAKING** for renderer internals: pipeline code (PrelightPipeline, shadows, sky, ocean, particles, vegetation, postfx, ImGui/RmlUi render backends) stops calling GL directly and goes through the RHI.
- **OpenGLRHI** backend: port of the current GL renderer behind the RHI, preserving current behavior and remaining the fallback backend.
- **VulkanRHI** backend: Vulkan 1.x device, swapchain, pipelines; SPIR-V shaders.
- **MetalRHI** backend (macOS): Metal device, drawable swapchain, pipelines; MSL shaders.
- **Slang shader toolchain**: offline/CLI cross-compile of the existing GLSL shader set to SPIR-V (Vulkan) and MSL (Metal), plus GLSL passthrough for OpenGL. Shader permutation/define model preserved.
- **Startup-time RHI selection** (no runtime switching): `--rhi=opengl|vulkan|metal` arg on `fury`/`furye`, an editor settings option, and a game-side option (island game options menu) listing the backends supported on the current platform (Windows: opengl/vulkan; macOS: opengl/vulkan/metal).
- **Fallback**: if the requested backend fails basic device/init detection, log the error and fall back to OpenGLRHI.
- **Cook/package integration**: cook cross-compiles shaders for every supported RHI by default and packs all of them; a `--rhi` cook arg restricts to one backend. Compiled shaders are cached in the existing DDC under `SHADER_`-prefixed keys (texture DDC keeps `TEX_`), with a master shader-DDC version key for forced invalidation.

## Capabilities

### New Capabilities

- `rhi-abstraction`: the backend-agnostic RHI API surface (device lifecycle, resource creation, pipeline state, command submission, frame pacing) and the contract engine renderers code against.
- `rhi-backend-opengl`: the OpenGL backend behind the RHI, preserving current rendering behavior.
- `rhi-backend-vulkan`: the Vulkan backend (device, swapchain, SPIR-V pipelines, synchronization).
- `rhi-backend-metal`: the Metal backend on macOS (device, CAMetalLayer swapchain, MSL pipelines).
- `shader-cross-compile`: the Slang-based toolchain that compiles the engine GLSL set to SPIR-V and MSL, its permutation handling, and its SHADER_ DDC caching with a master version key.
- `rhi-selection`: startup-time backend selection (`--rhi` arg, editor option, game option), per-platform support matrix, and OpenGL fallback on init failure.

### Modified Capabilities

- `asset-cooking`: cook gains shader discovery + per-RHI cross-compilation, `SHADER_`-prefixed DDC entries, and a `--rhi` option restricting packed backends (default: all supported).
- `cli`: `fury` gains a global `--rhi=<backend>` option on engine-boot paths (launcher, `exec`, pak boot).

## Impact

- **Code**: new `engine/Fury/RHI/` module; all files with raw GL calls (PrelightPipeline, Pipeline shadow rendering, SkyAtmosphere, ocean, particles, vegetation/instancing, postfx, gbuffer, texture loading, ImGui + RmlUi render backends) migrate to the RHI; window/context creation (SFML window) becomes per-backend surface creation.
- **Render thread**: RHI commands execute on the existing render thread; the command-queue boundary stays, with GL confined behind OpenGLRHI.
- **Dependencies**: new vendored deps — Slang (shader cross-compile), Vulkan SDK headers + volk-style loader (or MoltenVK runtime on macOS), Metal (system frameworks, Objective-C++ translation units on macOS).
- **Build**: CMake options per backend (e.g. `FURY_RHI_VULKAN=ON`, `FURY_RHI_METAL=ON` on macOS); OpenGL always built as fallback.
- **Cook/package**: `furye-cli cook` gains shader cross-compile + `--rhi`; paks grow per-backend shader blobs; DDC gains `SHADER_` keyspace + master version.
- **Editor/game UX**: settings option for RHI (restart to apply); island game options menu entry.
- **Out of scope**: runtime RHI switching, DX12/DX11 backends, ray tracing.
- **Coordination note**: a concurrent agent is fixing a vegetation popping visual bug in the GL pipeline; this change's GL code motion must rebase cleanly over it (behavior-preserving port).
