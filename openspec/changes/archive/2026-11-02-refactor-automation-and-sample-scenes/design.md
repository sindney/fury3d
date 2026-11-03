## Context

Scene persistence currently implements `Serializable::Load(const void*, bool)` and `Save(void*, bool)` over RapidJSON values and writers. `Scene`, `SceneNode`, components, resources, pipelines, and postprocess types each duplicate their field schema, while `FileUtil` separately handles JSON, LZ4-compressed JSON, in-memory strings, texture extraction, active-scene scoping, and render-thread flushing. Cooking is a separate resource-discovery traversal in `CookPipeline`; pak, DDC, and manifest formats are independent systems.

The test suite has 29 top-level Lua guards, 10 scene/render scripts, and several shell wrappers, but no aggregate runner. `fury exec` is GPU-free; `fury`, `furye`, and some `furye-cli` commands require a graphics context. The editor uses Dear ImGui 1.92.8 docking, submits a deep-cloned draw snapshot to the render thread, and currently has no test-engine hooks. Existing `FURY_*` setup hooks and native file dialogs are not a portable UI automation layer.

The examples need two stable, feature-rich fixtures. Source models are now grouped under `examples/Projects/samples`; the new projects will reference those sources rather than introduce another set of model copies.

## Goals / Non-Goals

**Goals:**

- One persistent-field definition per type, shared by load, save, and cook-time reference discovery.
- Backward-compatible `.json` and `.bin` scenes with explicit defaults, sticky errors, and semantic headless round-trip checks.
- A maintainable three-lane automation suite: headless, editor UI, and rendering.
- Unattended editor interaction tests with stable identifiers, isolated state, timeouts, and JUnit output.
- Two canonical showcase scenes covering the requested rendering, animation, particles, terrain, vegetation, ocean, HDR, and game UI features.
- Reproducible captures and backend-aware image regression artifacts.
- Examples and test cleanup with an explicit coverage and reference inventory.

**Non-Goals:**

- Replacing glTF/FBX importers, image codecs, pak compression/indexing, DDC entries, shader caches, or cook-manifest serialization.
- Pixel-identical output across GPUs or rendering backends.
- Automating native operating-system dialogs with synthetic OS input.
- Exact FPS, timing, or performance assertions in functional UI tests.
- Removing unique feature coverage merely because a harness is old or large.
- Deleting the user's grouped source assets or treating generated binaries as maintained examples.

## Decisions

### Decision 1: Use named-field JSON archives with three explicit modes

Introduce `Archive` with `Loading`, `Saving`, and `Cooking` modes, `IsLoading()` / `IsSaving()` / `IsCooking()`, scene/entity-manager context, format version, sticky status, and a nested field path for diagnostics. JSON reader and writer backends wrap the existing RapidJSON types. Named wrappers such as `ar << Field("position", m_Position)` preserve field names; object and array scopes replace the current `bool object` flattening convention.

Required fields, optional fields with defaults, and conditionally present fields are distinct operations. A present malformed field is an error; an absent optional field applies its legacy default. Errors propagate to `FileUtil` and the caller. Version validation happens before destructive scene replacement, and failed loads never become active.

Cooking is a separate mode because the current cook operation collects references from live scenes rather than reading or writing a runtime scene document. `CookArchive` collects typed references without decoding textures, creating GPU resources, rebuilding terrain, subscribing components, or registering physics.

**Alternatives considered:** A binary-only stream was rejected because unnamed `<<` operations cannot preserve the existing named JSON schema. Making cooking orthogonal to loading/saving was rejected because it would give reference collection a misleading transfer direction and invite runtime side effects.

### Decision 2: Migrate incrementally behind a temporary adapter, then remove Load/Save

Add `Serializable::Serialize(Archive&)` alongside the old interface. A temporary default adapter forwards unmigrated types through the JSON backends. Migrate in this order: value types and `Entity`; simple components; resources; `SceneNode` and `Scene`; pipeline/postprocess types. Keep `ComponentRegistry` as the factory and centralize component type emission/validation, including the currently non-persisting `Transform` registration case.

`FileUtil` remains the transport facade for plain JSON, LZ4 JSON, extension dispatch, and strings. Save-time memory-backed texture extraction remains a file-save preparation step with an output directory; it is not part of `Serialize`, in-memory cloning, or cooking. Existing `Scene::Active` and `Pipeline::Active` scopes remain until all serializers use explicit archive context safely. Post-load joint relinking, texture registration, node registration, and derived-state rebuilds remain ordered loading branches.

Remove the adapter and old per-type `Load`/`Save` implementations only after every `Serializable` implementation and caller, including in-memory clone/duplicate and play-mode save, uses archives.

**Alternatives considered:** A big-bang rewrite was rejected because it would make format regressions difficult to bisect. Permanent dual implementations were rejected because they preserve the maintenance problem this change is intended to solve.

### Decision 3: Verify runtime state with an independent semantic fingerprint

Add a native persistent-state visitor and a small read-only Lua binding for the consolidated `tests/lua/scene_archive_roundtrip.lua` harness. The visitor reads runtime objects, not the archive's output document. It canonicalizes unordered resource/map traversal by stable path or name while preserving semantic child, LOD, joint, and postprocess order. Counts, identifiers, paths, enums, and references compare exactly; floats use documented tolerances; transient GPU, playback, and editor-only state are excluded.

The harness saves and reloads into fresh scenes in isolated temporary directories for both `.json` and `.bin`, then reports a structured field-path diff. It covers the legacy scene, every registered component type, both showcases, malformed/wrong-arity inputs, missing optional blocks, future versions, duplicate paths, and physics-to-no-physics reloads. Cook/package verification compares referenced texture/sidecar sets before and after migration.

**Alternatives considered:** Byte-comparing saved scenes was rejected because unordered containers make output order non-semantic and compression output is not a stable interface. Extending the existing save-only Lua smoke test without a native visitor was rejected because the Lua API does not expose all persistent component state.

### Decision 4: Host ImGui tests in a separate `furye-ui-tests` executable

Vendor a pinned `imgui_test_engine` revision compatible with the vendored Dear ImGui 1.92.8 docking sources. Build a separate editor test executable with `WITH_EDITOR=1`, a test-host define, and `IMGUI_ENABLE_TEST_ENGINE` applied consistently to ImGui and the test target. Production `fury`, `furye`, and `furye-cli` do not compile the test engine or its hooks.

Create the test engine after `Gui::Initialize`, register tests before start, and run its coroutine on the game thread after editor/Lua UI submission and before draw snapshot cloning. Install pre-swap/post-swap hooks around presentation and stop/destroy the engine before `Gui::Shutdown`. Initial functional UI tests run with `--render-thread=0`; a separate render-thread smoke guards integration without making every UI test depend on it.

**Alternatives considered:** Using `furye-cli` was rejected because it has no editor frame loop. Adding the engine to normal `furye` was rejected to keep production hooks and dependency costs out of the editor. Pumping tests on the render thread was rejected because editor state and ImGui context ownership are on the game thread.

### Decision 5: Use test-only UI services and semantic assertions

Give automated windows, hierarchy rows, inspector properties, tabs, and preview controls stable identifiers. Replace pointer-derived automation references with stable node/component/property scopes where needed. Tests assert `Editor` selection, component values, dirty state, preview metadata, and populated profiler data rather than treating window visibility as sufficient.

A test-only dialog provider returns known open/save paths, allowing File menu and confirmation routing to be exercised without native OS automation. Actual file correctness is still checked by the headless round-trip lane. Mesh preview coverage drives Content Browser selection and LOD/preview controls; profiler coverage visits Perf/GBuffer/Shadows and loads the existing minimal GPU graph fixture.

Each run uses a fixed 1280x720 window, scale 1, deterministic fixture, temporary output directory, disabled settings writes, and overridden ImGui ini paths for both ImGui and Fury's early manual readers. Failure captures, logs, and JUnit XML are artifacts. Inspector mutations exposed by these tests use one dirty-marking path instead of adding test-specific state updates.

**Alternatives considered:** Synthetic OS clicks were rejected because they are blocked or unreliable on supported hosts. Golden screenshots for all editor behavior were rejected in favor of model assertions, with images retained for failures and genuinely visual preview checks.

### Decision 6: Organize automation as three lanes under one runner

Add a JSON manifest and a small `tests/run.py` entry point with `headless`, `ui`, and `render` lanes, tags, filters, timeouts, binary-path options, artifact directories, and a machine-readable summary. The runner reuses built binaries and fails clearly on missing preconditions; it does not overwrite `examples/fury*` outputs or implicitly select a different build directory.

Inventory current Lua and shell checks before consolidation. Move user-facing scene authoring scripts with their examples, keep deterministic render drivers under tests, and correct misclassified windowed guards. Consolidate duplicated filtering/capture/reporting from cloud, PreZ, and Metal/GL wrappers. Preserve unique checks as named scenarios or documented retained harnesses.

**Alternatives considered:** Introducing a new native unit-test framework and CTest hierarchy was deferred; the immediate need is one reproducible integration runner, not another test technology. Deleting all legacy wrappers at once was rejected until their coverage inventory is complete.

### Decision 7: Build two canonical projects from shared source assets

Create `examples/Projects/feature/` and rebuild `examples/Projects/ocean/ocean_island.bin`, with `examples/FeatureGame.lua` and the existing `examples/IslandGame.lua` as entry points. Tank and james sources resolve from `examples/Projects/samples`; restore the small Fox glTF payload from git history into `examples/Projects/samples/fox/`. Generated engine scenes and project-local imported data live in the consuming project.

The feature showcase combines tanks, point/spot/directional-CSM lighting, fox and james animation, and the outdoor fire/smoke setup. A headless, idempotent Lua builder imports the models, merges the proven particle definitions, and saves `feature.bin`; the older JSON-oriented particle injector is not the canonical workflow. Reusable particle textures are copied or retargeted into retained project assets so the showcase does not depend on a scene later removed by cleanup. A small RmlUi overlay reuses a responsive shared theme under `examples/Resource/Ui/` without instantiating the full IslandGame menu flow.

The island project contains terrain, walkable beach collision/spawn, kraut vegetation, infinite ocean, lighting/atmosphere, and render settings. Rebuild it through the existing base/island chain plus a restored and adapted seeded PalmTree2 HISM builder, preserving the `DefaultSun`, `Sky`, `Ocean`, `Terrain`, `MainCamera`, and `PlayerSpawn` contracts used by IslandGame. A fresh rebuild drops orphaned Tree1 content without deleting canonical Tree1 sample assets. IslandGame retains its menu, options, pause, HUD, and FPS behavior. All assets required by either canonical scene are checked in or reproducibly generated from checked-in sources; test runs never download them.

**Alternatives considered:** Keeping separate tank, particle, skeletal, ocean, and UI fixtures as the primary regression matrix was rejected because composition bugs are exactly what the old scattered fixtures miss. Duplicating source models into each project was rejected to avoid asset drift.

### Decision 8: Use deterministic render scenarios and reviewed backend baselines

Scenario setup fixes resolution, camera, render settings, wave time, animation sample time, particle warmup/state, and UI values before capture. Arbitrary wall-clock frame counts are not sufficient for animated scenes. The core matrix contains feature-showcase HDR-off, feature-showcase HDR-on with UI, and island; supported backend lanes run the same scenarios without sharing baselines.

Store approved baselines under `tests/baselines/<backend>/` with per-scenario tolerances and masks. Reuse existing screenshot and image-probe facilities where possible. Missing baselines and threshold failures retain actual, expected, and diff/metric artifacts. Baseline updates require an explicit command and review; PNG byte equality and cross-backend comparisons are never gates.

**Alternatives considered:** Screenshot-only testing was rejected because a rendered image can omit required content without a useful diagnosis. Semantic-only testing was rejected because it does not catch composition, HDR, or UI rendering regressions.

## Risks / Trade-offs

- [A symmetric serializer accidentally changes defaults or side effects] -> Migrate behind the adapter, record legacy defaults, run semantic and malformed-input tests per group, and stage scene copies before save.
- [The archive becomes a second generic object system] -> Keep it limited to named fields, scopes, references, mode, context, and status; leave pak/DDC/manifest formats alone.
- [CookArchive skips resources discovered by the manual terrain/sky walk] -> Integrate cooking last and compare complete reference sets and pak pipeline results against the pre-migration inventory.
- [ImGui and test-engine revisions drift] -> Pin both revisions, enable the test macro consistently for the test target, and make the compatibility build a prerequisite before writing tests.
- [UI tests become order-dependent through persisted editor state] -> Override all ini readers/writers, use fresh fixtures and output directories, and enforce per-test timeouts.
- [Rendering goldens are noisy across drivers] -> Separate baselines by backend, fix simulation state, use tolerances/masks, and require explicit baseline review.
- [Scene or test deletion strands a reference or loses unique coverage] -> Inventory repository references and assertions first, migrate coverage, then delete and run the aggregate runner.
- [The combined change is difficult to review] -> Land in staged commits: baselines/fixtures, archive foundations, serializer migrations, UI host/tests, render runner, then cleanup.

## Migration Plan

1. Inventory existing tests, example references, persistent defaults, scene counts/fingerprints, visual captures, and cook reference sets. Record the baseline before changing code.
2. Author the two canonical projects and headless content checks using the current format. Confirm all source assets are available and reproducible.
3. Add archive foundations and the adapter. Migrate serializers in dependency order, running headless round trips after each group; integrate `CookArchive` only after load/save parity.
4. Remove old per-type Load/Save code and the adapter after all callers migrate. Run legacy, malformed-input, clone/play-mode, cook/package, and ASAN verification.
5. Add the pinned UI test engine and separate host, then implement stable IDs, isolated fixtures, editor tests, and JUnit reporting.
6. Add the aggregate runner and deterministic render scenarios. Capture and review initial per-backend baselines; migrate existing visual checks without losing unique coverage.
7. Reorganize remaining examples/tests, update all wrappers and documentation, and run the full available headless/UI/render matrix.

Rollback is staged: the adapter preserves the old path until migration is proven, canonical scenes remain valid in the old format, and baseline updates are explicit reviewable changes. If a late migration group fails, revert that group without discarding earlier verified fixtures or archive foundations.

## Open Questions

- Exact deletion candidates and baseline tolerances are inventory decisions made during apply, not assumptions in this design. Each deletion requires migrated coverage and updated references; each baseline requires review.
- The pinned test-engine revision will be selected by compiling against Dear ImGui 1.92.8 docking before UI test implementation. No unverified revision is prescribed here.
