# rhi-backend-opengl

## Purpose

The OpenGL RHI backend: the existing GL renderer ported behind the RHI interfaces with no behavior change, always built and always available as the fallback backend.

## Requirements

### Requirement: OpenGLRHI SHALL implement the RHI over the existing OpenGL renderer

The OpenGL backend SHALL port the current GL renderer behind the RHI interfaces with no behavior change: SFML-created core-profile context (negotiated 4.6 down to 3.3 as today), the custom GL loader, runtime GLSL source compilation, name-based uniforms via `glGetUniformLocation` (including the location-cached path), and the existing SSBO/divisor-VBO dual path gated on GL 4.3. It SHALL be built on every platform and SHALL always be available as the fallback backend.

#### Scenario: Full test suite passes under OpenGLRHI

- **WHEN** the headless Lua test suite runs with `--rhi=opengl` after the migration
- **THEN** every test that passed before the migration passes, with equivalent rendered output

#### Scenario: GL 4.3 SSBO path preserved

- **WHEN** instanced vegetation renders on a GL 4.3+ context
- **THEN** the SSBO instance path is used, and on a GL 3.3-4.2 context the divisor-VBO path is used, exactly as before

### Requirement: OpenGLRHI SHALL present through the SFML window

The OpenGL swapchain SHALL wrap the existing `sf::Window` GL context and `display()` call, preserving current vsync behavior.

#### Scenario: Window creation unchanged for OpenGL

- **WHEN** the engine boots with `--rhi=opengl`
- **THEN** the SFML window and GL context are created exactly as before, including macOS retina backing-scale handling

### Requirement: OpenGLRHI SHALL consume GLSL shader sources

The OpenGL backend SHALL compile shaders from GLSL source at load time (directly or from DDC-cached source entries), preserving the stage-split injected defines (`VERTEX_SHADER`/`FRAGMENT_SHADER`/...), the `#include` preprocessor, and JSON-declared permutation defines.

#### Scenario: Permutation defines produce distinct programs

- **WHEN** the gbuffer shader is compiled with and without `SKINNED_MESH`
- **THEN** two distinct programs result, as today
