## 1. Baseline and coverage inventory

- [x] 1.1 Record current `fury info` structural counts and save/reload behavior for the legacy scene, ocean island, outdoor fixtures, and available sample scenes in isolated temporary directories.
- [x] 1.2 Record current cook/package texture and sidecar reference sets and run the existing pak pipeline as the pre-migration baseline.
- [x] 1.3 Inventory every Lua and shell test by lane, fixture, assertions, unique coverage, window/GPU requirement, and current pass/fail status; correct the classification of windowed guards in the inventory.
- [x] 1.4 Inventory example scenes, launchers, source assets, generated outputs, and repository references. Identify redundant candidates without deleting coverage or the grouped files under `examples/Projects/samples`.
- [x] 1.5 Capture available current render outputs and same-backend noise measurements for existing visual suites, and record which OpenGL, Metal, Vulkan, UI-display, and ASAN lanes are available.

## 2. Canonical showcase scenes

- [x] 2.1 Restore the small Fox glTF payload from git history into `examples/Projects/samples/fox/`; verify tank, Fox, and james imports headlessly and keep these as the single model-source copies.
- [x] 2.2 Delegate the self-contained feature-scene authoring workstream: create an idempotent `examples/Projects/feature/setup_feature_scene.lua` that imports and positions tanks, Fox, and james using measured world AABBs, attaches Animators, and selects stable animation clips.
- [x] 2.3 In the feature builder, merge the outdoor `FireEmber` and `SmokePlume` definitions, retain or retarget their textures in durable project assets, remove unrelated outdoor nodes, and verify both particle systems resolve after reload.
- [x] 2.4 Add named directional-CSM, point, and spot lights, a stable inspection camera, PBR/HDR/CSM render settings, and save the generated scene as `examples/Projects/feature/feature.bin`.
- [x] 2.5 Add `examples/FeatureGame.lua` and a small feature/HDR RmlUi overlay. Promote the responsive IslandGame stylesheet and shared font resources to `examples/Resource/Ui/`, and support reproducible HDR-on/HDR-off runs without activating IslandGame's state machine.
- [x] 2.6 Delegate the independent island rebuild workstream: restore and adapt the seeded PalmTree2 HISM builder, compose it with the existing base/island terrain and infinite-ocean chain, and deterministically create `PlayerSpawn` and all node names required by IslandGame.
- [x] 2.7 Regenerate `examples/Projects/ocean/ocean_island.bin` from the reproducible chain, omit orphaned Tree1 content, preserve terrain collision and beach spawn, and verify IslandGame menu, Start, pause, HUD, options, and player flow.
- [x] 2.8 Add `tests/lua/scene_sample_scenes.lua` to assert both scenes' required content, resource paths, lights, CSM/HDR settings, particle definitions, skeletal clips/Animators, island terrain/ocean, and nonempty kraut HISM/LOD content through the headless CLI.

## 3. Archive foundations and round-trip harness

- [x] 3.1 Add `Archive` with Loading/Saving/Cooking modes, scene/entity-manager context, version, sticky status, nested error paths, and required/optional/conditional field operations.
- [x] 3.2 Implement named-field JSON reader/writer archives, `<<` operators, object/array scopes, primitive/container/value-type support, and typed name/path resource-reference helpers.
- [x] 3.3 Add the temporary `Serializable::Serialize` adapter for unmigrated Load/Save types and route `FileUtil` JSON, LZ4, extension-dispatch, and in-memory paths through archive construction while preserving active-scene and render-thread scopes.
- [x] 3.4 Keep memory-backed texture extraction as file-save preparation only; add tests proving string serialization, clone/duplicate, and cooking do not relocate or mutate textures.
- [x] 3.5 Implement a native persistent-state fingerprint visitor independent of archive output, with canonical unordered-map handling, semantic array ordering, exact fields, float tolerances, and transient-state exclusions; expose the minimal read-only Lua binding needed by tests.
- [x] 3.6 Replace the save-only smoke coverage with `tests/lua/scene_archive_roundtrip.lua`, performing fresh-scene save/load/save cycles for JSON and BIN in isolated asset directories and reporting structured field-path diffs.
- [x] 3.7 Add malformed and compatibility cases for absent optional fields, invalid required fields, Vector4 arity, short uniform arrays, duplicate paths, unknown components, future versions, truncated LZ4, JSON/BIN equivalence, and physics-to-no-physics reloads.
- [x] 3.8 Run the new fingerprint and compatibility harness against the baseline fixtures and both canonical scenes before migrating individual serializers; investigate every difference from the recorded legacy behavior.

## 4. Migrate persistent types to Serialize

- [x] 4.1 Migrate value types and `Entity`, then replace the `bool object` flattening convention with explicit archive scopes while preserving existing field names and defaults.
- [x] 4.2 Migrate MeshRender, InstancedMeshRender, Light, Camera, Animator, ParticleRenderer, and the registered Transform path; centralize component type emission/validation and test every `ComponentRegistry` entry.
- [x] 4.3 Migrate BodySetup, FreeFlyController, CharacterController, SkyAtmosphere, Terrain, OceanComponent, and BuoyancyComponent, keeping loading-only attachment, rebuild, and subscription behavior explicit.
- [x] 4.4 Migrate Texture, Material, Shader, Uniform, Mesh and inline LODs/joints, AnimationClip, ParticleSystem/modules, Heightmap, OceanWaves, and RenderSettings without reviving the unused legacy `lod_group` format.
- [x] 4.5 Migrate SceneNode and Scene, preserving resource load order, editor-only filtering, joint relinking, canonical texture registration, node registration, physics/render settings, working-directory resolution, and documented unknown-component behavior.
- [x] 4.6 Migrate Pipeline, PrelightPipeline, Pass, and PostProcessEffect serialization, including active-pipeline context and ordered postprocess chains.
- [x] 4.7 Migrate all direct callers, including Lua open/save, CLI convert/exec, editor play-mode temporary scenes, in-memory clone/duplicate, and any asset-editor paths; run the round-trip harness after each migration group.

## 5. Cooking integration and legacy API removal

- [x] 5.1 Implement reference-collecting `CookArchive` and cook branches that gather resource metadata without texture decode/upload, terrain rebuild, physics registration, or scene-document writes.
- [x] 5.2 Replace CookPipeline's manual terrain/sky/component reference discovery with the shared serializers and compare the complete texture, sidecar, passthrough, and manifest reference sets against the baseline.
- [x] 5.3 Run `tests/pak_pipeline.sh` and packaged-scene boot checks, then resolve any missing or extra cooked references before removing legacy code.
- [x] 5.4 Remove the temporary adapter and all replaced per-type Load/Save field implementations and `void*` persistence casts; retain only the transport-level FileUtil load/save facade and the shared `Serialize` API.
- [x] 5.5 Update serialization documentation and comments to describe archive modes, required/optional fields, references, error propagation, and the cooking contract.

## 6. Dear ImGui Test Engine host

- [x] 6.1 Vendor a pinned imgui_test_engine revision and prove it compiles against the vendored Dear ImGui 1.92.8 docking sources before implementing the test suite.
- [x] 6.2 Add a separate `furye-ui-tests` target with `WITH_EDITOR=1`, the test-host define, required coroutine implementation, and `IMGUI_ENABLE_TEST_ENGINE` applied consistently; leave production fury/furye/furye-cli targets free of test-engine code.
- [x] 6.3 Implement test-engine lifecycle after `Gui::Initialize` and before `Gui::Shutdown`, game-thread pumping after UI submission and before draw snapshot cloning, pre/post-swap hooks, graceful completion, and timeout handling.
- [x] 6.4 Add automation arguments for test filtering, JUnit output, artifact directory, unattended exit, and render-thread selection; return non-zero for failed tests, empty selections, and timeouts.
- [x] 6.5 Add isolated UI fixtures and test-only services for native dialog results, temporary scene paths, temporary game settings, fixed window/scale, and complete ImGui ini read/write overrides, including Fury's early manual readers.
- [x] 6.6 Add stable automation IDs and window/component/property scopes for hierarchy rows, inspector widgets, Content Browser assets, mesh preview controls, profiler tabs, and File menu commands.
- [x] 6.7 Add one injection smoke test that selects and edits a known node, proving the full host, coroutine, fixture, reporting, and exit-code path before expanding coverage.

## 7. Editor UI automation coverage

- [x] 7.1 Add hierarchy tests for plain and multi-selection, selected-node state, inspector contents, and selection reset after scene replacement.
- [x] 7.2 Add inspector property tests that commit transform and component edits, verify live values and dirty state, exercise supported undo, and centralize missing dirty-marking behavior exposed by the tests.
- [x] 7.3 Add mesh preview tests that drive Content Browser filtering/opening, verify expected mesh metadata and renderable content, and exercise LOD selection and preview camera controls.
- [x] 7.4 Add profiler tests for Perf/GBuffer/Shadows tabs and load the existing minimal GPU graph fixture to verify render-graph and texture-window content without asserting exact FPS or timings.
- [x] 7.5 Add File menu/shortcut open/save tests using injected temporary dialog paths and confirmation handling, then verify the modified saved scene through the independent headless round-trip harness.
- [x] 7.6 Verify failure captures, JUnit content, per-test cleanup, no source-fixture or user-settings mutation, and a separate render-thread-enabled UI smoke after the functional suite passes with `--render-thread=0`.

## 8. Aggregate runner and rendering automation

- [x] 8.1 Add `tests/run.py` and a JSON manifest supporting headless/ui/render lanes, tags, filters, explicit binary paths, timeouts, artifact directories, machine-readable summaries, and clear missing-precondition failures.
- [x] 8.2 Consolidate the duplicated visual Lua drivers into a deterministic render-scenario helper with fixed camera/resolution, simulation or fixed-delta seam, wave time, animation sample time, particle state, UI values, and clean post-capture window shutdown.
- [x] 8.3 Register feature HDR-off, feature HDR-on with UI, and island scenarios, with supported-backend variants and short temporal checks for skeletal animation and particles where semantic checks alone are insufficient.
- [x] 8.4 Add backend-specific baseline storage, tolerance/mask configuration, explicit baseline-update commands, missing-baseline failures, and actual/expected/diff artifacts using the existing image-probe facilities where practical.
- [x] 8.5 Review and approve initial baselines separately for each available backend, measure repeat-run noise, and verify HDR-on/HDR-off captures differ for the declared rendering reason rather than scene content changes.
- [x] 8.6 Migrate cloud, PreZ, and Metal/GL wrapper filtering/capture/reporting to the shared runner where equivalent; retain unique checks as named scenarios or documented standalone harnesses and keep performance tests out of functional gates.
- [x] 8.7 Move user-facing authoring scripts beside their examples, keep deterministic render drivers under tests, reclassify windowed pipeline checks, and make game UI automation use temporary settings instead of rewriting project configuration.
- [x] 8.8 Update test READMEs with lane commands, display/GPU requirements, fixture generation, artifacts, baseline review, exit codes, and the retained-harness inventory.

## 9. Examples and test cleanup

- [x] 9.1 Apply the approved example reorganization from the coverage inventory, consolidating redundant scene builders and launchers around the two canonical showcases and retained feature-specific samples.
- [x] 9.2 Remove or replace redundant test scripts and shell wrappers only after their assertions are represented in the manifest or a documented retained harness; update every reference in the same step.
- [x] 9.3 Deduplicate island vegetation textures or other serialized assets only after retargeting paths and proving headless reloads; do not delete canonical Tree1 resources or grouped source models.
- [x] 9.4 Remove tracked obsolete generated sample outputs and backup files identified by the inventory, excluding user-local ignored outputs and unrelated workspace files.
- [x] 9.5 Update examples documentation with FeatureGame, IslandGame, canonical project/build commands, source-asset locations, shared UI resources, and retained feature-specific samples.
- [x] 9.6 Search for stale scene, script, wrapper, UI-resource, and test-fixture paths and run the aggregate headless suite to prove the reorganization left no dangling references.

## 10. Final verification

- [x] 10.1 Build normal and ASAN configurations plus `furye-ui-tests` without clobbering shared example binaries, and record exact build commands and results.
- [x] 10.2 Run all headless tests, both-format semantic round trips for legacy and canonical scenes, malformed-input cases, cook/package/pak tests, and the ASCII checker.
- [x] 10.3 Run the complete editor UI suite with JUnit output on an available display, plus the render-thread-enabled smoke; investigate every failure rather than approving it from screenshots alone.
- [x] 10.4 Run the core rendering matrix on every available backend with repeat-run noise checks and reviewed baseline artifacts; explicitly report unavailable GPU/display lanes.
- [x] 10.5 Run retained particle, ocean, terrain, vegetation, cloud, game UI, play-mode, clone/duplicate, and render-settings regressions affected by serialization or scene reorganization.
- [x] 10.6 Run the ASAN-first headless and relevant windowed checks, confirm clean shutdown and no new leaks/errors, and remove only scratch artifacts created by this change.
- [x] 10.7 Review the final diff against the proposal's non-goals, verify no unrelated files such as the pre-existing `examples/.pinned-version` were modified, and mark tasks complete only with their recorded verification.
