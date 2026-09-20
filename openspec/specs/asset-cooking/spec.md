# asset-cooking

## Purpose

The cook step of the cook/package pipeline: discovering cookable assets from a scene file, mapping each texture to a block-compressed format by usage and cook target, compressing through the vendored KTX-Software `ktx` CLI with mipmaps, caching cooked output in a content-keyed Derived Data Cache, and emitting the cook manifest the package step consumes.

## Requirements

### Requirement: The cook step SHALL discover cookable assets from a scene file

The cook command SHALL accept a scene file (`.json` or `.bin`), load it, and enumerate every referenced asset that requires cooking. Textures SHALL be collected from the scene's texture registry (the path-keyed `EntityManager` entries, i.e. every unique canonical texture path the scene references). Non-texture assets (meshes, audio, scripts, shader files) SHALL be collected as passthrough assets that need no cooking. Assets that fail to resolve on disk SHALL be reported by path and SHALL NOT silently be skipped from the report.

#### Scenario: Cook enumerates scene textures

- **WHEN** `furye-cli cook scene.bin` runs against a scene referencing `TerrainIsland/snow.png`, `TerrainIsland/rock.png` and one mesh
- **THEN** the cook report lists exactly the two unique texture paths as cookable
- **AND** the mesh is listed as passthrough (no cooking)

#### Scenario: Duplicate references cook once

- **WHEN** a scene references the same texture path from multiple materials
- **THEN** the texture is cooked exactly once

#### Scenario: Missing source asset is reported

- **WHEN** a scene references `textures/gone.png` and the file does not exist on disk
- **THEN** the cook report names `textures/gone.png` as unresolved
- **AND** the cook command exits with a non-zero code

### Requirement: Cook SHALL map each texture to a block-compressed format by usage and cook target

Cook SHALL accept a `--texture-target` option with values `legacy` and `modern`. The format mapping SHALL be:

| Texture usage | `legacy` target | `modern` target |
| --- | --- | --- |
| Color, no alpha | BC1 | BC7 |
| Color + alpha | DXT5 (BC3) | BC7 |
| Normal map | BC5 | BC5 |
| HDR | uncompressed HDR | uncompressed HDR (BC6H blocked on tool support: the pinned ktx 4.4.2 CLI has no uastc-hdr codec and no bc6hu transcode target; the mapping gains BC6H-unsigned when the toolchain does) |

Texture usage SHALL be derived from the texture's cook settings (sRGB flag, normal-map flag, HDR source format). When the host is macOS, the default target SHALL be `legacy` (macOS GL caps at 4.1 with no BPTC support); on other hosts the default SHALL be `modern`. An explicit `--texture-target` always overrides the host default.

#### Scenario: Legacy mapping on macOS by default

- **WHEN** cook runs on macOS without `--texture-target`
- **THEN** a color texture with alpha cooks to DXT5
- **AND** a normal map cooks to BC5

#### Scenario: Modern mapping when requested

- **WHEN** cook runs with `--texture-target modern`
- **THEN** a color texture with alpha cooks to BC7
- **AND** an HDR texture passes through uncompressed (BC6H requires a toolchain with uastc-hdr support)

#### Scenario: Explicit target overrides host default

- **WHEN** cook runs on macOS with `--texture-target modern`
- **THEN** the modern mapping is used (the user accepts the pak will not render compressed textures on macOS GL)

### Requirement: Cook SHALL compress textures through the KTX-Software `ktx` CLI with mipmaps

Cook SHALL invoke the `ktx` CLI for each texture needing compression: `ktx create --generate-mipmap` to build a full mip chain, `ktx encode --codec uastc` (LDR) or `uastc-hdr-4x4` (HDR), then `ktx transcode --target <bcn>` to land the codec matching the format mapping (BC1/BC3/BC5/BC7, BC6H-unsigned for HDR). The cooked artifact SHALL be a plain KTX2 file with an unsupercompressed BCn payload. The tool path SHALL be resolvable with a clear error when absent. A compression failure at any pipeline stage SHALL fail the cook with the tool's output included in the error.

#### Scenario: KTX2 output with mip chain

- **WHEN** cook compresses a 1024x1024 color texture to BC7
- **THEN** the cooked artifact is a valid `.ktx2` file whose vkFormat is BC7, whose supercompression scheme is none, and which contains mip levels down to 1x1

#### Scenario: Missing tool produces actionable error

- **WHEN** the `ktx` binary cannot be found at the resolved path
- **THEN** cook exits non-zero with a message naming the expected tool path and how to configure it

### Requirement: The repo SHALL vendor the `ktx` CLI and CMake SHALL stage it next to the binaries

The pinned `ktx` CLI runtime files (`FURY_KTX_VERSION`) SHALL be committed under the third-party tree per platform (`engine/ThirdParty/KTX-Software/<version>/<platform>/`), extracted once from the official KTX-Software release - including any dylib/shared-library dependencies the exe needs (on macOS: `ktx` + `libktx.4.dylib`). Small payloads (a few MB) SHALL be committed as-is without git LFS; a platform payload that is much bigger SHALL use LFS instead. CMake SHALL copy the host platform's tool files next to `furye-cli` at build time so cook finds them beside the running binary. `FURY_KTX_CLI` (env/config) SHALL override tool resolution entirely. The pinned version string SHALL be part of DDC keys so a version bump recooks. The repo SHALL NOT build KTX-Software from source; if the vendored `ktx` fails during implementation, the fallback is a vendored basis_universal `basisu` source build, and that switch SHALL be raised with the user before being made.

#### Scenario: Build stages the tool next to furye-cli

- **WHEN** the project builds `furye-cli` on a platform with a committed tool set
- **THEN** the `ktx` binary (and its shared libraries) sit next to `furye-cli` in the output directory
- **AND** `ktx --version` runs successfully from that location

#### Scenario: Env override wins over the vendored binary

- **WHEN** `FURY_KTX_CLI` points at a user-supplied `ktx` binary
- **THEN** cook uses that binary regardless of the vendored one

#### Scenario: Tool version bump recooks

- **WHEN** `FURY_KTX_VERSION` is bumped and new binaries committed
- **THEN** DDC keys change and the next cook repopulates the cache

### Requirement: Cook SHALL cache cooked output in a Derived Data Cache (DDC) keyed by content and settings

Cook SHALL compute a DDC key as a hash over: the source file's content hash, the texture's cook settings (usage flags, target format), and the cooker/tool version string. The DDC SHALL live in a `DDC/` folder next to the executing binary (overridable via option/env). DDC entries SHALL be stored in hash-prefix subfolders (`DDC/<hex[0:2]>/<hex[2:4]>/<full-hash>.ktx2`) so no folder holds a large number of files. On a DDC hit the cached `.ktx2` SHALL be reused without invoking the compressor; on a miss cook SHALL compress and then write the entry. A change to source content, settings, or tool version SHALL produce a different key and therefore a recook.

#### Scenario: Second cook is a cache hit

- **WHEN** cook runs twice against the same unchanged scene
- **THEN** the second run performs no `ktx` invocations
- **AND** reports all textures as DDC hits

#### Scenario: Editing a texture invalidates its entry only

- **WHEN** one source texture file changes between two cooks
- **THEN** only that texture is recompressed
- **AND** all other textures are DDC hits

#### Scenario: Switching cook target recooks

- **WHEN** cook runs with `--texture-target legacy` then `--texture-target modern`
- **THEN** the second run recompresses every texture (different keys) and stores separate DDC entries

#### Scenario: DDC folder uses hash-prefix subfolders

- **WHEN** a DDC entry with hex key `3fa9c1...` is written
- **THEN** the file lands at `DDC/3f/a9/3fa9c1....ktx2`

### Requirement: Cook SHALL emit a cook manifest for the package step

Cook SHALL write a cook manifest (alongside the scene or at an explicit output path) mapping each canonical asset path to its cooked artifact: for textures the DDC file + format + mip count + dimensions, for passthrough assets the source file. The manifest SHALL record the cook target used. The package step SHALL consume this manifest rather than rediscovering assets.

#### Scenario: Manifest maps path to cooked artifact

- **WHEN** cook completes for a scene with two textures
- **THEN** the manifest contains an entry per canonical texture path pointing at the DDC `.ktx2`, its BC format, dimensions and mip count
- **AND** passthrough assets point at their source files

#### Scenario: Manifest records cook target

- **WHEN** cook completes with `--texture-target legacy`
- **THEN** the manifest records `legacy` as the cook target
