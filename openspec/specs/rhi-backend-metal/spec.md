# rhi-backend-metal

## Purpose

The Metal RHI backend on macOS: CAMetalLayer swapchain, load-time MSL compilation, shadow-constant-buffer uniforms, and output parity with the OpenGL backend.

## Requirements

### Requirement: MetalRHI SHALL implement the RHI over Metal on macOS

The Metal backend SHALL provide device creation, a `CAMetalLayer` swapchain attached to the SFML window's `NSView`, command buffers/encoders recorded from RHI command lists, and pipeline states from MSL shader libraries plus RHI pipeline state. Metal sources SHALL live in Objective-C++ translation units compiled only on Apple platforms.

#### Scenario: Metal boots and renders on macOS

- **WHEN** the engine starts with `--rhi=metal` on macOS
- **THEN** the engine logs the selected `metal` backend and renders frames through the CAMetalLayer swapchain

### Requirement: MetalRHI SHALL compile MSL source at load time

The Metal backend SHALL create shader libraries from MSL source text produced by the cross-compiler (cook/DDC products), letting the OS cache compiled variants. Loose uniforms SHALL be emulated via a shadow constant buffer and the reflection metadata as specified in `rhi-abstraction`.

#### Scenario: First load compiles, second load is cached

- **WHEN** a Metal game boots twice with unchanged shaders
- **THEN** the second boot loads shader libraries faster via the OS shader cache (no cross-compile runs in either boot)

### Requirement: MetalRHI SHALL match OpenGL output conventions

The Metal backend SHALL use [0,1] NDC depth and correct Y handling so rendered images match OpenGL output orientation, including `gl_FragDepth`-equivalent depth writes, shadow comparison samplers, 3D textures, and texture arrays. The vegetation PreZ depth-equality requirement SHALL hold.

#### Scenario: Island scene parity under Metal

- **WHEN** the ocean island scene renders under `--rhi=opengl` and `--rhi=metal` from the same camera
- **THEN** reference screenshots agree within tolerance (sky, ocean, terrain, vegetation all present and correctly composited)
