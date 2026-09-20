# add-cook-package-pipeline

## Why

Today the engine only loads loose asset files relative to a working directory, so shipping a scene means shipping the whole resource tree. There is no way to produce a deployable artifact, textures ship uncompressed (slow load, huge VRAM), and every load is a synchronous disk read on the main thread. This adds a UE-style cook + package pipeline: cook compresses textures to GPU block formats with a Derived Data Cache (DDC) so recooks are incremental, package bundles everything a scene needs into one `.pak` archive, and the runtime loads from the pak through a transparent middle layer with an async loading thread - readying the engine for streaming.

## What Changes

- **Cook step (headless)**: walks a scene's referenced assets, compresses textures to BCn block-compressed KTX2 via the KTX-Software `ktx` CLI (UASTC encode -> per-target transcode), caches results in a DDC keyed by `hash(source content + texture settings + tool version + cook target)`.
- **DDC folder** next to the binary (UE-style), split into hash-prefix subfolders (`DDC/<hex[0:2]>/<hex[2:4]>/<full-hash>.ktx2`) so folders stay small.
- **Two cook targets**: `legacy` (BC1 color, DXT5/BC3 alpha, BC5 normal maps - S3TC-class, universal desktop GL) and `modern` (BC7 color/alpha - GL 4.2+/BPTC). HDR stays uncompressed on both targets in v1 (the pinned ktx 4.4.2 CLI lacks uastc-hdr/BC6H; the mapping gains BC6H when the toolchain does). Default is `legacy` on macOS hosts (GL 4.1 cap, no BPTC), `modern` elsewhere.
- **Package step**: packs the scene's cooked textures + all other referenced assets into a single `.pak` (custom container: EOF footer + path-keyed index + per-entry LZ4 block compression, or stored uncompressed). The scene file itself is packed into the pak as the boot entry - `fury game.pak` mounts and plays it. Loose `scene.bin` + sibling `scene.pak` keeps working for development.
- **Asset backend middle layer**: all asset byte requests go through a backend interface (`FileSystemBackend` raw files, `PakBackend` mounted archive) keyed by the same canonical asset paths used today. Scene files are NOT modified on deploy; upper layers cannot tell pak from loose files.
- **Async loading thread**: pak reads + decompression run off the main thread behind a request queue; GL upload stays on the render thread. Sync fallback path preserved.
- **New `furye-cli` binary**: headless, reuses `fury`'s CLI dispatch (`convert`, `info`, `exec`) plus new `cook` and `package` subcommands, and can run editor scripts. `furye` stays the interactive editor; `fury` stays the player/runtime and gains `.pak` boot.
- Runtime KTX2 reader: parse header/DFD/level index (unsupercompressed subset), upload each mip via `glCompressedTexImage2D`.

## Capabilities

### New Capabilities

- `asset-cooking`: the cook step - cookable-asset discovery from a scene, per-type texture format mapping (color/alpha/normal/HDR x legacy/modern target), `ktx` CLI invocation, and the DDC (key derivation, hash-prefix folder layout, hit/miss semantics, tool-version invalidation).
- `pak-packaging`: the `.pak` container format (header, path-keyed index, per-entry offset/size/compression), the package step that collects cooked + raw assets plus the scene boot entry, and CLI options (compression on/off, cook target passthrough).
- `pak-runtime-loading`: the asset backend middle layer (raw filesystem vs pak, transparent to callers), pak mount + index lookup, block decompression, the async loading thread and its main-thread completion pump, and `.pak` boot in `fury`.
- `furye-cli`: the third binary target - headless CLI with `fury`'s subcommand set plus `cook` / `package`, editor-script execution without a window, shared dispatch/exit-code conventions.

### Modified Capabilities

- `cli`: `fury` gains `.pak` input dispatch (mount pak, load boot scene) alongside existing scene-file inputs; subcommand inventory documentation extends to `furye-cli`.
- `kraut-toolchain`: the `kraut` subcommand moves from `fury` to `furye-cli` (player binary keeps inspect/play only).
- `render-mesh-cli`: the `render-mesh` subcommand moves from `fury` to `furye-cli` (same split).
- `editor-shell`: the editor File menu gains a "Package..." item driving `furye-cli package` with a settings modal and a modal progress dialog (streaming log, cancel).

## Impact

- **New code**: cook/package command handlers, DDS writer/loader, DDC store, pak writer/reader, asset backend interface + two implementations, async loading queue, `furye-cli` target in CMake.
- **Touched code**: `EntityManager`/`Texture` load path reroutes through the backend interface (identity semantics unchanged); `fury` `main()` dispatch adds `.pak`; texture GL upload gains a compressed path (`glCompressedTexImage2D`).
- **Dependencies**: KTX-Software `ktx` CLI vendored as pinned-version binaries committed to the third-party tree (official release extraction; ~4.3 MB on macOS, no LFS; CMake stages them next to `furye-cli`; `FURY_KTX_CLI` env override; vendored basisu source build as consult-user-first fallback); the already-vendored LZ4 for pak block compression; a small vendored SHA-1 for DDC keys and pak integrity hashes.
- **Out of scope (next change)**: actual streaming (priority queues, mip-level streaming, open-world mount/unmount) - this change builds the plumbing it needs.
- **Unaffected**: scene file format (`.bin`/`.json` unchanged), editor workflows on loose files, `asset-path-identity` semantics (path stays the canonical key; pak resolution happens below it).
