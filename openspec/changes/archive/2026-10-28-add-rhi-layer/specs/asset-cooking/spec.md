# asset-cooking (delta for add-rhi-layer)

## ADDED Requirements

### Requirement: Cook SHALL discover shaders from the scene's pipeline JSONs and cross-compile them per RHI backend

Cook SHALL enumerate the shader entries and their define permutations from the pipeline JSON referenced by the scene's render settings (plus the engine default pipeline) and SHALL cross-compile each (shader, permutation) for every backend in scope through the shader compiler interface. Cross-compilation SHALL run headless (no GPU device). Compile failures SHALL be reported by shader, permutation, and backend, and SHALL fail the cook.

#### Scenario: Cook emits per-backend shader blobs

- **WHEN** `furye-cli cook scene.bin` runs for a scene using the deferred pipeline
- **THEN** the cook report lists every pipeline shader entry with a compiled blob per in-scope backend (SPIR-V for vulkan, MSL source for metal, GLSL for opengl)

#### Scenario: Shader compile failure fails the cook

- **WHEN** a referenced shader fails to cross-compile for the metal target
- **THEN** cook names the shader, permutation, and backend and exits non-zero

### Requirement: Cook SHALL accept a `--rhi` option restricting which backends shaders are packed for

Cook and package SHALL accept `--rhi=<list>` (comma-separated subset of `opengl,vulkan,metal`). When omitted, shaders SHALL be cooked for all backends supported on the package target platform. The option SHALL NOT affect texture cooking.

#### Scenario: Default cooks all backends

- **WHEN** cook runs on a macOS host for a macOS package without `--rhi`
- **THEN** shader blobs for opengl, vulkan, and metal are produced

#### Scenario: Restricted to one backend

- **WHEN** cook runs with `--rhi vulkan`
- **THEN** only SPIR-V shader blobs are produced and recorded in the manifest

### Requirement: The cook manifest SHALL record per-backend shader artifacts

The manifest SHALL map each (shader, permutation) to its cooked blob per backend plus the reflection sidecar. The package step SHALL pack these blobs, and the runtime SHALL load the blob matching the active RHI backend.

#### Scenario: Manifest maps shader permutations to blobs

- **WHEN** cook completes with two backends in scope
- **THEN** the manifest contains, per shader permutation, one entry per backend pointing at the blob and reflection files

## MODIFIED Requirements

### Requirement: Cook SHALL cache cooked output in a Derived Data Cache (DDC) keyed by content and settings

Cook SHALL compute a DDC key as a hash over: a namespaced type prefix (`TEX_` for textures, `SHADER_` for shaders), the source file's content hash, the cook settings (for textures: usage flags, target format; for shaders: backend, define set, compiler version, and the shader master version constant), and the cooker/tool version string. The DDC SHALL live in a `DDC/` folder next to the executing binary (overridable via option/env). DDC entries SHALL be stored in hash-prefix subfolders (`DDC/<hex[0:2]>/<hex[2:4]>/<full-hash>.<ext>`) so no folder holds a large number of files. On a DDC hit the cached artifact SHALL be reused without invoking the compressor/compiler; on a miss cook SHALL produce and then write the entry. A change to source content, settings, type prefix, or tool version SHALL produce a different key and therefore a recook. Existing `TEXTURE`-tagged entries are orphaned by the `TEX_` rename and simply recook once.

#### Scenario: Second cook is a cache hit

- **WHEN** cook runs twice against the same unchanged scene
- **THEN** the second run performs no `ktx` or shader-compiler invocations
- **AND** reports all assets as DDC hits

#### Scenario: Editing a texture invalidates its entry only

- **WHEN** one source texture file changes between two cooks
- **THEN** only that texture is recompressed
- **AND** all other assets are DDC hits

#### Scenario: Switching cook target recooks

- **WHEN** cook runs with `--texture-target legacy` then `--texture-target modern`
- **THEN** the second run recompresses every texture (different keys) and stores separate DDC entries

#### Scenario: DDC folder uses hash-prefix subfolders

- **WHEN** a DDC entry with hex key `3fa9c1...` is written
- **THEN** the file lands at `DDC/3f/a9/3fa9c1....<ext>`

#### Scenario: Texture and shader entries do not collide

- **WHEN** a texture and a shader happen to hash identically in content and settings
- **THEN** their keys still differ because of the `TEX_` vs `SHADER_` prefix
