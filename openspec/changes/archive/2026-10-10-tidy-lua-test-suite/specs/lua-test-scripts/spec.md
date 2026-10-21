# lua-test-scripts

## Purpose

Runnable Lua smoke tests under `tests/lua/` that exercise the new `Mesh` / `Material` / `MeshUtil` / `MeshSimplifier` / `Scene.ForEach*` bindings end-to-end via `./fury exec`. These scripts are binding smoke tests (not feature duplicates of `fury convert` / `fury info`) and double as worked examples for agents.

## Purpose (this delta)

The capability is rewritten to describe the post-organization convention rather than naming three specific scripts. The original `gen_lod.lua` is reclassified as a scene-generation sample and moves to `tests/lua/scenes/`; `smoke_save.lua` and `smoke_iterate.lua` retain their regression-guard role but rename to `scene_smoke_save.lua` and `scene_smoke_iterate.lua`. The detailed organization rules are covered by the `lua-test-organization` capability; this spec keeps the binding-smoke role and the CLI non-overlap invariant.

## REMOVED Requirements

### Requirement: A `tests/lua/` folder SHALL contain runnable Lua smoke tests for the new bindings, invocable via `fury exec`

**Reason**: The original requirement enumerated three specific scripts. The new organization moves scene-generation scripts out of `tests/lua/` and groups regression guards by feature prefix; a script-named list is no longer the right shape for this spec. The binding-smoke role and the no-CLI-overlap invariant remain in the ADDED replacement below.

**Migration**: See the `lua-test-organization` capability for the new folder split (`tests/lua/` vs `tests/lua/scenes/`) and the feature-prefix convention. `gen_lod.lua` is now under `tests/lua/scenes/gen_lod.lua`. The binding-smoke role is now described by the ADDED requirement below.

## ADDED Requirements

### Requirement: `tests/lua/` SHALL contain binding-smoke regression guards invocable via `fury exec`

A `tests/lua/` directory SHALL exist at the repository root (sibling to `examples/`). It SHALL contain at least three Lua scripts, each invocable via `./fury exec <scene> tests/lua/<script>.lua [args...]`.

The scripts are **binding smoke tests**, not replicas of existing CLI features. The repo already ships `fury convert` (headless format conversion) and `fury info` (headless scene summarization); these scripts do not duplicate that functionality. Their purpose is to prove the new Lua bindings work end-to-end from the `exec` path, and to serve as worked examples for agents.

Each script SHALL exit non-zero on invariant violation (exit code 1 for user error / binding failure, code 2 for internal C++ exception escaping `DoExec`). Scripts whose purpose is to author or transform a scene asset SHALL live in `tests/lua/scenes/` instead; see the `lua-test-organization` capability.

The required binding-smoke scripts SHALL include at minimum:

- **`tests/lua/scene_smoke_save.lua`** — round-trip a scene through the Lua save binding. Reads `Scene.GetActive()` (the scene already loaded by `fury exec`), calls `Scene.SaveActive(arg[1])` (or `FileUtil.SaveByExtension` if that helper is preferred), then exits. The output format is inferred from `arg[1]`'s extension (`.json` or `.bin`). Verifies the Lua save-path binding works end-to-end. Does NOT exercise format conversion as a feature (that's `fury convert`'s job); only proves the save binding can round-trip a scene.

- **`tests/lua/scene_smoke_iterate.lua`** — exercise the `Scene.ForEachMesh` / `ForEachMaterial` / `ForEachNode` iteration bindings. Calls each in turn against `Scene.GetActive()`, counts callbacks, and logs the counts to stdout. Read-only — does not write any file. Does NOT summarize a scene as a feature (that's `fury info`'s job); only proves the iteration bindings work.

The `tests/lua/scenes/gen_lod.lua` script (scene-generation, not a binding smoke) is documented separately in the `lua-test-organization` capability.

A `tests/lua/README.md` SHALL exist describing the folder split, the feature-prefix naming convention, the CLI non-overlap invariant, and a list of the scripts removed in this change with reasons.

#### Scenario: scene_smoke_save.lua round-trips a scene to .bin
- **WHEN** a user runs `./fury exec examples/Resource/Scene/scene.json tests/lua/scene_smoke_save.lua /tmp/out.bin`
- **THEN** the file at `/tmp/out.bin` exists
- **AND** it begins with the 8-byte LZ4 envelope (network-order original size + compressed size)
- **AND** the process exits 0

#### Scenario: scene_smoke_save.lua round-trips a scene to .json
- **WHEN** a user runs `./fury exec examples/Resource/Scene/scene.bin tests/lua/scene_smoke_save.lua /tmp/out.json`
- **THEN** the file at `/tmp/out.json` is a valid UTF-8 JSON document
- **AND** the document has top-level `textures`, `materials`, `meshes`, and `nodes` keys
- **AND** the process exits 0

#### Scenario: scene_smoke_iterate.lua runs end-to-end
- **WHEN** a user runs `./fury exec examples/Resource/Scene/scene.json tests/lua/scene_smoke_iterate.lua`
- **THEN** stdout contains callback counts for `meshes`, `materials`, and `nodes` (e.g., `ForEachMesh callbacks: 12`, `ForEachMaterial callbacks: 8`, `ForEachNode callbacks: 45`)
- **AND** the process exits 0
- **AND** no scene file is written (read-only)

#### Scenario: scripts under tests/lua/ exit non-zero on invariant violation
- **WHEN** a script under `tests/lua/` (not `tests/lua/scenes/`) is invoked against a broken scene
- **THEN** the process exits non-zero

#### Scenario: tests/lua/README.md documents the new organization
- **WHEN** the repository is inspected
- **THEN** `tests/lua/README.md` exists
- **AND** it documents the `tests/lua/` vs `tests/lua/scenes/` split
- **AND** it lists the feature prefixes
- **AND** it documents each surviving script's purpose, arguments, and a sample `fury exec` invocation
- **AND** it explicitly notes that the iteration / save smoke tests are binding smokes, not replacements for the `convert` or `info` CLI subcommands