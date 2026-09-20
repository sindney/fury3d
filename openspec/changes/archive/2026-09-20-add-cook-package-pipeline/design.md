# add-cook-package-pipeline - design

## Context

The engine loads everything as loose files: `FileUtil::LoadString`/`LoadImage` (engine/Fury/FileUtil.cpp) are direct `std::ifstream`/stb_image calls, textures upload in `Texture::CreateFromImage` (engine/Fury/Texture.cpp:227) via `glTexStorage2D`/`glTexSubImage2D` inside a `DispatchGL` lambda (decode on caller thread, upload on GL thread). There is no archive layer, no compressed-texture support (only unused `glCompressedTexImage*` pointers in GLLoader.h), and no content hashing (closest: FNV-1a `MeshContentHash` in Mesh.cpp:842). Useful existing pieces: LZ4 is already vendored (engine/ThirdParty/LZ4, used for the `.bin` scene wrapper), the render thread already marshals GL work via `RenderThread::EnqueueJob`/`DispatchGL` (engine/Fury/RenderThread.h), headless CLI dispatch exists as `Cli::LooksLikeSubcommand` + `Cli::Run` (engine/Fury/Cli.cpp:1356), and a third binary is one `fury_add_target` call away (engine/CMakeLists.txt:489). Asset identity is canonical-by-path in `EntityManager` (engine/Fury/EntityManager.h) after asset-path-identity - the pak layer must key on exactly those paths. UE conventions referenced from the local MorefunEngine checkout: DDC keys as `<type>_<version>_<hash-of-inputs>` (DerivedDataCache.cpp), on-disk DDC split into hash-derived subfolder tiers (FileSystemDerivedDataBackend.cpp), pak = EOF footer (`FPakInfo`: magic/version/index offset+size/hash) + path->`FPakEntry` index with 64 KB compression blocks and SHA-1 per entry (IPlatformFilePak.h), cook via `UCookCommandlet` (`-run=cook`). For texture encoding on a Mac host UE vendors encoder libs and commits prebuilt dylibs (NVTT 2.0.8 for DXT1/3/5+BC4/5 at Engine/Binaries/ThirdParty/nvTextureTools/Mac, ISPC TexComp for BC6H/BC7 at Engine/Binaries/ThirdParty/Intel/ISPCTexComp/Mac64-Release) - evidence these encoders work on macOS, but both upstreams are archived/dead, so we prefer a maintained tool with official binaries instead of copying the vendored-dylib pattern.

## Goals / Non-Goals

**Goals:**

- One-command deployment: `furye-cli package scene.bin` -> `scene.pak` (+ optional loose scene) that `./fury scene.pak` plays.
- Textures shipped as GPU block-compressed DDS (BC1/DXT5/BC5 legacy, BC7/BC6H modern), DDC-cached so recooks are incremental.
- Transparent runtime: unmodified scene files; callers cannot tell pak from loose files.
- Async loading thread owning all pak/decompression work; GL upload stays on the render thread.
- Pak format ready for streaming (block-granular compression, hash-verified entries, versioned footer).

**Non-Goals:**

- Streaming itself (priority scheduling beyond a simple field, mip streaming, pak mount/unmount during play) - next change.
- Encryption, pak patching/diffing, chunked DLC paks, network mounting.
- Cooking meshes/audio into new formats (passthrough only), shader cooking (shaders stay source files; they can be packed as passthrough).
- Editor UI for cook/package (CLI only; editor menu item is a later QoL).

## Decisions

### 1. Scene ships inside the pak as the boot entry

`fury game.pak` mounts and loads the scene recorded as the boot entry in the index. Loose `scene.bin` + sibling `scene.pak` also works (auto-mount) for iteration. Alternatives considered: (a) loose scene + pak only - simpler but two files to ship and a mismatch hazard (scene newer than pak); (b) scene-only-outside, pak keyed by a manifest inside the scene - inverts the dependency and complicates `fury <thing>` UX. Scene-in-pak gives single-file deployment, and because the pak index records the boot path, no sidecar manifest is needed. The scene entry is stored byte-identical (pak keys are the same canonical paths, so no rewrite is needed).

### 2. Backend layer slots under FileUtil, keyed by canonical path

A static `AssetBackend` facade (engine/Fury/AssetBackend.h) owns mounted paks and answers every byte read: `ReadAssetBytes(resolvedPath)` canonicalizes the RESOLVED path via `FileUtil::ToCanonicalAssetKey` (cwd-strip + lexical normalization), serves from the pak index on a key hit (later mounts shadow), else falls back to the filesystem. All raw reads route through it: `FileUtil::LoadString`/`LoadImage`/`LoadFile`/`LoadCompressedFile`, `Texture::CreateFromImage`, `Heightmap::LoadHeights`, `OceanWaves` payloads. Callers and scene files never learn which source served a read. Rejected alternative: virtualizing at `Scene::ResolveAsset` (returning `pak://` URLs) - it would leak the pak into every caller and break code that expects real filesystem paths (stb_image, ktx inputs, extract-textures-on-save).

**Content sniffing, not extensions**: the pak stores cooked KTX2 bytes under the ORIGINAL image key (e.g. `grass.png`), so `Texture::CreateFromImage` reads bytes via the facade and dispatches on magic bytes (KTX2 identifier -> compressed upload; otherwise stb decode from memory). The scene file stays unmodified and mixed loose/cooked folders work.

**Lazy-registered assets**: terrain splat/layer textures bind at render-setup time and never enter the EM during a headless cook - cook walks the node tree for Terrain components explicitly (splat = non-sRGB data, layers = sRGB). Second-order file references are followed too: heightmap `.r16` -> `.json` sidecar, ocean.json -> `*File` payload keys (disp/nrm). A deployed run with zero loose project files renders the full scene (verified by the isolated-cwd screenshot test).

### 3. KTX2 as the cooked texture container

Cooked textures are KTX2 files holding plain (unsupercompressed) BCn payloads with full mip chains, produced by the `ktx` CLI. The runtime parses KTX2 itself for the unsupercompressed subset: fixed header, DFD block (vkFormat -> GL internalformat), level index; note KTX2 stores mip levels smallest-first, flipped on upload. Files with `supercompressionScheme != 0` are rejected with a clear error (that path is the future streaming upgrade). Upload follows the existing split: parse on the calling thread, `glCompressedTexImage2D` per mip inside `DispatchGL`. `TextureFormat` gains compressed enum values; `Texture::Save`/`Load` of cooked textures references the pak key like any path. Alternatives considered: DDS (trivial 128-byte parse, but cannot carry UASTC and no maintained win+mac CLI writes it - KTX2 is the native output of the chosen tool); vendoring libktx for parsing (heavier dep than the ~200-line unsupercompressed reader; revisit when supercompression lands); hand-rolling a custom blob (reinvents standardized metadata).

### 4. Cook = furye-cli subcommand driving the KTX-Software `ktx` CLI; DDC keyed UE-style

`furye-cli cook <scene>` headless-loads the scene (no GL - `Texture::CreateFromImage` already skips GPU upload when no GL, Texture.cpp:286), enumerates path-keyed `Texture` entries, derives usage (material slot binding: `normal*` slot -> normal map; `.hdr`/`.exr` source -> HDR; sRGB flag + alpha presence -> color/alpha; overridable per-path via an optional `<scene>.cook.json` sidecar), and for each texture computes:

`ddcKey = hex( SHA1( "TEXTURE" | COOKER_VERSION | target | format | usageFlags | SHA1(sourceBytes) ) )`

stored at `DDC/<hex[0:2]>/<hex[2:4]>/<key>.ktx2` next to the binary (override: `--ddc`, `FURY_DDC` env). This mirrors UE's `<type>_<version>_<inputs-hash>` key and its subfolder-tier on-disk backend; we use 2+2 hex chars (git-object style, 65k leaf capacity, eyeball-friendly) instead of UE's CRC decimal-digit tiers - same purpose, cheaper to compute and debug. On miss, the ktx pipeline runs: `ktx create --generate-mipmap` (source -> KTX2 with mips) -> `ktx encode --codec uastc` (or `uastc-hdr-4x4` for HDR) -> `ktx transcode --target bc1|bc3|bc5|bc7|bc6hu` (per the legacy/modern mapping) -> plain unsupercompressed BCn KTX2 into the DDC. On hit the KTX2 is reused. Cook writes a manifest (JSON) mapping canonical path -> {ddc file, format, w/h, mip count} + passthroughs, consumed by `package`.

**Tool choice**: KTX-Software `ktx` CLI (pinned 4.4.2, Apache-2.0, actively maintained) is the only candidate with official, current, prebuilt binaries for BOTH Windows and macOS (plus Linux), and it embeds basis_universal - UASTC->BCn transcode is near-lossless by construction, and BC6H (unsigned `bc6hu`, correct for sky/HDR radiance) comes from UASTC HDR. Rejected: CompressonatorCLI (no macOS binary, dormant since 2024), ISPCTextureCompressor (archived upstream, library-only, no container writer - UE vendors it but ships its own prebuilt dylibs), DirectXTex texconv (Windows-only, WIC-bound), NVTT (dead, no BC7/BC6H), astcenc (ASTC-only), basisu CLI standalone (no current prebuilt binaries - kept as the vendored source-build fallback since it is a zero-dependency CMake build). The CLI boundary keeps the tool swappable per platform if provisioning ever needs it.

**Tool provisioning**: the `ktx` CLI binaries are committed to the repo, not downloaded at configure time. One-time per platform: install/extract the official KTX-Software release (`FURY_KTX_VERSION` pin, initially 4.4.2), then commit the minimal runtime set into `engine/ThirdParty/KTX-Software/<version>/<platform>/`. On macOS arm64 that set is `ktx` (1.9 MB) + `libktx.4.dylib` (2.4 MB) - the exe links `@rpath/libktx.4.dylib` whose rpath already includes `@executable_path`, so the dylib just sits next to the exe (verified: `--version` and a full PNG -> mips -> UASTC -> BC7 KTX2 smoke test pass from a bare directory). Total ~4.3 MB: committed as-is, no git LFS (updates are rare; if a future platform payload turns out much bigger, switch that platform to LFS then). CMake copies the platform's tool files next to `furye-cli` in examples/ at build time. `FURY_KTX_CLI` env/config overrides resolution entirely. The pinned version string feeds DDC keys, so upgrading the pin recooks cleanly. sRGB note from the smoke test: pass an `_SRGB` `--format` to `ktx create` for sRGB sources or it warns about lossy color conversion - the cook maps the engine's sRGB flag accordingly. If `ktx` turns out not to work during implementation for any reason, STOP and consult the user before switching to the vendored basisu source-build fallback (basis_universal is the middleware inside ktx anyway, zero-dependency CMake build).

**Runtime-transcode upgrade path (documented, not this change)**: cook could stop at the UASTC stage and package the supercompressed KTX2, vendoring the single-file Apache-2.0 `basisu_transcoder.cpp` to transcode at load per GL caps (BC7 where supported, DXT5 otherwise) - one cooked artifact serving both targets and smaller paks. Deferred: adds the transcoder dependency and load-time CPU cost; the pre-transcoded KTX2 path keeps the runtime minimal and the legacy/modern contract explicit.

**HDR/BC6H limitation (verified against the pinned binary)**: ktx 4.4.2's `encode --codec` accepts only `basis-lz|uastc` and `transcode --target` has no `bc6hu` - UASTC HDR is not exposed in this CLI build. HDR textures therefore cook as uncompressed passthrough on BOTH targets in v1; when a toolchain with uastc-hdr ships (newer KTX-Software or the vendored basisu build), the mapping gains BC6H-unsigned with no format/runtime changes.

Texture usage inference conflict handling: first material slot binding wins, a warning lists conflicts; sidecar overrides silence it.

### 5. Pak format: EOF footer + sorted index + 64 KB LZ4 blocks (UE-shaped, minimal)

Layout: `[data blocks...][index][footer]`. Footer (fixed size): magic `FURYPAK1`, u32 version, u64 indexOffset, u64 indexSize, SHA-1 of index. Index: count, sorted entries {canonical path, boot flag, u64 offset, u64 uncompressedSize, u32 codec (0=none,1=lz4), u32 blockSize, per-block u32 compressedSizes, SHA-1 of uncompressed payload}, then boot path. Writer appends data, then index, then footer; reader seeks to EOF-footer, validates magic+version+index hash, binary-searches the sorted index. LZ4 from the already-vendored copy; SHA-1 vendored as a small single-file (also used for DDC keys - one hash primitive everywhere; xxhash rejected to avoid a second one, cook-time hashing is cached anyway). 64 KB blocks match UE's `MaxChunkDataSize` granularity: partial-entry reads and future streaming decompress only touched blocks. `--compression none` stores payloads verbatim (fastest load, bigger file). Per-entry "store if not smaller" fallback keeps already-compressed payloads (png/jpg passthroughs, dds) from wasting CPU at read time.

### 6. Async loading thread: one worker queue, main-thread completion pump, GL never leaves the render thread

New `AssetLoader` (engine/Fury/AssetLoader.h): a bounded request queue {canonical path, priority, completion callback}, N=1 worker thread (pool later) doing backend read + LZ4 decompress; completions drained per frame on the main thread (`Pump()` from the frame loop / exec pump). Workers touch no GL (`FURY_GL_THREAD_GUARD` keeps this honest). Scene texture loads go through it with a drain barrier before `Scene::Load` returns, so load semantics stay deterministic (tests unchanged) while disk+decompress parallelizes. Sync reads remain for anything needing immediate bytes. This is UE's AsyncLoadingThread2 shape (worker does IO+decompress, game thread gets completions) minus priorities-as-scheduling - the priority field exists but phase 1 is FIFO, deliberately leaving scheduling to the streaming change.

### 7. `furye-cli` = WITH_EDITOR sources, always-CLI main

Third target via `fury_add_target(furye-cli)` (engine/CMakeLists.txt), sharing FURY_EDITOR_SRC with a `FURY_HEADLESS_CLI` define: `main()` routes everything through `Cli::Run` (cook/package/exec-script added to the dispatch, compiled into this binary only; `fury` and `furye` inventories unchanged except fury's `.pak` boot carve-out). GL-needing subcommands (render-mesh) may still create a hidden window - headless-by-default, not headless-forced. Editor-script execution = `exec-script <file.lua>` registering the editor binding surface without entering the Editor.lua shell. Binary lands in examples/ next to fury/furye.

## Risks / Trade-offs

- **Windows/Linux tool payloads not yet committed** -> the repo carries the macOS arm64 set first (this dev machine); Windows/Linux sets get extracted and committed the first time a build on that platform needs cook, same one-time procedure.
- **Committed binaries go stale** -> intentional: pin is explicit (`FURY_KTX_VERSION`), updates are rare and deliberate, and the version string feeds DDC keys so a bump recooks instead of mixing encoder versions silently.
- **Hand-rolled KTX2 reader correctness** -> restricted to the unsupercompressed BCn subset with explicit rejection of `supercompressionScheme != 0`; KTX2's smallest-first mip order is covered by a round-trip test (cook -> read -> dimension/mip assertions) before any GL upload work.
- **ktx CLI version skew** -> pinned via `FURY_KTX_VERSION` and embedded in DDC keys; bumping the pin recooks instead of silently mixing encoder versions.
- **Path canonicalization drift between packager and runtime** -> single shared `CanonicalPakKey()` helper + the loose-vs-pak equivalence test (same scene both ways, byte-compare resolved payloads).
- **SHA-1 is collision-weak in crypto contexts** -> used here only for integrity/DDC keys, not security; noted in code.
- **Async loader changes load timing** -> drain barrier keeps `Scene::Load` synchronous-from-caller; async opt-in per call site; tracy zones on queue depth so regressions are visible.
- **Modern-target paks won't render compressed textures on macOS GL 4.1** -> host-based default (legacy on macOS) + loader fallback to placeholder with a clear log, per spec.
- **DDC unbounded growth** -> keyed by content so stale entries are harmless; a `furye-cli cook --prune` (delete entries not referenced by the last manifest) is the escape hatch.
- **Scene-in-pak vs sibling-pak double source of truth** -> if both exist, pak wins and logs which one served (deployed folders typically have one form; editor never auto-mounts).

## Migration Plan

No migration: loose-file projects work unchanged (FileSystemBackend default, no pak mounted). Adoption is per-scene: cook + package when ready to deploy. Rollback = delete the pak; nothing in the scene file or DDC affects the loose path.

## Open Questions

- None outstanding. (`fury exec` accepts `.pak` scenes: yes, implemented - it is the headless test entry point.)
