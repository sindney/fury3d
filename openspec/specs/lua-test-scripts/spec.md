# lua-test-scripts

## Purpose

Runnable Lua smoke tests under `tests/lua/` that exercise the new `Mesh` / `Material` / `MeshUtil` / `MeshSimplifier` / `Scene.ForEach*` bindings end-to-end via `./fury exec`. These scripts are binding smoke tests (not feature duplicates of `fury convert` / `fury info`) and double as worked examples for agents.

## Requirements

### Requirement: A `tests/lua/` folder SHALL contain runnable Lua smoke tests for the new bindings, invocable via `fury exec`

A `tests/lua/` directory SHALL exist at the repository root (sibling to `examples/`). It SHALL contain at least three Lua scripts, each invocable via `./fury exec <scene> tests/lua/<script>.lua [args...]`.

The scripts are **binding smoke tests**, not replicas of existing CLI features. The repo already ships `fury convert` (headless format conversion) and `fury info` (headless scene summarization); these scripts do not duplicate that functionality. Their purpose is to prove the new Lua bindings work end-to-end from the `exec` path, and to serve as worked examples for agents.

The required scripts are:

- **`tests/lua/smoke_save.lua`** — round-trip a scene through the Lua save binding. Reads `Scene.GetActive()` (the scene already loaded by `fury exec`), calls `FileUtil.SaveByExtension(arg[1], Scene.GetActive())` (or `Scene.SaveActive(arg[1])` if that helper is introduced), then exits. The output format is inferred from `arg[1]`'s extension (`.json` or `.bin`). Verifies the Lua save-path binding works end-to-end. Does NOT exercise format conversion as a feature (that's `fury convert`'s job); only proves the save binding can round-trip a scene.

- **`tests/lua/smoke_iterate.lua`** — exercise the `Scene.ForEachMesh` / `ForEachMaterial` / `ForEachNode` iteration bindings. Calls each in turn against `Scene.GetActive()`, counts callbacks, and logs the counts to stdout. Read-only — does not write any file. Does NOT summarize a scene as a feature (that's `fury info`'s job); only proves the iteration bindings work.

- **`tests/lua/gen_lod.lua`** — exercise the `MeshSimplifier.SimplifyMesh` + `Mesh.SetLodMeshes` bindings end-to-end. Selects mesh(es) by name from `arg[1]` (or all meshes if `arg[1] == "--all"`). Calls `MeshSimplifier.SimplifyMesh(mesh, {})` with default options on each selected mesh. Attaches the LOD chain via `mesh:SetLodMeshes(r.lod_meshes, r.thresholds)`. If `arg[2]` is provided, saves the modified scene to that path via `FileUtil.SaveByExtension` (or `Scene.SaveActive`); otherwise logs the per-mesh LOD counts and exits without saving. Genuinely new — no existing CLI feature generates LODs.

A `tests/lua/README.md` SHALL exist with one paragraph per script describing its purpose, arguments, and a sample `fury exec` invocation. The README SHALL explicitly note that `smoke_save.lua` and `smoke_iterate.lua` are binding smoke tests, not replacements for the `convert` or `info` CLI subcommands.

#### Scenario: smoke_save.lua round-trips a scene to .bin
- **WHEN** a user runs `./fury exec examples/Resource/Scene/scene.json tests/lua/smoke_save.lua /tmp/out.bin`
- **THEN** the file at `/tmp/out.bin` exists
- **AND** it begins with the 8-byte LZ4 envelope (network-order original size + compressed size)
- **AND** the process exits 0

#### Scenario: smoke_save.lua round-trips a scene to .json
- **WHEN** a user runs `./fury exec examples/Resource/Scene/scene.bin tests/lua/smoke_save.lua /tmp/out.json`
- **THEN** the file at `/tmp/out.json` is a valid UTF-8 JSON document
- **AND** the document has top-level `textures`, `materials`, `meshes`, and `nodes` keys
- **AND** the process exits 0

#### Scenario: smoke_iterate.lua runs end-to-end
- **WHEN** a user runs `./fury exec examples/Resource/Scene/scene.json tests/lua/smoke_iterate.lua`
- **THEN** stdout contains callback counts for `meshes`, `materials`, and `nodes` (e.g., `ForEachMesh callbacks: 12`, `ForEachMaterial callbacks: 8`, `ForEachNode callbacks: 45`)
- **AND** the process exits 0
- **AND** no scene file is written (read-only)

#### Scenario: gen_lod.lua runs end-to-end with --all and saves
- **WHEN** a user runs `./fury exec examples/Resource/Scene/scene.json tests/lua/gen_lod.lua --all /tmp/out.json`
- **THEN** every mesh in the loaded scene has a non-empty LOD chain after the script runs
- **AND** the file at `/tmp/out.json` exists and is a valid engine scene JSON document
- **AND** the process exits 0

#### Scenario: gen_lod.lua runs end-to-end without saving
- **WHEN** a user runs `./fury exec examples/Resource/Scene/scene.json tests/lua/gen_lod.lua --all`
- **THEN** stdout contains per-mesh LOD counts (e.g., `mesh <name>: 3 LODs generated`)
- **AND** no scene file is written
- **AND** the process exits 0

#### Scenario: gen_lod.lua targets a single named mesh
- **WHEN** a user runs `./fury exec examples/Resource/Scene/scene.json tests/lua/gen_lod.lua TankMesh /tmp/out.json`
- **AND** the scene contains a mesh named `TankMesh`
- **THEN** only `TankMesh` has a non-empty LOD chain after the script runs
- **AND** other meshes in the saved scene are unchanged
- **AND** the process exits 0

#### Scenario: tests/lua/README.md exists and notes the non-overlap
- **WHEN** the repository is inspected
- **THEN** `tests/lua/README.md` exists
- **AND** it documents each script's purpose, arguments, and a sample `fury exec` invocation
- **AND** it explicitly notes that `smoke_save.lua` and `smoke_iterate.lua` are binding smoke tests, not replacements for `fury convert` or `fury info`