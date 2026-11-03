# lua-test-scripts

## Purpose

Runnable Lua smoke tests under `tests/lua/` that exercise the new `Mesh` / `Material` / `MeshUtil` / `MeshSimplifier` / `Scene.ForEach*` bindings end-to-end via `./fury exec`. These scripts are binding smoke tests (not feature duplicates of `fury convert` / `fury info`) and double as worked examples for agents.
## Requirements
### Requirement: `tests/lua/` SHALL contain binding-smoke regression guards invocable via `fury exec`

A `tests/lua/` directory SHALL exist at the repository root (sibling to `examples/`). It SHALL contain at least three Lua scripts, each invocable via `./fury exec <scene> tests/lua/<script>.lua [args...]`.

The scripts are **binding smoke tests**, not replicas of existing CLI features. The repo already ships `fury convert` (headless format conversion) and `fury info` (headless scene summarization); these scripts do not duplicate that functionality. Their purpose is to prove the new Lua bindings work end-to-end from the `exec` path, and to serve as worked examples for agents.

Each script SHALL exit non-zero on invariant violation (exit code 1 for user error / binding failure, code 2 for internal C++ exception escaping `DoExec`). Scripts whose purpose is to author or transform a scene asset SHALL live beside the relevant example or under `tests/lua/scenes/` when they are deterministic test fixtures; see the `lua-test-organization` capability.

The required binding-smoke scripts SHALL include at minimum:

- **`tests/lua/scene_archive_roundtrip.lua`** — save and reload fresh scenes through JSON and compressed BIN in isolated asset directories, compare structural counts and semantic fingerprints, verify string/clone texture identity, and exercise malformed and compatibility inputs. Reports structured field-path differences instead of only proving that the save binding writes a file.

- **`tests/lua/scene_smoke_iterate.lua`** — exercise the `Scene.ForEachMesh` / `ForEachMaterial` / `ForEachNode` iteration bindings. Calls each in turn against `Scene.GetActive()`, counts callbacks, and logs the counts to stdout. Read-only — does not write any file. Does NOT summarize a scene as a feature (that's `fury info`'s job); only proves the iteration bindings work.

The `examples/GenLod.lua` script (scene-generation, not a binding smoke) is documented separately in the `lua-test-organization` capability.

A `tests/lua/README.md` SHALL exist describing the folder split, the feature-prefix naming convention, the CLI non-overlap invariant, and a list of the scripts removed in this change with reasons.

#### Scenario: scene_archive_roundtrip.lua round-trips both formats
- **WHEN** a user runs `./fury exec examples/Projects/feature/feature.bin tests/lua/scene_archive_roundtrip.lua examples/Projects/feature/feature.bin /tmp/feature-archive`
- **THEN** fresh scenes load from isolated JSON and BIN outputs
- **AND** structural counts and semantic fingerprints match under their exactness and tolerance rules
- **AND** the process exits 0

#### Scenario: scene_archive_roundtrip.lua rejects malformed input
- **WHEN** the harness loads a document with an invalid required field, unsupported version, wrong Vector4 arity, short uniform array, or truncated LZ4 envelope
- **THEN** the load fails without installing a partial active scene
- **AND** the process exits 0 after confirming every expected rejection

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

