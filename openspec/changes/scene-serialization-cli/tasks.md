## 1. Fix `Scene::Load` textures-array bug (root cause)

- [x] 1.1 In `engine/Fury/Scene.cpp` `Scene::Load`, locate the textures block (currently `if (auto texWrapper = FindMember(wrapper, "textures")) { LoadArray(texWrapper, "textures", [&](const void* node) -> bool { ... }); }`). Change the `LoadArray` call from the 3-arg overload (`LoadArray(texWrapper, "textures", walker)`) to the 2-arg array-walking overload (`LoadArray(texWrapper, walker)`). Remove the `"textures"` second argument. Leave the `FindMember` guard and the lambda body unchanged. This is the one-line fix for both the rapidjson `IsObject()` assert (debug) and the silent texture skip (release).
- [x] 1.2 Confirm the materials and meshes blocks below it are unchanged (they correctly use the 3-arg overload from the top-level `wrapper`, which is the scene object — do not touch them).
- [x] 1.3 Build the engine in a debug configuration (`cmake --build build` with the default Debug config) and confirm `Scene::Load` compiles cleanly with no new warnings.

## 2. Add `FileUtil::SaveByExtension` (C++ + Lua binding)

- [x] 2.1 In `engine/Fury/FileUtil.h`, add the declaration `static bool SaveByExtension(const std::shared_ptr<Serializable> &source, const std::string &filePath, int maxDecimalPlaces = 5);` next to the existing `SaveFile` / `SaveCompressedFile` declarations.
- [x] 2.2 In `engine/Fury/FileUtil.cpp`, implement `SaveByExtension`: lowercase the extension of `filePath` (reuse the `ToLowerExt` pattern from `Cli.cpp` or inline it), dispatch to `SaveFile` for `.json`, `SaveCompressedFile` for `.bin`, and for any other extension log a `FURYE` naming the unsupported extension and return false. Do NOT re-implement `ExtractMemoryBackedTextures` here — it runs inside `SaveFile`/`SaveCompressedFile`.
- [x] 2.3 In `engine/Fury/LuaBindings.cpp`, expose `SaveByExtension` on the `FileUtil` Lua table as `FileUtil.SaveByExtension(scene, path)`. Mirror the binding shape of the existing `SaveFile` / `SaveCompressedFile` Lua bindings.
- [x] 2.4 Refactor `engine/Fury/Cli.cpp` `WriteSceneByExt` to call `FileUtil::SaveByExtension(scene, out_path)` instead of its inline `.json`/`.bin` branch. Keep the `wrote <path>` stdout print and the return-code logic. Remove the now-dead `ToLowerExt` call from `WriteSceneByExt` (the helper does it internally now). Leave the standalone `ToLowerExt` helper in place — `DoConvert` still uses it for input-extension validation.
- [x] 2.5 Refactor `examples/Editor.lua` `write_scene_to_path` to call `FileUtil.SaveByExtension(Scene.GetActive(), full)` instead of the inline `ext == ".json"` / `ext == ".bin"` branch. Keep the `Editor.SetCurrentScene(full, true)` and `set_status` calls on success, and the `set_status` + `return false` on failure. Remove the now-unused `ext` local.

## 3. Add `fury convert scene` kind

- [x] 3.1 In `engine/Fury/Cli.cpp` `DoConvert`, extend the `kind` validation to accept `"scene"` in addition to `"gltf"` and `"fbx"` (update the error message's supported-kinds list to `gltf, fbx, scene`).
- [x] 3.2 Add a `kind == "scene"` branch: validate `in_ext` is `.json` or `.bin` (else exit 1 with a stderr message listing supported input extensions); validate `out_ext` is `.json` or `.bin` (else exit 1 listing supported output extensions). Create a `Scene` with `working_dir = DirOf(input).empty() ? std::string{} : DirOf(input) + "/"` and an `OcTree`. Set `Scene::Active = scene`. Call `FileUtil::LoadFile` (`.json`) or `FileUtil::LoadCompressedFile` (`.bin`) on the input. On load failure, reset `Scene::Active = nullptr`, print a stderr message, return 1. On success, call `WriteSceneByExt(scene, output)` (which now delegates to `SaveByExtension`), reset `Scene::Active = nullptr`, return the write's exit code.
- [x] 3.3 Update `kConvertHelp` in `Cli.cpp` to add the `fury convert scene <input.json|.bin> <output.json|.bin>` usage line, a one-paragraph `scene` kind description (engine scene → engine scene, format by extension, no lossy mapping), and update the `KINDS` section to list `scene` alongside `gltf` and `fbx`.
- [x] 3.4 Update `kTopHelp` in `Cli.cpp` to mention `scene` is supported by `convert` (the existing one-line `convert` description can stay; the detail lives in `kConvertHelp`).

## 4. Ditch `fury verify` (kept CLI surface minimal)

The `verify` subcommand was the closed-loop round-trip test tool, but
`convert scene` + `info` cover the same workflow from the shell:

```
fury convert scene scene.bin /tmp/rt.json
fury convert scene /tmp/rt.json /tmp/rt.bin
fury info /tmp/rt.bin
fury info scene.bin
# diff the two `info` outputs by eye or with `diff`
```

The structured `MISMATCH: <field> <a> vs <b>` line was the only piece
`verify` added on top. Removing it keeps the CLI surface clean; a
custom Lua script can produce the diff if a project needs it.

- [x] 4.1 Remove `kVerifyHelp`, `DoVerify`, `PrintCounts`, `CompareCounts` from `engine/Fury/Cli.cpp`. Keep `SceneCounts` + `CountScene` (used by `InfoEngineScene`).
- [x] 4.2 Drop `verify` from `kTopHelp`, `DoHelp` topic list, `LooksLikeSubcommand` token list, and `Cli::Run` dispatch.

## 5. Update docs

- [x] 5.1 In `docs/CLI.md`, add a `convert scene` subsection under the `convert` reference: full syntax, supported input/output extensions (`.json`, `.bin`), behavior (load via `FileUtil::LoadFile`/`LoadCompressedFile`, save via `SaveByExtension`, working_dir = dirname(input)), no-engine-boot note, and an example `fury convert scene Resource/Scene/scene.bin /tmp/scene.json`.
- [x] 5.2 Skip (verify section was removed in step 4).
- [x] 5.3 In `docs/CLI.md`, update the top-level subcommand list to match the trimmed CLI (no `verify` entry).
- [x] 5.4 Verify the help strings in `Cli.cpp` (`kTopHelp`, `kConvertHelp`) and `docs/CLI.md` agree on supported extensions and exit codes.

## 6. Build and smoke-test the fixes

- [x] 6.1 Build the engine and the `fury` binary in a debug configuration: `cmake --build build`. Confirm a clean build with no new warnings.
- [x] 6.2 Confirm the original crash on the un-fixed code path is no longer reachable: `convert scene scene.bin /tmp/rt.bin` followed by `info /tmp/rt.bin` exits 0 (the re-saved file loads cleanly).
- [x] 6.3 Cross-format convert: `fury convert scene scene.bin /tmp/scene.json`. Confirm `wrote /tmp/scene.json` and the file is valid JSON.
- [x] 6.4 Convert back: `fury convert scene /tmp/scene.json /tmp/scene.bin`. Confirm the file begins with the LZ4 envelope.
- [x] 6.5 Confirm the bundled `scene.bin` is untouched by the smoke tests: `git status examples/Resource/Scene/scene.bin` reports no changes.

## 7. Regression-test the originally-reported bugs

- [x] 7.1 **Bug: scene.bin round-trip crash.** Run `convert scene scene.bin /tmp/roundtrip.bin` then `info /tmp/roundtrip.bin`. Confirm exit 0 and no `Assertion failed: (IsObject())` abort. Before the fix, this crashed; after the fix, it completes.
- [x] 7.2 **Bug: fbx → json lost textures.** Run `fury convert fbx examples/Resource/Scene/tank.fbx /tmp/tank.json` then `fury info /tmp/tank.json` and confirm the `textures` count is non-zero (3 in the bundled tank). Before the fix (release), the reloaded texture count was lower because the top-level textures array was silently skipped.
- [x] 7.3 **Editor smoke (manual, optional if a display is available):** Launch `./furye Editor.lua`, press `Cmd+S` (or `Cmd+Shift+S` and pick a path under `/tmp/`), quit, and relaunch the editor on the saved file. Confirm the editor starts without the rapidjson assertion and the scene renders with textures. The `protect-bundled-scene-from-save-overwrite` change should still redirect the first `Cmd+S` away from the bundled `scene.bin`.
- [x] 7.4 Run `git diff --stat engine/ examples/ docs/` and confirm the only edits are: `engine/Fury/Scene.cpp` (one-line Load fix), `engine/Fury/FileUtil.{h,cpp}` (`SaveByExtension`), `engine/Fury/Cli.cpp` (`convert scene` + help strings + `WriteSceneByExt` refactor), `engine/Fury/LuaBindings.cpp` (`SaveByExtension` binding), `examples/Editor.lua` (`write_scene_to_path` refactor), and `docs/CLI.md` (new subsection). No other files should be touched.

## 8. No-C++-changes-to-other-subsystems invariant

- [x] 8.1 `git diff --stat engine/Fury/Scene.cpp` — only the textures-array `LoadArray` line changed.
- [x] 8.2 `git diff --stat engine/Fury/Mesh.cpp engine/Fury/Material.cpp engine/Fury/Texture.cpp engine/Fury/GltfImporter.cpp engine/Fury/FbxConverter.cpp` — empty (no edits to those files in this change).
- [x] 8.3 `git diff --stat examples/Editor.lua` — only `write_scene_to_path` changed (the `SaveByExtension` call replacement); no other Lua edits.
