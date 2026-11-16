## ADDED Requirements

### Requirement: Import SHALL build and cache eligible cluster mesh payloads

Import SHALL enumerate every eligible static mesh and invoke the validated builder once per shared mesh after import/weld and before native save. Cluster DDC entries SHALL use a `CLOD_` namespace and a key covering the complete source content, build options, meshoptimizer/builder versions, and payload schema. Unchanged inputs SHALL reuse cached payloads without rebuilding; changed inputs or versions SHALL regenerate them. Cook SHALL validate and package the resulting mandatory payloads rather than generating missing payloads during normal operation.

#### Scenario: Second tank import is a cache hit

- **WHEN** an unchanged tank mesh is imported twice and its containing scene is cooked
- **THEN** the second import reuses the tank `.clod` artifact without invoking the builder
- **AND** cook packages the existing validated artifact without rebuilding it
- **AND** editing any render-relevant mesh stream or cluster option produces a different key and a rebuild on the next import

### Requirement: A temporary CLI SHALL migrate existing native assets

The migration CLI SHALL load existing native JSON/BIN scenes and meshes, generate and validate payloads for every eligible shared static mesh through the import builder, and save the resulting descriptors and native assets without embedding cluster arrays in scene JSON. Failed meshes SHALL be reported and SHALL NOT be treated as successfully migrated. Runtime SHALL NOT retain a separate cluster-free native compatibility path after migration.

#### Scenario: Existing tank scene is migrated once

- **WHEN** the CLI processes an existing native scene whose shared tank meshes have no cluster descriptors
- **THEN** it builds each eligible shared mesh once and writes the descriptor and `.clod` payload
- **AND** subsequent loads use the same mandatory-payload path as a freshly imported scene

### Requirement: Cook and package SHALL persist cluster payloads as first-class artifacts

The cook manifest SHALL add a `cluster_meshes` section mapping each cluster content key to its validated binary payload and schema metadata. Package SHALL include those artifacts in the pak, and runtime SHALL resolve them through `AssetBackend`. Embedded scene meshes SHALL remain associated with payloads by descriptor content key rather than requiring an external source-mesh filename.

#### Scenario: Packaged tank boots without source FBX or DDC

- **WHEN** a pak containing the tank scene and its `cluster_meshes` artifact is run with the source FBX and host DDC unavailable
- **THEN** the runtime loads the embedded mesh and matching cluster payload from the pak
- **AND** eligible instances render through the cluster path

### Requirement: Import, migration, and cook SHALL report incompatible or failed cluster data explicitly

Import and the temporary migration CLI SHALL report each eligible mesh that is unsupported, fails generation or validation, or cannot produce a payload, and SHALL NOT save it as a successfully migrated native asset. Cook SHALL report missing, stale, or corrupt mandatory payloads. It SHALL NOT silently omit a required cluster artifact or mutate the source scene. Validation failures and unresolved required outputs SHALL produce a nonzero import, migration, or cook result according to the existing error policy.

#### Scenario: Invalid cluster build fails visibly

- **WHEN** an eligible mesh fails monotonic-error validation during import, migration, or cook validation
- **THEN** the report names the mesh and failed invariant
- **AND** the invalid payload is not added to the DDC or manifest
- **AND** the operation returns a failure result rather than reporting complete success

### Requirement: Cook SHALL include cluster shader permutations for every requested backend

Shader discovery SHALL include cluster instance compaction, selection, culling, command generation, GBuffer, debug, and any per-draw-ID permutations required by the enabled pipeline. Every requested Vulkan and Metal target SHALL receive headless-compiled backend artifacts and reflection metadata, including storage bindings and base-instance/draw-ID inputs. Metal-owned ICB conversion shaders SHALL be versioned and packaged through the backend's resource mechanism.

#### Scenario: Packaged Vulkan and Metal run without source compilation

- **WHEN** a packaged build loads the cluster pipeline with `--rhi=vulkan` or `--rhi=metal`
- **THEN** all shared cluster compute, graphics, and debug permutations load from cooked artifacts
- **AND** Metal's native ICB encoder is available through its packaged backend resource
- **AND** no runtime GLSL-only dependency is required
