# pak-runtime-loading

## Purpose

Runtime loading of deployed scenes from `.pak` archives: the asset backend abstraction that routes every asset byte read through a canonical-path interface (loose filesystem vs mounted pak), booting `fury` directly from a pak, transparent LZ4 decompression with integrity verification, compressed KTX2 texture upload, and the async loading worker thread.

## Requirements

### Requirement: All asset byte access SHALL go through an asset backend interface

The engine SHALL route every asset file read (textures, meshes, audio, scripts, scene files) through an asset backend interface keyed by canonical asset path. Two backends SHALL exist: `FileSystemBackend` (loose files, today's behavior, the default) and `PakBackend` (a mounted pak). Callers (scene loader, texture loader, Lua bindings) SHALL NOT know which backend is serving a request: the same load call works against loose files in the editor and against a pak in a deployed build, with an unmodified scene file.

#### Scenario: Same scene loads from loose files and from pak

- **WHEN** a scene is loaded once from loose files and once from a pak built from those files
- **THEN** both loads succeed and produce equivalent scenes (same nodes, same materials, same texture contents)
- **AND** the scene file used for the pak load is byte-identical to the loose one

#### Scenario: Filesystem backend is the default

- **WHEN** the engine starts with no pak mounted
- **THEN** all asset reads resolve against the working directory exactly as before this change

### Requirement: `fury` SHALL boot directly from a `.pak` file

When `fury` is invoked with a `.pak` path as its scene argument, it SHALL mount the pak, read the boot entry from the index, and load that scene through the asset backend. `fury scene.pak` SHALL be sufficient to play a deployed scene. When invoked with a loose scene file that has a sibling `<scene>.pak`, the engine SHALL mount that pak automatically so a deployed folder of `scene.bin` + `scene.pak` also plays without extra arguments.

#### Scenario: Pak boot

- **WHEN** a user runs `./fury game.pak`
- **THEN** the pak is mounted, its boot scene loads, and gameplay starts with no loose asset files present

#### Scenario: Sibling pak auto-mount

- **WHEN** a user runs `./fury scene.bin` and `scene.pak` sits next to it
- **THEN** the pak is mounted and asset requests resolve from it

#### Scenario: Loose files win when no pak exists

- **WHEN** a user runs `./fury scene.bin` with no sibling pak
- **THEN** assets load from the working directory as today

### Requirement: PakBackend SHALL serve entry bytes with transparent decompression

PakBackend SHALL look up canonical paths in the pak index, read the entry's data region, and decompress LZ4 entries to their recorded uncompressed size before returning bytes to the caller. A read for a path absent from the index SHALL fail the same way a missing loose file fails (same error surface to callers). Entry integrity SHALL be verified against the recorded content hash on read, with a clear error on mismatch.

#### Scenario: LZ4 entry returns original bytes

- **WHEN** a caller requests an entry stored LZ4-compressed
- **THEN** the returned bytes equal the original uncompressed content and length

#### Scenario: Missing path mirrors missing-file behavior

- **WHEN** a caller requests `textures/nope.png` that is not in the pak index
- **THEN** the load fails with the same error shape as a missing loose file (no crash, no null-texture silence beyond existing behavior)

#### Scenario: Corrupt entry is detected

- **WHEN** an entry's bytes are corrupted on disk
- **THEN** the read fails with an integrity error naming the entry path

### Requirement: Cooked textures SHALL upload as compressed GL textures

When a texture's bytes are a cooked `.ktx2` with a block-compressed payload, the texture loader SHALL parse the KTX2 header, DFD and level index and upload every mip level via `glCompressedTexImage2D` with the GL internal format matching the vkFormat (BC1/BC3/BC5 on legacy target, BC7/BC6H on modern), accounting for KTX2's smallest-first level order. Files with a supercompression scheme other than none SHALL be rejected with a clear error (runtime transcoding is a future upgrade). Regular image files SHALL keep the existing upload path. If the runtime GL context lacks support for the texture's compressed format, the loader SHALL log a clear per-texture error and substitute the engine's missing-texture placeholder rather than crash.

#### Scenario: BC7 texture uploads all mips

- **WHEN** a cooked BC7 `.ktx2` with 11 mip levels loads on a GL 4.2+ context
- **THEN** each mip level is uploaded with `glCompressedTexImage2D` in largest-first order and the texture is complete

#### Scenario: Supercompressed input is rejected clearly

- **WHEN** a `.ktx2` with a non-none supercompression scheme reaches the loader
- **THEN** it fails with an error naming the file and the unsupported scheme (no crash, no garbage upload)

#### Scenario: Modern pak on legacy-only context degrades cleanly

- **WHEN** a pak cooked with `--texture-target modern` loads on a context without BPTC support
- **THEN** each BC7/BC6H texture logs an unsupported-format error and renders as the placeholder, and the scene otherwise loads

### Requirement: Asset reads SHALL be servable from an async loading thread

The engine SHALL provide an async loading path: callers submit asset read requests (canonical path + priority), a worker thread performs the pak/filesystem read and decompression, and completion callbacks run on the main thread's per-frame pump. GL upload SHALL remain on the render/main thread - the worker delivers raw bytes only. A synchronous read through the same backend SHALL remain available for callers that need immediate bytes. The async path SHALL be exercised by at least scene texture preloading so its correctness is covered by more than unit tests.

#### Scenario: Async request completes on the main thread

- **WHEN** a texture read is requested asynchronously during scene load
- **THEN** the worker thread performs the disk read and decompression
- **AND** the completion callback runs during the main-thread pump in a later frame, delivering the full bytes

#### Scenario: Sync and async paths return identical bytes

- **WHEN** the same pak entry is read synchronously and asynchronously
- **THEN** both return byte-identical content

#### Scenario: Worker never touches GL

- **WHEN** assets load through the async path
- **THEN** no GL call is made from the worker thread (verifiable under the render-thread GL-call checks/tracy zones)
