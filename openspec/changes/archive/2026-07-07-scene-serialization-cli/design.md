## Context

The engine's `Scene::Save` / `Scene::Load` pair is supposed to be a lossless round-trip: save a `Scene` to `.json` (pretty-printed rapidjson) or `.bin` (LZ4-compressed rapidjson), then reload it into a fresh `Scene` and get the same tree, meshes, materials, textures, and joints back. Today it is not.

Two intertwined facts cause the reported bugs:

1. **`Scene::Load` reads the top-level `"textures"` array with the wrong `LoadArray` overload.** `Scene::Save` (engine/Fury/Scene.cpp:138) emits a top-level `"textures"` array (deduped by UUID, first-class textures from the `EntityManager`). `Scene::Load` (Scene.cpp:66) does `if (auto texWrapper = FindMember(wrapper, "textures")) { LoadArray(texWrapper, "textures", walker); }`. `texWrapper` is already the array value. The 3-arg `LoadArray(wrapper, name, walker)` overload (Serializable.cpp:292) calls `dom.FindMember(name.c_str())` on `texWrapper` — i.e. it tries to find a member `"textures"` *inside* the array. `rapidjson::Value::FindMember` begins with `RAPIDJSON_ASSERT(IsObject())` (document.h:1154 in the vendored copy). In a debug build (no `NDEBUG`), that `assert(IsObject())` fires and the process aborts with the user's exact message: `Assertion failed: (IsObject()), function FindMember, file document.h, line 1154`. In a release build (`-DNDEBUG`, see engine/CMakeLists.txt:37), `RAPIDJSON_ASSERT` is a no-op, `FindMember` returns `MemberEnd()` on the non-object, `LoadArray` returns false, and the top-level textures array is **silently skipped**.

2. **The bundled `Resource/Scene/scene.bin` predates the top-level `"textures"` array.** So `FindMember(wrapper, "textures")` returns nullptr on the bundled file, the buggy block is skipped, and the editor launches fine. The moment any save runs (`Scene::Save` always emits the array now), the file becomes unloadable in debug (assert) and lossy in release (top-level textures dropped).

Fact (1) explains both user-reported symptoms: the `scene.bin` round-trip crash (debug) and the `fbx → json` "lost textures" (release — textures that are only in the deduped top-level array, not re-emitted inline by every referencing material, get dropped on reload). The `protect-bundled-scene-from-save-overwrite` change is a Lua-side mitigation that prevents the bundled file from being overwritten, but it does not fix the underlying serialization bug — any user-initiated save to any path produces a file that cannot be cleanly reloaded.

A second, smaller gap: there is no offline way to exercise the round-trip. `fury convert` handles only `gltf`/`fbx` → scene; there is no `scene.bin` → `scene.json` path and no `verify` subcommand. To reproduce a serialization regression today the user must boot the full editor (SFML window, GL context, Lua VM), which is slow and offers no structured diff. The user explicitly asked for a CLI that converts scene formats and one that verifies by loading, "so you can check out what's wrong in a closed loop, and this can be part of future automation test as well."

Extension-based save dispatch currently exists ad-hoc in two places: `Cli::WriteSceneByExt` (engine/Fury/Cli.cpp:198) and `Editor.lua`'s `write_scene_to_path` (examples/Editor.lua:113). Both branch on `.json` vs `.bin` and call `SaveFile` / `SaveCompressedFile`. The user asked to "recognize whether to save to json or bin by extension" — consolidating this into one C++ helper + Lua binding is both the cleanup and the enabler for `convert scene` and `verify`.

## Goals / Non-Goals

**Goals:**
- Make `Scene::Save` → `Scene::Load` crash-free and lossless for any `.json`/`.bin` round-trip, on both debug and release builds.
- Add `fury convert scene <input> <output>` so `.json` ↔ `.bin` conversion (and format-preserving re-saves) work offline, no window/Lua.
- Add `fury verify <path> [--round-trip <out>]` that loads a scene, optionally round-trips it through a temp save+reload, and prints a structured diff of structural counts. Exits non-zero on mismatch. Usable as an automation-test harness.
- Consolidate save-by-extension dispatch into `FileUtil::SaveByExtension(scene, path)` (C++) + `FileUtil.SaveByExtension` (Lua binding); retire the ad-hoc branches in `Cli.cpp` and `Editor.lua`.
- Keep the existing `convert gltf` / `convert fbx` / `info` / `help` / `version` subcommands and their exit codes unchanged.

**Non-Goals:**
- Changing the on-disk scene format. The top-level `"textures"` array stays (it is the dedup-by-UUID canonical record; materials' inline textures are a secondary reference). The fix is in the reader, not the writer.
- Adding new texture states or changing `embedded-textures` extraction semantics. `ExtractMemoryBackedTextures` already works correctly; the bug is purely in `Scene::Load`.
- A PBR / HDR material upgrade. The lossy PBR → Lambert mapping from `convert gltf`/`fbx` is unchanged.
- A new automation-test framework. `fury verify` is a CLI tool that *can* be wired into CI later, but this change does not add a test runner, CI config, or fixture corpus beyond the smoke tests in `tasks.md`.
- Fixing the working-dir resolution for scenes opened via `File → Open…` in the editor. `Importer.LoadScene` already sets `working_dir = dirname(path)` (LuaBindings.cpp:563), which is correct for sibling-extracted textures. Any remaining texture-loss case found by `verify` is a follow-up.
- Changing the `protect-bundled-scene-from-save-overwrite` Lua-side protection. It remains as defense-in-depth.

## Decisions

### Decision 1: Fix `Scene::Load` by using the 2-arg `LoadArray` overload on the already-resolved array

**Choice:** In `engine/Fury/Scene.cpp` `Scene::Load`, change the textures block from:

```cpp
if (auto texWrapper = FindMember(wrapper, "textures")) {
    LoadArray(texWrapper, "textures", [&](const void* node) -> bool { ... });
}
```

to:

```cpp
if (auto texWrapper = FindMember(wrapper, "textures")) {
    LoadArray(texWrapper, [&](const void* node) -> bool { ... });
}
```

The 2-arg `LoadArray(wrapper, walker)` (Serializable.cpp:277) walks `wrapper` as an array directly — no `FindMember`, no `IsObject()` assert. This matches what the block already does for materials and meshes conceptually (find the member, then walk it), but uses the correct overload because the member was already found.

**Alternatives considered:**
- Drop the `FindMember` guard and use the 3-arg overload from the top-level wrapper: `LoadArray(wrapper, "textures", walker)`. Rejected — the 3-arg overload returns false (treated as "error") when the member is missing, and the existing code intentionally treats a missing top-level textures array as "old format, skip" (not an error). Keeping the `FindMember` guard + 2-arg walk preserves that lenient behavior for old scenes.
- Stop emitting the top-level `"textures"` array in `Scene::Save` and rely solely on materials' inline textures. Rejected — the top-level array is the dedup-by-UUID canonical record (added deliberately so `EntityManager::ForEach<Texture>` round-trips without depending on materials). Removing it would regress the dedup guarantee and break `fury info`'s texture count for scenes with unreferenced textures.
- Add an `IsArray()` guard before the `LoadArray` call. Rejected — defensive but redundant once the correct overload is used; the 2-arg `LoadArray` already returns false for a non-array, which the `if` block handles fine (no textures to load).

**Why:** Minimal, correct, preserves old-format leniency. The 2-arg overload is exactly the "walk this array value" primitive the block wants.

### Decision 2: `convert scene` reuses `FileUtil` load + `SaveByExtension` save, no GL context

**Choice:** Add a `scene` kind to `DoConvert` (engine/Fury/Cli.cpp). The handler:
1. Validates `input` extension is `.json` or `.bin`; `output` extension is `.json` or `.bin`. Else exit 1 with a clear stderr message.
2. Creates a `Scene` with `working_dir = dirname(input) + "/"` (so texture relative paths resolve against the input's directory, matching `Importer.LoadScene`'s runtime behavior).
3. Sets `Scene::Active = scene` for the duration of the load (texture/material resolution uses `Scene::Active`).
4. Calls `FileUtil::LoadFile` (`.json`) or `FileUtil::LoadCompressedFile` (`.bin`) on the input.
5. Calls `FileUtil::SaveByExtension(scene, output)` (new helper, see Decision 4).
6. Resets `Scene::Active = nullptr`, returns 0 on success.

No `Engine::Initialize`, no SFML window, no Lua VM — same no-boot contract as the existing `convert gltf`/`fbx` kinds. `BufferManager::Initialize()` and `Log<0>::Initialize` already run at the top of `Cli::Run`.

**Alternatives considered:**
- Reuse `Importer.LoadScene` (the Lua binding's C++ body) instead of `FileUtil` directly. Rejected — `Importer.LoadScene` is a sol2-wrapped lambda that lives in `LuaBindings.cpp`; pulling it out into a reusable C++ entry point would blur the "Lua surface" vs "engine core" boundary. The `FileUtil` load path is the same one `Importer.LoadScene` calls internally, so calling `FileUtil` directly is equivalent and cleaner.
- Auto-detect input format by magic bytes instead of extension. Rejected — extension is the existing convention across all CLI subcommands (`convert`, `info`); adding magic-byte sniffing here would be inconsistent.

**Why:** Smallest path to `scene.bin` ↔ `scene.json` conversion. Reuses the exact load/save code paths the editor uses, so a successful `convert scene` round-trip is direct evidence the editor will load the same file.

### Decision 3: `verify` does load → (optional save+reload) → structural-count diff

**Choice:** Add a `verify` subcommand. Syntax: `fury verify <path> [--round-trip <out>]`.

- **Without `--round-trip`:** load `<path>` into a fresh `Scene` (same setup as `convert scene`). If the load returns false OR an exception/assert fires, print the error to stderr and exit 1. If the load succeeds, print the same structural counts `fury info` prints (nodes, meshes static/skinned, submeshes, vertices, triangles, materials, textures, animations, joints) to stdout, then exit 0. This catches the "scene won't open" class of bug (the rapidjson assert, parse errors, missing required members) without needing a round-trip.
- **With `--round-trip <out>`:** after the first load, save the scene to `<out>` via `FileUtil::SaveByExtension` (format by `<out>`'s extension; if `<out>` is omitted, default to a temp `.bin` in the system tempdir), reload `<out>` into a second fresh `Scene`, then diff the structural counts between the two loads. Print both sets of counts and a `OK` / `MISMATCH` line. Exit 0 if all counts match; exit 1 if any count differs OR the second load fails. The temp file is preserved on mismatch (path named in stderr) for inspection, deleted on success.

Counted fields (compared in `--round-trip` mode): `nodes`, `meshes_total`, `meshes_static`, `meshes_skinned`, `submeshes`, `vertices`, `triangles`, `materials`, `textures`, `animations`, `joints`. AABB is reported but not compared (float rounding through LZ4+rapidjson makes a tight equality check noisy; a future change could add a tolerance-based AABB check).

The `--round-trip` path uses an exception-guarded wrapper around `Scene::Load` so that a rapidjson assert (debug) or a silent skip (release) is observable: in debug the assert aborts the process (exit code reflects the abort, typically 134 on macOS); the `verify` documentation calls out that debug builds abort on assert and release builds return the structured diff. To make `verify` useful in debug too, the tasks include a follow-up note that `Scene::Load`'s fix removes the assert entirely, so debug `verify` will complete normally after the fix.

**Alternatives considered:**
- Deep equality (serialize both scenes to JSON strings and diff). Rejected — float formatting via `PrettyWriter` with `SetMaxDecimalPlaces(5)` is stable, but pointer-identity fields (UUIDs) and entity ordering in `EntityManager::ForEach` are not guaranteed stable across loads, producing noisy false mismatches. Structural counts are the right granularity for a first-pass regression check.
- Compare against a golden reference file. Rejected — requires maintaining a fixture corpus; the user asked for a closed-loop self-check, not a golden-file regression suite. Golden files can be a follow-up built on top of `verify`.
- Make `--round-trip` always-on (no flag). Rejected — the load-only mode is useful on its own (e.g. confirming a downloaded scene opens at all), and the flag keeps the default fast.

**Why:** Structural counts are exactly what `fury info` already computes, so `verify` reuses `WalkEngineScene` + the `EntityManager::ForEach` counters. The `--round-trip` diff is the smallest reliable signal for "did save+reload lose anything." This is the closed-loop tool the user asked for.

### Decision 4: `FileUtil::SaveByExtension` as the single save dispatch point

**Choice:** Add `static bool FileUtil::SaveByExtension(const std::shared_ptr<Serializable>& source, const std::string& filePath, int maxDecimalPlaces = 5)` to `engine/Fury/FileUtil.{h,cpp}`. It lowercases the extension of `filePath`, calls `SaveFile` for `.json`, `SaveCompressedFile` for `.bin`, and returns false (with a `FURYE` naming the bad extension) for anything else. `ExtractMemoryBackedTextures` runs inside `SaveFile`/`SaveCompressedFile` as today — no change there.

Expose it to Lua as `FileUtil.SaveByExtension(scene, path)`. Refactor:
- `Cli::WriteSceneByExt` (Cli.cpp:198) → call `FileUtil::SaveByExtension` and keep only the "wrote <path>" stdout print + exit-code logic around it.
- `Editor.lua` `write_scene_to_path` (line 113) → replace the inline `.json`/`.bin` branch with `FileUtil.SaveByExtension(Scene.GetActive(), full)`.

**Alternatives considered:**
- Keep `WriteSceneByExt` as the C++ single point and only add the Lua binding. Rejected — the user explicitly asked to add the ability to save by extension; having two C++ functions (`WriteSceneByExt` in `Cli.cpp` and `SaveByExtension` in `FileUtil`) that do nearly the same thing is the kind of duplication that caused the ad-hoc state in the first place. `Cli::WriteSceneByExt` becomes a thin caller.
- Make `SaveFile` itself extension-aware (dispatch internally). Rejected — `SaveFile` and `SaveCompressedFile` are the long-established public API named by format; making `SaveFile` secretly call `SaveCompressedFile` based on extension would surprise every existing caller and break the "explicit format" contract. `SaveByExtension` is a separate, opt-in dispatcher.

**Why:** One dispatch implementation, used by CLI, editor, and verify. The user's "if can't add the ability" is satisfied by adding it; the consolidation is a natural cleanup.

### Decision 5: `verify` exit codes follow the existing CLI convention

**Choice:** `verify` reuses the existing exit-code contract (cli spec: "The CLI SHALL use stable exit codes"): 0 success, 1 user error (bad path, unsupported extension, load returned false, round-trip count mismatch) , 2 internal error (uncaught exception). A debug-build assert abort is outside the exit-code contract (the process is terminated by the runtime); the tasks verify that the Decision 1 fix removes the assert so `verify` completes normally in debug.

**Why:** No new exit-code policy. Consistent with `convert`/`info`.

## Risks / Trade-offs

- **[Risk] The `Scene::Load` fix is a one-liner but touches a code path every scene load uses.** → Mitigation: the `verify --round-trip` tool (built in the same change) is the regression test. Task 7 in `tasks.md` runs `verify --round-trip` on `scene.bin`, `scene.json`, and a `convert fbx`-produced json, on both the bundled file and a freshly-converted one. The fix is also the smallest possible change (overload correction, no new branches), so the blast radius is minimal.
- **[Risk] Structural-count diff in `verify --round-trip` may miss field-level regressions** (e.g. a texture's `srgb` flag flipping, a mesh's AABB shrinking). → Mitigation: accepted for v1. The counts catch the reported class of bug (dropped textures, crashed loads). A future change can add field-level golden comparison on top of `verify` once the count-level check is green. Noted in Open Questions.
- **[Risk] `convert scene` loads with `working_dir = dirname(input)`, which differs from the editor's `Scene::Active` working dir (CWD) when the editor later opens the converted file.** → Mitigation: this is the same working_dir `Importer.LoadScene` uses (LuaBindings.cpp:563), so `convert scene`'s output is consistent with what the editor will load. The editor's `replace_active_scene` merges into the active scene but textures already loaded their pixels during `Importer.LoadScene`'s load, so working_dir mismatch after merge does not cause re-resolution. Confirmed in `tasks.md` smoke test.
- **[Risk] `SaveByExtension` returns false for an unknown extension but existing callers (`Editor.lua`) previously emitted their own status message.** → Mitigation: `Editor.lua`'s `write_scene_to_path` keeps a `set_status` call on failure; the `SaveByExtension` false return flows through the existing `if ok then ... else set_status(...) end` branch. No behavior change for the user.
- **[Trade-off] `verify` does not run the GL pipeline (no render).** → Accepted: the goal is serialization fidelity, not render correctness. Render-time bugs (e.g. a shader uniform mismatch) are out of scope.
- **[Trade-off] The `--round-trip` temp file defaults to `.bin` when `<out>` is omitted.** → `.bin` exercises the LZ4 envelope (the format the bundled scene ships in); `.json` would exercise only the pretty-printer. Defaulting to `.bin` gives the stronger signal. The user can pass `--round-trip /tmp/rt.json` to test the `.json` path explicitly.

## Migration Plan

1. Fix `Scene::Load` (Decision 1) — one line in `engine/Fury/Scene.cpp`.
2. Add `FileUtil::SaveByExtension` + Lua binding (Decision 4); refactor `Cli::WriteSceneByExt` and `Editor.lua` to use it.
3. Add `convert scene` kind (Decision 2) and `verify` subcommand (Decision 3) to `Cli.cpp`; update help strings.
4. Update `docs/CLI.md` with the new kind and subcommand.
5. Run the smoke tests in `tasks.md` (sections 5–7): `verify` on the bundled `scene.bin` (must pass after the fix), `convert scene scene.bin /tmp/scene.json` + `verify /tmp/scene.json --round-trip`, `convert fbx tank.fbx /tmp/tank.json` + `verify /tmp/tank.json --round-trip`.

**Rollback:** Revert the `Scene::Load` one-liner (restores the bug), the `SaveByExtension` helper + callers, and the `convert scene`/`verify` handlers. No on-disk format change, no migration. Users with a previously-corrupted `scene.bin` recover via `git checkout HEAD -- examples/Resource/Scene/scene.bin` as before.

## Open Questions

- **Should `verify --round-trip` also compare a small set of field-level invariants beyond counts** (e.g. "every material's `diffuse_texture` UUID resolves to a texture in the EM")? The count-level check catches the reported bugs, but a UUID-resolution check would catch dangling references. Out of scope for v1; candidate follow-up.
- **Should `verify` grow a `--golden <ref>` mode** that compares a freshly-converted scene against a checked-in golden file for CI regression? The user mentioned "future automation test"; `--golden` is the natural next step. Out of scope for this change.
- **Should `convert scene` accept `.gltf`/`.glb`/`.fbx` inputs too** (making it a unified `convert scene <any-input> <any-output>`)? The existing `convert gltf`/`fbx` kinds already cover those inputs with lossy-mapping warnings; folding them into `convert scene` would either duplicate that logic or require routing back through the kind-specific handlers. Out of scope; `convert scene` stays `.json`/`.bin` → `.json`/`.bin` for v1.
- **Is the debug-build assert-abort acceptable for `verify`, or should `Scene::Load` catch the assert somehow?** `RAPIDJSON_ASSERT` maps to `assert` (rapidjson.h:402), which cannot be caught as a C++ exception. The Decision 1 fix removes the assert-triggering code path entirely, so `verify` completes normally in debug after the fix. No `try`/`catch` can rescue an `assert` failure; the fix is the mitigation.
