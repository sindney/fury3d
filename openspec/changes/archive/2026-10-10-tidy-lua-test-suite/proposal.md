## Why

`tests/lua/` has grown to 54 scripts with inconsistent naming, hidden duplication of CLI features, debug-only scripts mixed with regression guards, and a sample scene-generation script (`gen_lod.lua`) sitting next to true smoke tests. The `lua-test-scripts` spec only describes the original 3 scripts; nothing captures the broader shape. The script `tests/pak_pipeline.sh` references a `furye-cli` binary that is not built in this configuration, so the cook/package/pak pipeline test cannot run from a fresh checkout.

## What Changes

- Move scene-generation / one-off sample scripts out of `tests/lua/` into `tests/lua/scenes/` so the binding-smoke folder holds only regression guards.
- Rename the binding-smoke scripts with feature prefixes (`physics_*`, `game_ui_*`, `editor_*`, `ocean_*`, `buoyancy_*`, `terrain_*`, `vegetation_*`, `particle_*`, `cloud_*`, `render_*`) so same-feature tests group together when sorted.
- Merge small same-feature tests where they assert on the same surface (e.g. the two `asset_path_identity.lua` / `content_browser_folders.lua` scripts, the redundant dump helpers).
- Delete debug-only repro scripts that produce no automated assert (the windowed `game_ui_close*`/`game_ui_osexit*`/`game_ui_look_probe*`/`game_ui_options_shot*`/`game_ui_pause_shot*`/`game_ui_slider_probe*` harnesses; `dump_nodes.lua`, `dump_ground.lua`, `dump_lights.lua`).
- Try to run each remaining script first. Scripts that fail to run in this environment are deleted when small, and flagged for user decision when large.
- Build `furye-cli` (the cook/package/pak binary) at apply time so `tests/pak_pipeline.sh` becomes runnable. If the build fetches from the network and network access fails, hold and surface the blocker instead of working around it.
- Update `tests/lua/README.md` to reflect the new folder + naming convention and to list the scripts that were removed.

**BREAKING**: the file paths referenced by `tests/cloud_visual_suite.sh`, `tests/prez_ab_suite.sh`, and `tests/pak_pipeline.sh` change. The shell wrappers must be updated in the same change.

## Capabilities

### New Capabilities

- `lua-test-organization`: defines the folder split (`tests/lua/` for binding-smoke / regression guards; `tests/lua/scenes/` for sample one-off scripts), the feature-prefix naming convention, and the deletion rules for debug-only repros.
- `lua-test-runnable-pipeline`: defines the precondition that `tests/pak_pipeline.sh` requires `examples/furye-cli` to be built locally (or the script is marked skipped with a clear reason).

### Modified Capabilities

- `lua-test-scripts`: the existing capability describes three scripts (`smoke_save.lua`, `smoke_iterate.lua`, `gen_lod.lua`). Under the new layout `gen_lod.lua` lives under `tests/lua/scenes/` (scene-generation, not binding smoke), and the surviving smoke scripts follow the new prefix convention. The spec is rewritten to describe the convention rather than naming individual files.

## Impact

- `tests/lua/` and `tests/lua/scenes/` (new folder).
- `tests/lua/README.md` rewritten.
- `tests/cloud_visual_suite.sh`, `tests/prez_ab_suite.sh`, `tests/pak_pipeline.sh` updated to point at renamed scripts.
- Build system: a `FURYE_CLI_BUILD` task added under `openspec/changes/tidy-lua-test-suite/tasks.md` that produces `examples/furye-cli` so the pak pipeline becomes runnable. No source-file changes outside `tests/` and the spec doc.
- 54 Lua scripts renamed, deleted, or relocated.