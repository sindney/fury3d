# Test automation

The maintained suite has three lanes declared in `tests/manifest.json` and
executed by `tests/run.py`:

- `headless`: `fury exec` scene, physics, serialization, game UI, cook, and pak
  checks. No display or GPU context is required.
- `ui`: injected Dear ImGui input in the separate `furye-ui-tests` binary. A
  windowing display is required; production binaries contain no test engine.
- `render`: windowed captures and image/semantic checks on a real GPU. Baselines
  are separate for OpenGL and Metal.

## Build and list

From the repository root:

```bash
cmake -S engine -B build
cmake --build build --target fury furye furye-cli furye-ui-tests -j2
python3 -I tests/run.py list
python3 -I tests/run.py list --lane render --filter island
```

The runner uses existing binaries and never builds or replaces them. Override
binary locations with `--fury`, `--furye`, and `--furye-cli` when needed.

## Run lanes

```bash
python3 -I tests/run.py run --lane headless --artifacts /tmp/fury-headless
python3 -I tests/run.py run --lane ui --artifacts /tmp/fury-ui-lane
python3 -I tests/run.py run --lane render --artifacts /tmp/fury-render
python3 -I tests/run.py run --filter scene_archive_roundtrip --artifacts /tmp/fury-archive
```

Selectors can be combined: `--lane`, repeatable `--tag`, and repeatable
`--filter`. Tags and filters are ORed within their category and ANDed across
categories. `--timeout-multiplier` scales manifest timeouts.

Run the editor suite from `examples/`:

```bash
./furye-ui-tests Editor.lua --ui-test --render-thread=0 \
  --ui-test-artifacts /tmp/fury-ui --ui-test-timeout 120
./furye-ui-tests Editor.lua --ui-test --render-thread=1 \
  --ui-test-artifacts /tmp/fury-ui-render-thread --ui-test-timeout 120
```

Use `--ui-test-filter <substring>` to select editor tests. Unknown filters,
failed tests, and timeouts return non-zero. The UI lane also includes a
render-thread smoke and an intentional `failure_probe` entry whose expected
exit 1 verifies failure captures and JUnit reporting. UI runs use isolated scenes,
settings, native-dialog results, and ImGui ini files; they do not read or write
`examples/imgui.ini` or project game settings.

`ui_saved_scene_verify` consumes the scene written by `editor_ui_suite` in the
same artifact directory. A headless-only run against a fresh artifact directory
skips it by design; run the `editor_ui` and `ui_saved` filters together to verify
the complete save path.

## Fixtures

Canonical scenes are checked in. Rebuild them only when authoring content
changes, from `examples/`:

```bash
./fury exec Projects/samples/fox/Fox.gltf Projects/feature/setup_feature_scene.lua
./fury exec Projects/ocean/ocean_base.bin Projects/ocean/setup_ocean_island.lua
./fury Projects/ocean/populate_island_grass.lua /tmp/ocean_island_grass.bin
cp /tmp/ocean_island_grass.bin Projects/ocean/ocean_island.bin
./fury exec Projects/ocean/ocean_island.bin Projects/ocean/setup_ocean_island_trees.lua
```

`GenLod.lua` is a general scene-authoring tool, not a regression guard:

```bash
./fury exec Projects/feature/feature.bin GenLod.lua --all /tmp/feature_lod.bin
```

The Kraut import check requires `/tmp/kraut_gen/PalmTree2.bin`; the runner
reports it as skipped when that external fixture is absent. Other missing
preconditions are named in the per-test result.

## Artifacts and exit codes

Each runner invocation writes `summary.json` and `logs/<test-id>.log` below
`--artifacts` (default `test-artifacts/<timestamp>`). Scenario drivers add
captures, capture logs, metrics, and JUnit XML where applicable. Relative
artifact paths are resolved from the caller's working directory.

Runner exit codes are 0 when every executed test passed or was skipped for a
missing precondition, and 1 for a failed test, timeout, or startup error.
Direct UI and render drivers use the same 0/1 convention. Logs and semantic
results determine failures; a screenshot alone is never approval.

## Render baselines

Core scenarios are `feature_hdr_off`, `feature_hdr_on`, and `island`, with
OpenGL and Metal baselines under `tests/baselines/<backend>/`. Configuration,
regions, temporal checks, and tolerances live in `tests/render_scenarios.json`.
A missing baseline fails and leaves the actual capture for review.

Update a baseline only as an explicit review operation, for example:

```bash
python3 -I tests/run.py run --filter render_feature_hdr_off_opengl \
  --update-baselines --artifacts /tmp/fury-baseline-review
```

Review the actual capture and metadata before accepting the updated files.
Threshold failures retain actual, expected, diff, and metrics artifacts. PNG
byte equality and cross-backend pixel identity are not required.

Cloud, PreZ, and OpenGL/Metal parity checks are named cases in
`tests/render_regressions.json`, driven by `tests/render_regression.py` through
the same aggregate runner. Select them with filters such as `render_cloud`,
`render_prez`, or `render_backend_parity`.

## Retained standalone harnesses

These cover manual visual inspection or performance and are not functional
baseline gates:

- `tests/lua/scenes/tree_preview.lua`: Kraut LOD/wind/billboard inspection.
- `tests/lua/scenes/instancing_smoke.lua`: instanced draw-call behavior.
- `tests/lua/scenes/vegetation_perf.lua`: island vegetation timing.
- `tests/lua/scenes/grass_field_perf.lua` and `grass_island_check.lua`: grass
  density, composition, and timing.
- `tests/lua/scenes/lod_transition_check.lua` and `billboard_pop_check.lua`:
  LOD transition visual review.
- `tests/prez_perf.sh`: PreZ on/off CPU-proxy timing; not registered in the
  functional lanes. Set `OUT_DIR` to override `/tmp/prez_perf`.

`tests/pak_pipeline.sh` remains a staged cook/package integration harness and
is invoked by the headless manifest. `tests/check_engine_ascii.py` is the ASCII
source guard. See `tests/lua/README.md` and `tests/lua/scenes/README.md` for
script-level details.
