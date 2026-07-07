## Why

Saving any engine scene to `.json`/`.bin` and reloading it is broken today. `Scene::Load` (engine/Fury/Scene.cpp:66) reads the top-level `"textures"` array with the wrong `LoadArray` overload — it calls `FindMember("textures")` on the array value itself, which asserts `IsObject()` (rapidjson `document.h:1154`) in debug builds and silently skips the array in release (`NDEBUG` makes `RAPIDJSON_ASSERT` a no-op). Because `Scene::Save` always emits a top-level `"textures"` array, **every** saved file hits this on reload: the bundled `Resource/Scene/scene.bin` (old format, no top-level array) loads fine, but saving it once writes the new format and the next launch aborts with `Assertion failed: (IsObject()), function FindMember`. The same skip in release explains "fbx → json lost textures": textures only present in the deduped top-level array are dropped on reload. There is also no offline way to convert `scene.bin` ↔ `scene.json` or to verify a round-trip — the existing `fury convert` only handles `gltf`/`fbx` → scene, and there is no `verify` subcommand. The user has to boot the full editor just to test a save, which makes closed-loop debugging of serialization regressions impractical.

## What Changes

- **Fix `Scene::Load` textures-array read (root cause).** Replace the buggy `LoadArray(texWrapper, "textures", walker)` call (3-arg overload, which re-FindMembers on the array) with the correct 2-arg `LoadArray(texWrapper, walker)` that walks the already-resolved array directly. One-line fix in `engine/Fury/Scene.cpp`. This is the fix for both the reported crash and the reported texture loss.
- **New `fury convert scene <input> <output>` kind.** Loads an engine scene (`.json`/`.bin`) and saves it back out, dispatching the output format by extension. Enables `scene.bin` → `scene.json`, `scene.json` → `scene.bin`, and format-preserving re-saves. Reuses the existing `FileUtil::LoadFile` / `LoadCompressedFile` + `SaveFile` / `SaveCompressedFile` paths.
- **New `fury verify <path> [--round-trip <out>]` subcommand.** Loads a scene and checks it deserialized cleanly (no assertion, node/mesh/material/texture/joint counts non-zero where expected). With `--round-trip`, additionally saves to a temp file (format chosen by the `--round-trip` argument's extension, default `.bin`), reloads the temp file, and compares structural counts (nodes, meshes, submeshes, materials, textures, animations, joints) between the two loads. Prints a diff and exits non-zero on mismatch. This is the closed-loop tool the user asked for, and it doubles as the foundation for future serialization automation tests.
- **Canonical save-by-extension dispatch.** Add `FileUtil::SaveByExtension(scene, path)` (C++) that picks `SaveFile` vs `SaveCompressedFile` by `.json`/`.bin` extension, and expose it as a Lua binding `FileUtil.SaveByExtension`. Refactor `Cli::WriteSceneByExt` and `Editor.lua`'s `write_scene_to_path` to call it, so the dispatch lives in one place instead of being duplicated ad-hoc in C++ and Lua. The user explicitly asked: "when saving, can you recognize whether to save to json or bin by extension? If can't add the ability."
- **Docs.** Update `docs/CLI.md` with the `convert scene` kind and the `verify` subcommand; update the `convert` and top-level help strings in `Cli.cpp` to match.

## Capabilities

### New Capabilities

- `scene-round-trip`: Engine-side contract that a `Scene` saved by `Scene::Save` can be reloaded by `Scene::Load` without assertion failures, silent data loss, or texture-reference breakage. Covers the textures-array `Load` fix, the save-by-extension dispatch helper, and the round-trip-stability invariant (counts match, textures resolve, no assert). The `fury verify` CLI is the executable check of this invariant.

### Modified Capabilities

- `cli`: Add the `convert scene <input> <output>` kind (engine scene → engine scene, any `.json`/`.bin` combination) and the `verify <path> [--round-trip <out>]` subcommand. Both reuse the no-engine-boot CLI dispatch path. The `convert` help text and `docs/CLI.md` are updated to cover the new kind and subcommand.

## Impact

- **Code (C++):**
  - `engine/Fury/Scene.cpp`: one-line fix in `Scene::Load` (textures-array overload correction).
  - `engine/Fury/FileUtil.{h,cpp}`: add `SaveByExtension(scene, path)`; refactor `SaveFile`/`SaveCompressedFile` callers where ad-hoc.
  - `engine/Fury/Cli.cpp`: add `convert scene` kind to `DoConvert`; add `DoVerify` handler and wire it in `Cli::Run`; update `kTopHelp` / `kConvertHelp` / add `kVerifyHelp`; refactor `WriteSceneByExt` to call `FileUtil::SaveByExtension`.
  - `engine/Fury/LuaBindings.cpp`: expose `FileUtil.SaveByExtension` Lua binding.
- **Code (Lua):**
  - `examples/Editor.lua`: `write_scene_to_path` calls `FileUtil.SaveByExtension` instead of the inline `.json`/`.bin` branch.
- **Specs:** `scene-round-trip` (new), `cli` (modified).
- **Docs:** `docs/CLI.md` updated for `convert scene` and `verify`.
- **Behavior visible to users:** `fury convert scene scene.bin scene.json` and `fury verify scene.bin --round-trip /tmp/rt.json` work offline (no window, no Lua VM). The bundled `scene.bin` survives a save-then-reload cycle without the rapidjson assertion. The `protect-bundled-scene-from-save-overwrite` change remains valuable as defense-in-depth (prevents accidental overwrite of the bundled file) but is no longer the only thing keeping the editor launchable.
- **No breaking API changes.** `SaveFile`/`SaveCompressedFile` keep their signatures; `SaveByExtension` is additive.
- **Rollout:** The `Scene::Load` fix unblocks the existing saved-scene reload path immediately. The CLI additions are purely additive. No on-disk migration; users with a previously-corrupted `scene.bin` still recover via `git checkout HEAD -- examples/Resource/Scene/scene.bin`.
