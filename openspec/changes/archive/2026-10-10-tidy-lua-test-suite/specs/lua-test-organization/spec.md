# lua-test-organization

## Purpose

Conventions for the Lua test suite under `tests/lua/`: how regression-guard scripts are kept distinct from one-off scene-generation samples, the feature-prefix naming convention, and the deletion rules for debug-only repro scripts.

## ADDED Requirements

### Requirement: Binding-smoke and regression-guard scripts SHALL live in `tests/lua/`; scene-generation samples SHALL live in `tests/lua/scenes/`

`tests/lua/` SHALL contain only Lua scripts that assert a binding or a component invariant against a scene, and that exit non-zero on failure (regression guards). One-off scripts whose purpose is to author or transform a scene asset (the output is a `.bin` / `.json` consumed by other tooling, not a pass/fail signal) SHALL live in `tests/lua/scenes/`.

A script's category SHALL be inferred from its exit code and output, not its name: a `tests/lua/<x>.lua` script MUST exit non-zero on invariant violation; a `tests/lua/scenes/<x>.lua` script is allowed to exit 0 unconditionally even when the produced asset is malformed (the caller inspects the output by hand).

#### Scenario: Scene-generation scripts are not regression guards
- **WHEN** a script in `tests/lua/scenes/` is invoked
- **THEN** it MAY exit 0 unconditionally after producing an output asset
- **AND** it is NOT required to assert invariant violations

#### Scenario: Binding-smoke scripts exit non-zero on failure
- **WHEN** a script in `tests/lua/` is invoked against a broken scene
- **THEN** the process exits with code 1 (invariant violation) or 2 (internal C++ exception escaping `DoExec`)

### Requirement: Binding-smoke scripts SHALL be named with a feature prefix so same-feature tests sort together

Every script in `tests/lua/` SHALL begin with a feature prefix that matches one of the engine systems it exercises: `physics_`, `game_ui_`, `editor_`, `ocean_`, `buoyancy_`, `terrain_`, `vegetation_`, `particle_`, `cloud_`, `render_`, `shadow_`, `scene_`, `joystick_`, `kraut_`, `pak_`, `pipeline_`. A script that exercises more than one feature SHALL pick the dominant feature for its prefix (the prefix exists to group scripts, not to encode all features).

Generic binding-smoke scripts that exercise only the scene-graph or material iteration surface (no specific feature) MAY use the `scene_` prefix or, if they are explicitly part of a single feature's spec, that feature's prefix.

#### Scenario: Two scripts sharing a feature share a prefix
- **WHEN** two scripts assert on the same engine feature
- **THEN** both filenames begin with the same feature prefix
- **AND** an `ls tests/lua/ | sort` places them adjacent

#### Scenario: Scene-generation scripts are exempt from the prefix rule
- **WHEN** a script lives in `tests/lua/scenes/`
- **THEN** its filename MAY omit a feature prefix

### Requirement: Debug-only repro scripts SHALL be deleted from the suite

Scripts whose sole purpose is to reproduce a UI / window / exit-path crash for manual review and that contain no exit-code-based assertion SHALL be removed from the suite. A script is "debug-only" when ALL of the following hold: it calls `Engine.run` without ever calling `os.exit(0)` based on a binding check; it produces no `OK` / `PASS` line that an automated runner can grep for; and it relies on `Window.Close()` or `os.exit()` to terminate at a fixed frame count.

#### Scenario: Repro script with no automated check is removed
- **WHEN** a script only calls `Engine.run({ on_update = function(dt) if frame == 30 then Window.Close() end })`
- **THEN** it is removed from `tests/lua/`

### Requirement: Redundant scripts asserting the same invariant SHALL be merged

When two scripts assert the same invariant against different scene fixtures, the smaller one SHALL be merged into the larger one as an additional `#### Scenario:`-equivalent block, with the original fixture name preserved in a comment.

#### Scenario: Two scripts asserting unique-texture-paths collapse into one
- **WHEN** `asset_path_identity.lua` asserts `Scene.GetTexture(path)` returns the canonical entry across 5 reload cycles against the bundled `ocean_island.bin`
- **AND** `content_browser_folders.lua` asserts the same invariant against a synthetic JSON fixture
- **THEN** the merged script covers both fixtures (bundled legacy + synthetic Terrain)
- **AND** the merged script asserts the shared invariant once
- **AND** the merged script runs against both fixtures back-to-back

### Requirement: Scripts that fail to run SHALL be deleted when small, escalated when large

A script that fails to run in the current environment (build error, missing binary, missing scene fixture) is graded by approximate size in lines of code. Scripts under the deletion-threshold line count SHALL be deleted with no further escalation. Scripts at or above the threshold SHALL be surfaced for a user decision before deletion.

#### Scenario: Small broken script is deleted
- **WHEN** a script under the deletion threshold fails to run in this environment
- **THEN** it is deleted
- **AND** the deletion is logged in the change's `tasks.md`

#### Scenario: Large broken script is escalated
- **WHEN** a script at or above the deletion threshold fails to run
- **THEN** the failure is reported with the script's size and reason
- **AND** deletion requires a user decision before proceeding

### Requirement: `tests/lua/README.md` SHALL describe the new organization

`tests/lua/README.md` SHALL explain the `tests/lua/` vs `tests/lua/scenes/` split, list the feature prefixes, and note which scripts were removed (with the reason: debug-only, redundant, or broken). It SHALL retain the existing convention notes (no-window / no-engine-boot / no-GL-context for `fury exec`, exit-code contract).

#### Scenario: README documents the folder split
- **WHEN** the repository is inspected
- **THEN** `tests/lua/README.md` exists
- **AND** it describes the `tests/lua/` (regression guards) vs `tests/lua/scenes/` (sample one-offs) split
- **AND** it lists the feature prefixes
- **AND** it lists the scripts removed in this change with reasons

### Requirement: Shell wrappers that reference renamed scripts SHALL be updated in the same change

`tests/cloud_visual_suite.sh`, `tests/prez_ab_suite.sh`, and any other shell wrapper that names a Lua script SHALL be updated in the same change as the rename. Renames SHALL NOT land without a matching wrapper update.

#### Scenario: Wrapper script points at renamed test
- **WHEN** a Lua script is renamed or moved
- **THEN** every shell wrapper that referenced the old path is updated in the same change
- **AND** no wrapper references a deleted script