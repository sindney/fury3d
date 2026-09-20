# pak-packaging

## Purpose

The package step of the cook/package pipeline: consuming a scene file plus its cook manifest to produce a single self-contained `.pak` containing every cooked texture, passthrough asset, and the scene itself as the boot entry. The pak format uses an EOF footer, a path-keyed index with per-entry integrity hashes, and fixed-size LZ4 compression blocks.

## Requirements

### Requirement: The package step SHALL produce a single `.pak` containing everything a scene needs

The package command SHALL consume a scene file plus its cook manifest and produce one `<scene>.pak` file containing: every cooked texture artifact, every passthrough asset, and the scene file itself as the designated boot entry. After packaging, the pak SHALL be self-contained: mounting it and loading the boot scene SHALL NOT require any file outside the pak. Paths inside the pak SHALL be the same canonical asset paths the scene references today (working-directory-relative), so the scene file is stored unmodified. Assets referenced via the engine-resource prefix (`Engine/...`) SHALL also be packed, under the same canonical key form the runtime resolution produces for them.

#### Scenario: Pak is self-contained

- **WHEN** a scene referencing two textures, one mesh and one Lua script is cooked and packaged
- **THEN** the pak contains the two cooked `.ktx2` payloads, the mesh, the script, and the scene file
- **AND** loading the boot scene from the pak succeeds with no loose files present

#### Scenario: Scene stored unmodified as boot entry

- **WHEN** a scene is packaged
- **THEN** the byte content of the scene entry in the pak equals the byte content of the packaged scene file
- **AND** the pak index records it as the boot entry

#### Scenario: Canonical paths are pak keys

- **WHEN** the scene references `TerrainIsland/snow.png`
- **THEN** the pak index contains an entry keyed by `TerrainIsland/snow.png`

### Requirement: The pak format SHALL use an EOF footer, a path-keyed index, and per-entry compression blocks

The `.pak` layout SHALL be: a data region of sequentially stored entry payloads, an index region, and a fixed-size footer at end of file. The footer SHALL carry a magic, a format version, the index offset and size, and a content hash of the index. The index SHALL map each canonical path to entry metadata: data offset, uncompressed size, compression codec (`none` or `lz4`), compression block size, the per-block compressed sizes, and a content hash (SHA-1) of the uncompressed payload, plus the boot entry's path. Index paths SHALL be normalized (forward slashes, defined case policy) so lookups are deterministic across pack and load. Readers SHALL locate the index via the footer (never by scanning) and SHALL validate the footer magic, version, and index hash before mounting.

#### Scenario: Index round-trips entry metadata

- **WHEN** a pak is written with an entry `a/b.png` of 1000 bytes uncompressed, LZ4-compressed
- **THEN** reading the index yields the offset, uncompressed size 1000, codec `lz4`, the block records, and the SHA-1 of the 1000 bytes for `a/b.png`

#### Scenario: Unknown pak version is rejected clearly

- **WHEN** the runtime opens a pak whose footer magic or format version it does not support
- **THEN** it fails with an error naming the pak version and the supported versions (no crash, no partial mount)

#### Scenario: Tampered index is rejected

- **WHEN** the index bytes of a pak are modified after packaging
- **THEN** mounting fails with an index integrity error

#### Scenario: Deterministic index order

- **WHEN** the same scene is packaged twice with unchanged inputs
- **THEN** the two index listings are identical (entries in a defined sorted order)

### Requirement: Package SHALL compress entries in fixed-size blocks with an uncompressed option

Package SHALL compress entry payloads with LZ4 in fixed-size blocks (64 KB, matching UE's pak chunk granularity) by default and SHALL accept an option to store entries uncompressed for maximum read speed. Block-level compression SHALL be what the index records, so a reader can decompress a single block without touching the rest of the entry - the property streaming will rely on later. An entry whose compressed blocks total >= its source size SHALL be stored uncompressed instead, and the index SHALL record what was actually done.

#### Scenario: Default package compresses in blocks

- **WHEN** package runs with default options on a scene with a 1 MB passthrough asset
- **THEN** that entry's index record shows codec `lz4`, block size 65536, and one compressed-size record per 64 KB block

#### Scenario: Uncompressed packaging for load speed

- **WHEN** package runs with `--compression none`
- **THEN** every entry's index record shows codec `none` and no block records

#### Scenario: Incompressible entry falls back to store

- **WHEN** an entry compresses to a size >= its source size
- **THEN** it is stored uncompressed and its index record says so

### Requirement: Package SHALL fail loudly on missing cooked artifacts

If the cook manifest references a DDC artifact or source file that does not exist, package SHALL exit non-zero naming the missing artifact and the asset path it belongs to. Package SHALL NOT emit a partial pak that silently omits referenced assets.

#### Scenario: Stale manifest entry fails the package

- **WHEN** the manifest points at a DDC file that has been deleted
- **THEN** package exits non-zero and names both the DDC path and the canonical asset path
- **AND** no `.pak` output is left behind (or a partial output is removed)
