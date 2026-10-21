# tests/lua/ — runnable Lua smoke tests for `fury exec`

This folder holds Lua scripts invoked via `./fury exec <scene> <script.lua> [args...]`.
Two sub-conventions:

- **`tests/lua/`** — binding-smoke regression guards. Each script exits
  non-zero on invariant violation and is suitable for headless CI.
- **`tests/lua/scenes/`** — scene-generation samples and render harnesses.
  Some produce an asset (LOD chain, populated island) that the caller
  inspects by hand; others run a windowed harness used by the shell
  suites in `tests/`.

Scripts in `tests/lua/` are invoked via the `fury exec` contract documented in
`docs/CLI.md` (no SFML window, no `Engine::Initialize`, no GL context on the
`exec` path). Scripts in `tests/lua/scenes/` open a window and drive
`Engine.run`; see `tests/lua/scenes/README.md`.

## Naming convention

Regression-guard scripts in `tests/lua/` begin with a feature prefix that
matches one of the engine systems the script exercises:

- `physics_*`, `game_ui_*`, `editor_*`, `ocean_*`, `buoyancy_*`,
  `terrain_*`, `vegetation_*`, `particle_*`, `cloud_*`, `render_*`,
  `shadow_*`, `scene_*`, `joystick_*`, `kraut_*`, `pak_*`, `pipeline_*`.

Generic binding-smoke scripts that exercise only the scene-graph or
material iteration surface use `scene_*`. Scripts under `tests/lua/scenes/`
may omit the prefix.

## Surviving regression guards

- `scene_smoke_iterate.lua` — exercise `Scene.ForEach*` iteration bindings.
  Read-only; not a replacement for `fury info`.
- `scene_smoke_save.lua` — round-trip a scene through the Lua save
  binding. Not a replacement for `fury convert`.
- `scene_asset_path_identity.lua` — merged unique-path-identity invariant
  covering both the bundled `ocean_island.bin` (legacy collapse) and a
  synthetic Terrain scene (deliberate duplicate). Replaces the prior
  `asset_path_identity.lua` + `content_browser_folders.lua` pair.
- `physics_smoke.lua` — `BodySetup` box drop + rest + save/reload.
- `terrain_physics.lua` — Terrain heightfield + chunk-meshes-excluded
  invariant.
- `terrain_sky_scene.lua` — outdoor_terrain.bin Sky + Terrain assertions.
- `buoyancy_smoke.lua` — `BuoyancyComponent` flat / wave / save/reload.
- `ocean_scene_roundtrip.lua` — every `OceanComponent` field round-trip.
- `ocean_asset_flow.lua` — `OceanWaves` transferred through `MergeInto`.
- `ocean_waves_spec.lua` — `WaveSampler` loop/tile wrap, determinism.
- `cloud_sky_roundtrip.lua` — `SkyAtmosphere` cloud block + TOD.
- `scene_node_picker_roundtrip.lua` — `*NodeName` setter round-trip.
- `pak_scene_load.lua` — loose scene vs scene.pak identical enumeration
  (also called by `tests/pak_pipeline.sh`).
- `kraut_import_roundtrip.lua` — Kraut-imported `*_LOD0` chain.
- `pipeline_compile_check.lua` — load both PBR + Lambert pipelines.
- `editor_only_nodes.lua` — `editorOnly` flag serialization.
- `joystick_smoke.lua` — `JoyInputUtil` bindings + `JoystickAxis`.
- `render_smoke_settings_roundtrip.lua` — `RenderSettings` save/reload.
- `shadow_smoke_receive.lua` — shadow-receive + particle resolution.
- `particle_smoke.lua` — `ParticleSystem` name resolution + save/reload.
- `game_ui_smoke.lua`, `game_ui.lua`, `game_ui_capture.lua`,
  `game_ui_demo.lua`, `game_ui_settings_persist.lua`,
  `game_ui_slider_param.lua`, `game_ui_spawn.lua`, `game_ui_swim.lua`,
  `game_ui_window.lua` — RmlUi + IslandGame regression guards.

## Removed in this change

- `dump_nodes.lua`, `dump_ground.lua`, `dump_lights.lua` — debug-only
  scene dumpers. No assert; the iteration pattern is covered by
  `scene_smoke_iterate.lua`.
- `game_ui_close.lua`, `game_ui_close_all.lua`, `game_ui_look_probe.lua`,
  `game_ui_options_shot.lua`, `game_ui_osexit.lua`,
  `game_ui_pause_shot.lua`, `game_ui_slider_probe.lua` — windowed repros
  for manual screenshot review; no automated assert.
- `smoke_hdr_import.lua` — 29-line HDR-import binding smoke; removed when its
  required `examples/Projects/samples/tank/tank.fbx` fixture was missing.
- `asset_path_identity.lua` — 71-line unique-path test that hit a STATUS_BREAKPOINT
  in `Importer.LoadScene` with absolute `/tmp/...` paths. Invariant kept
  under `scene_asset_path_identity.lua` (fixture A).
- `content_browser_folders.lua` — 177-line synthetic-fixture twin of the
  above. Invariant kept under `scene_asset_path_identity.lua` (fixture B).

## Scene samples (`tests/lua/scenes/`)

See `tests/lua/scenes/README.md` for the moved sample / harness scripts.
