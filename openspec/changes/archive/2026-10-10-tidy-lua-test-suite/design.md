## Context

`tests/lua/` has 54 scripts. They came from many feature PRs (cloud quality, ocean waves, kraut vegetation, terrain physics, IslandGame UI, particle systems, ...) and accumulated without a single organizing principle: file names are flat, debug-only repros sit next to true regression guards, and at least one suite (`tests/pak_pipeline.sh`) needs a `furye-cli` binary that this build configuration does not produce.

The `lua-test-scripts` spec at `openspec/specs/lua-test-scripts/spec.md` describes only the original three scripts (`smoke_save.lua`, `smoke_iterate.lua`, `gen_lod.lua`); nothing in `openspec/specs/` captures the broader shape of the suite or the deletion / rename rules an apply step would need to follow. The user's request is to fix that: combine small same-feature tests, delete redundancies, delete debug repros, rename with feature prefixes, and move scene-gen scripts out of the binding-smoke folder.

## Goals / Non-Goals

**Goals**

- A folder split where `tests/lua/` is regression-guard territory and `tests/lua/scenes/` is sample / one-off territory, with each file sorted into the right place.
- A feature-prefix naming convention so `ls tests/lua/ | sort` groups same-feature tests together.
- Concrete deletion decisions on the redundant and debug-only scripts identified in the inventory.
- A runnable pak pipeline, achieved by building `examples/furye-cli` so `tests/pak_pipeline.sh` works.
- Updated shell wrappers (`cloud_visual_suite.sh`, `prez_ab_suite.sh`, `pak_pipeline.sh`) that point at the renamed scripts.

**Non-Goals**

- Adding new tests or expanding coverage.
- Restructuring the C++ test surface (CMake test targets, CTest).
- Modifying the CLI itself (`Cli.cpp`, the subcommand table in `docs/CLI.md`).
- Changing the `fury` or `furye` build configurations.
- Renaming scripts whose behavior is already exercised by an existing shell wrapper that the apply step does not need to touch.

## Decisions

### Decision 1: Folder split is `tests/lua/` (regression guards) vs `tests/lua/scenes/` (samples)

Scripts are split by exit-code contract, not by topic. A regression guard exits non-zero on invariant violation; a sample produces an asset (and the caller inspects the result by hand). `tests/lua/scenes/` is the new home for `gen_lod.lua`, `populate_island_grass.lua`, `cloud_visual_check.lua`, `pipeline_compile_check.lua`, `tree_preview.lua`, `instancing_smoke.lua`, `vegetation_perf.lua`, `grass_field_perf.lua`, `grass_island_check.lua`, `lod_transition_check.lua`, and `billboard_pop_check.lua` — all of which either render an output the caller reviews or are render harnesses for shell suites.

**Alternatives considered**: A flat split by topic (`physics/`, `game_ui/`, `cloud/`). Rejected because the binding-smoke scripts are individually small (most under 200 lines) and a folder-per-feature would create empty folders; a flat folder with feature prefixes achieves the same grouping without the directory proliferation.

### Decision 2: Prefix taxonomy is feature-named, not component-named

Prefixes are the engine system a script asserts on: `physics_`, `game_ui_`, `editor_`, `ocean_`, `buoyancy_`, `terrain_`, `vegetation_`, `particle_`, `cloud_`, `render_`, `shadow_`, `scene_`, `joystick_`, `kraut_`, `pak_`, `pipeline_`. The "use the dominant feature" rule keeps the taxonomy flat.

**Alternatives considered**: Component-named prefixes (`body_setup_`, `character_controller_`). Rejected because the same script frequently asserts on multiple components of one feature (e.g. `physics_smoke.lua` covers `BodySetup`, `PhysicsWorld`, and the default gravity in one script), and component prefixes would force an arbitrary choice per script.

### Decision 3: Deletion is gated by size and runnability

The user asked: "try run the tests first; if it can't run or fails, if the test is small then we can treat it as broken and delete, if it's big and you can decide ask me." The size threshold for escalation is **150 lines**. Below 150 lines, a script that fails to run is deleted and logged in `tasks.md`. At or above 150 lines, the apply step surfaces the script with its size and reason and asks before deleting.

**Alternatives considered**: Delete all broken scripts unconditionally. Rejected because the user explicitly asked for escalation on large scripts.

### Decision 4: Redundant scripts are merged, not deleted, when the union is small

When two scripts assert the same invariant against different fixtures (e.g. `asset_path_identity.lua` against `ocean_island.bin` and `content_browser_folders.lua` against a synthetic JSON), they are merged into a single script that runs both fixtures back-to-back. The merge keeps both fixtures represented (one covers legacy pre-fix duplicates, the other covers the hand-authored duplicate path).

**Alternatives considered**: Delete one of the two. Rejected because each fixture exercises a distinct code path — the legacy same-path collapse in the bundled `.bin` is a different invariant from the deliberate-duplicate JSON case.

### Decision 5: `furye-cli` is built using `FURY_HEADLESS_CLI=1`, reusing existing source

The build target is registered in the existing CMake graph. `examples/main.cpp` already gates on `FURY_HEADLESS_CLI` (see `Cli.cpp` lines 90–94 for the matching `kProgName` selection). No source changes are needed; only a new build target and a script entry in `tasks.md` to invoke it.

**Alternatives considered**: Renaming `furye` to `furye-cli`. Rejected — `furye` is the editor GUI binary; the two serve different roles and both ship from this build.

### Decision 6: Build failure on missing dependency halts, does not fetch

The user said "if fails to build (requires downloading sth while cmake for example), then hold and stop there, because we do have issue connecting github now". The build's behavior on a missing dependency is fail-fast with a clear stderr message — same as `pak_pipeline.sh`'s `[ -x "$FURYCLI" ]` guard. The `lua-test-runnable-pipeline` spec captures this as a requirement.

**Alternatives considered**: Pre-fetch dependencies during the apply step. Rejected — the user explicitly forbade network access.

## Risks / Trade-offs

- [A rename breaks a third-party CI script that points at the old path] → Mitigation: grep the repo for every reference to a renamed script (shell wrappers, docs, `.openspec.yaml`-like config) and update in the same change. The shell wrappers are updated under `tests/cloud_visual_suite.sh`, `tests/prez_ab_suite.sh`, `tests/pak_pipeline.sh`.
- [The apply step cannot determine which scripts are runnable in this environment because the build is broken (missing `furye-cli`)] → Mitigation: the apply step runs the build target first. If the build fails for network reasons, the apply step halts and surfaces the build error before touching the test files. Scripts whose runnability cannot be determined are escalated per Decision 3.
- [Merging `asset_path_identity.lua` and `content_browser_folders.lua` loses the original failure-mode history] → Mitigation: the merged script keeps both fixtures and logs which fixture the assertion is running against, so a regression still points to the failing scenario.
- [The `furye-cli` build target reuses the same source as `fury` / `furye` but compiles in a different mode; a refactor that breaks the `FURY_HEADLESS_CLI` branch would silently break the pak pipeline] → Mitigation: the apply step verifies the binary with `./furye-cli --help` after building and asserts the output lists `cook`, `package`, `exec-script` before declaring success.
- [Some scripts reference scene fixtures (`Projects/ocean/ocean_island.bin`, `Examples/Projects/outdoor/outdoor_water.bin`) that exist in the working tree but may not exist in a fresh clone] → Mitigation: the apply step lists every fixture each surviving script needs and updates `tests/lua/README.md` accordingly.

## Migration Plan

1. Build `furye-cli`. If the build fetches dependencies, halt.
2. Run each existing script in `tests/lua/`. Record pass / fail / can't-run.
3. For each script under the deletion threshold that fails, delete and log.
4. For each script at or above the deletion threshold that fails, surface and ask.
5. Apply the rename and merge plan from `tasks.md`.
6. Update shell wrappers and `README.md`.
7. Re-run the surviving scripts to confirm the renames don't break them.

Rollback: each rename is a single file move; a `git mv` is recoverable from the index if the change is rejected at review time.

## Open Questions

- Should `tests/lua/scenes/` get a `README.md` of its own, or does the top-level `tests/lua/README.md` cover both folders? Open; defaulting to a single README until the apply step surfaces a need for a scenes-specific doc.
- Should the `tests/cloud_visual_suite.sh` and `tests/prez_ab_suite.sh` suites share their scaffolding (camera env-var parsing, screenshot plumbing, summary printer) via a common helper? Open; the apply step may consolidate if the duplication is mechanical, otherwise left as-is.