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

## Indexed indirect smoke

Run `examples/fury vulkan-indirect-smoke` from the repository root. The command
uses a hidden Vulkan window, writes three indexed commands and a GPU count of
two, then checks readback for base instances 3 and 7 while the zero-instance
slot for ID 11 remains untouched. It also injects a stale legacy mesh binding
and checks the explicit indirect position stream and nonzero second `firstIndex`
(`positions=1`). The fragment shader also samples a known-color texture and
validates the render target, while vertex and fragment uniform checks exercise
the globals required by real GBuffer draws. The vertex scale observation also
requires a stride-0 constant-fill tangent stream, matching empty mesh channels.
The third Metal command has `instanceCount=1` for ID 11 but lies beyond the GPU
range, so full-capacity execution fails the test. Success prints
`gpuCount=2 id3=3 id7=3 id11=0`.
Exit 77 means the required indexed-indirect or first-instance features are
unavailable; exit 1 is a renderer/shader/readback failure.

On native Metal, run `examples/fury metal-indirect-smoke`. It executes the same
commands through GPU-encoded indexed ICBs and a GPU-written execution range.
Metal PSO variants opt into `supportIndirectCommandBuffers`; fragment textures
and samplers use a descriptor-set argument buffer encoded into each command,
and indirect vertex streams use slots 26-29 to avoid storage-buffer collisions.
The probe reads back `range=0,2` in addition to the semantic draw checks.
Success prints `mode=gpu-encoded-icb-range gpuCount=2 id3=3 id7=3 id11=0`.

GPU-written indirect counts are required. Unsupported devices, including
MoltenVK, skip the smoke with exit 77 and use legacy rendering for cluster LOD;
no single-indirect emulation is used. Native `mdi-count` and gfxrecon MDI
acceptance must run on a device with `drawIndirectCount` enabled.

On a supported native Vulkan device, capture and inspect the smoke with:

```bash
OUT=/tmp/fury-vk-indirect-capture
gfxrecon-capture-vulkan.py -w "$OUT" -o indirect.gfxr --no-file-timestamp \
  --file-flush examples/fury vulkan-indirect-smoke
gfxrecon-replay --validate --sync "$OUT/indirect.gfxr"
gfxrecon-info --verbose "$OUT/indirect.gfxr"
gfxrecon-convert --expand-flags --format json \
  --output "$OUT/indirect.json" "$OUT/indirect.gfxr"
```

Inspect compute-to-indirect barriers, indexed indirect-count arguments, count
offsets, and readback copies. Capture/replay results supplement the semantic
count and base-instance output; they do not replace it.

On macOS, `test-artifacts/gfxrecon-kosmickrisp/COMMANDS.sh` retains the exact
ICD selection, local gfxrecon-cli inspection commands, smoke capture evidence,
and the KosmicKrisp tank swapchain crash. Its non-MoltenVK smoke proves the
MDI-count path, but the tank capture/replay lane remains unrun. MoltenVK is
unsupported for cluster LOD and does not substitute for native MDI validation.

## Geometry cluster LOD comparisons

Run the reusable feature-scene comparison from the repository root:

```bash
python3 -I tests/cluster_lod_render.py --backend metal --artifacts /tmp/cluster-lod-metal
python3 -I tests/cluster_lod_render.py --backend vulkan --artifacts /tmp/cluster-lod-vulkan
```

The driver checks the complete level-zero cut against legacy, near/far adaptive
reduction, and deterministic whole-instance budget fallback. It uses
`FURY_CAM` for pinned views, `FURY_CLUSTER_LOD=1` to enable geometry clusters,
and `FURY_CLUSTER_LOD_ERROR=0` for the all-leaf test cut. Editor screen-error
controls remain positive. `FURY_CLUSTER_LOD_BUDGET` supplies the test capacity
limit. Each run writes logs, images, GPU dumps, and `summary.json`. On a Vulkan
device without `drawIndirectCount`, including MoltenVK, the driver instead
verifies the legacy image and records an expected unsupported-platform skip.

For viewport diagnostics, use the editor's Geometry Clusters menu or the player
hooks `FURY_CLUSTER_LOD_DEBUG=lod` and `FURY_CLUSTER_LOD_DEBUG=id`.
`FURY_CLUSTER_LOD_DEBUG_CYCLE=1` renders LOD, ID, then normal shading in one
process. `--gpu-dump` adds on-demand selected, visible, command, triangle,
batch, argument, and Metal execution-range diagnostics without normal-frame
readback. See `docs/ClusterLOD.md` for backend contracts and open platform lanes.

Run the real feature scene from an isolated pak with no source assets or host
DDC on both macOS backends:

```bash
PACKAGED_TEST_KEEP=1 OUT_DIR=/tmp/cluster-lod-packaged \
  tests/cluster_lod_packaged.sh
```

The harness copies `fury` into an empty runtime directory, packages the feature
scene and `Player.lua` with Metal and Vulkan shaders, and checks frame-30
cluster statistics, cooked shader hits, and screenshots. Existing output
directories are preserved unless `PACKAGED_TEST_REPLACE=1` is set.

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
