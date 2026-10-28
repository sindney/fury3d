# shader-cross-compile

## Purpose

Cross-compilation of the engine's GLSL shader set through a vendored Slang compiler behind a pluggable `IShaderCompiler` interface: SPIR-V/MSL/GLSL targets with reflection sidecars, strict-portable source requirements, DDC caching under the SHADER_ keyspace, and runtime blob loading with OpenGL source fallback.

## Requirements

### Requirement: The engine SHALL vendor Slang and cross-compile the GLSL shader set through a pluggable compiler interface

The repo SHALL vendor the Slang compiler (prebuilt binaries + headers, ktx precedent) and CMake SHALL stage it next to the binaries. All cross-compilation SHALL go through an `IShaderCompiler` interface taking a preprocessed GLSL translation unit plus a define set and emitting, per target: SPIR-V (Vulkan), MSL source text (Metal), GLSL source (OpenGL passthrough), and reflection metadata (uniform name to offset/binding map). The interface SHALL allow a glslang+SPIRV-Cross implementation as a drop-in contingency without changing callers.

#### Scenario: One source, three targets

- **WHEN** the compiler processes `Lambert/GBuffer.glsl` with the `SKINNED_MESH` define
- **THEN** it emits a SPIR-V blob, an MSL source blob, a GLSL source blob, and reflection metadata, all for the same permutation

#### Scenario: Reflection metadata covers uniforms and textures

- **WHEN** reflection metadata is emitted for the gbuffer shader
- **THEN** it contains entries for `world_matrix`, `bone_matrices`, and the material texture samplers with their offsets/bindings

### Requirement: The shader set SHALL be made strict-portable before backend bring-up

The GLSL sources SHALL avoid constructs the compiler rejects while preserving the one-file-all-stages injected-define model, the `#include` preprocessor, JSON-declared permutation defines, and GLSL default-uniform semantics. A `furye-cli shaderc` mode SHALL compile every shader entry x define permutation in the engine's pipeline JSONs for all three targets and report failures by shader, permutation, and target.

#### Scenario: Full permutation matrix compiles

- **WHEN** `furye-cli shaderc` runs over the deferred and PBR pipeline JSONs
- **THEN** every shader entry compiles for opengl, vulkan, and metal targets, or the command exits non-zero naming each failing (shader, permutation, target)

#### Scenario: Uniform initializer survives cross-compilation

- **WHEN** a GLSL uniform has a scalar, integer, or vector initializer (e.g. `uniform float ambient_factor = 1;`)
- **THEN** the compiler records its bytes in the reflection sidecar and reflection-driven backends seed the CPU constant-buffer shadow with that value before any explicit bind

#### Scenario: Packaged Metal game loads MSL

- **WHEN** a packaged game boots with `--rhi=metal` and no loose DDC or compiler
- **THEN** shader modules and their reflection sidecars load from the pak and rendering completes without invoking Slang

### Requirement: Reflection SHALL define a validated uniform contract

The reflection sidecar SHALL carry a schema version, per-stage globals block name/binding/size, member offsets/sizes, GLSL default bytes, and vertex stage inputs. Compilation SHALL reject unexpected uniform blocks and members whose ranges exceed the block. Deferred name-based uniform handles SHALL be stable and deduplicated.

#### Scenario: Default seeds the Metal shadow

- **WHEN** a Metal module is adopted without an explicit bind for a default-initialized uniform
- **THEN** its stage shadow contains the sidecar's default bytes rather than an unconditional zero fill

### Requirement: Compiled shaders SHALL be cached in the DDC under a SHADER_ keyspace with a master version

Cross-compiled shader blobs SHALL be cached in the existing content-addressed DDC using the `SHADER_` type prefix (textures use `TEX_`). The shader DDC settings string SHALL include the target backend, the define set, the compiler version, and a `FURY_SHADER_DDC_MASTER_VERSION` constant; bumping the master version SHALL invalidate all shader entries without touching texture entries. Blobs SHALL be stored in the same hash-prefix folder layout as textures, with the reflection sidecar alongside.

#### Scenario: Shader DDC hit skips compilation

- **WHEN** a shader permutation is requested twice with unchanged source and settings
- **THEN** the second request is served from the DDC without invoking the compiler

#### Scenario: Master version bump flushes shaders only

- **WHEN** `FURY_SHADER_DDC_MASTER_VERSION` is incremented and a shader is requested again
- **THEN** the shader recompiles (new key) while previously cooked texture entries remain DDC hits

### Requirement: The runtime SHALL load backend-appropriate shader blobs with source fallback for OpenGL

At runtime the active RHI SHALL load its blob format (SPIR-V/MSL/GLSL) from the pak or loose files. In editor/development runs, a missing blob SHALL be produced on demand through the compiler + DDC. The OpenGL backend SHALL retain the direct GLSL source compile path as the ultimate fallback.

#### Scenario: Packaged Vulkan game loads SPIR-V

- **WHEN** a packaged game boots with `--rhi=vulkan`
- **THEN** shader modules load from SPIR-V blobs in the pak and no GLSL compilation occurs

#### Scenario: Editor shader edit recompiles through DDC

- **WHEN** a shader source file is edited and the editor (running Vulkan or Metal) next loads it
- **THEN** the shader is recompiled through the compiler, cached in the DDC, and the new blob is used
