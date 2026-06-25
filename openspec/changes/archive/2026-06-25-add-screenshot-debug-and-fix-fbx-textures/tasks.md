## 1. `Texture` — add memory-backed state

- [x] 1.1 In `engine/Fury/Texture.h`, add private members `std::vector<unsigned char> m_EncodedBytes;` and `std::string m_OriginalFilename;`.
- [x] 1.2 Declare `void CreateFromMemory(const unsigned char *bytes, size_t len, bool srgb, bool mipMap);` and `void SetOriginalFilename(const std::string &filename);` in the public section. Declare `bool IsMemoryBacked() const;` (returns `m_FilePath.empty() && !m_EncodedBytes.empty()`). Declare `const std::vector<unsigned char> &GetEncodedBytes() const;` and `const std::string &GetOriginalFilename() const;` (used by `FileUtil` save-time extraction).
- [x] 1.3 In `engine/Fury/Texture.cpp`, implement `CreateFromMemory`. Mirror `CreateFromImage`'s GL upload path, but call `stbi_load_from_memory` instead of `stbi_load`. On success: store the encoded bytes into `m_EncodedBytes`, leave `m_FilePath` empty, set `m_Format`/`m_Width`/`m_Height`/`m_Mipmap` the same way `CreateFromImage` does. On `stbi_load_from_memory` failure, log via `FURYE` with `stbi_failure_reason()` and leave the texture in its previous state (unchanged from `CreateFromImage`'s failure mode).
- [x] 1.4 Implement `SetOriginalFilename`, `IsMemoryBacked`, `GetEncodedBytes`, `GetOriginalFilename` as trivial accessors.
- [x] 1.5 Verify `Texture::Save` does NOT need changes — it serializes `m_FilePath` when non-empty (still correct), serializes the procedural format/w/h shape when both `m_FilePath` and the procedural fields say so. Memory-backed textures should NEVER reach `Texture::Save` in their memory-backed state — `FileUtil::SaveFile` extracts them to file-backed first (see Section 3). Add an `assert(!IsMemoryBacked()) // FileUtil should have extracted` to `Texture::Save` to catch missed call sites.

## 2. `GltfImporter` — drop temp extraction; route embedded bytes through memory

- [x] 2.1 In `engine/Fury/GltfImporter.cpp`, delete the `ExtractEmbeddedImages` function and the `Import()` call to it.
- [x] 2.2 Modify `DeriveImageUri` (rename to `DeriveOriginalFilename` or similar) to return what becomes the texture's `m_OriginalFilename`: prefer `image.name` if non-empty, else synthesize `<output_basename>_image<i>.<ext>` from the input glTF's basename (sniffed from `mimeType` as today). The function no longer participates in URI resolution; it just produces a hint for save-time extraction.
- [x] 2.3 Modify `CreateEngineTexture` to detect the embedded vs external case at the top:
  - If `image.uri` is non-empty: existing `SetFilePathAndSRGB` + `CreateFromImage` path. No change.
  - If `image.uri` is empty AND `image.bufferView >= 0`: locate the bufferView's bytes (`buffer.data.data() + bv.byteOffset`, length `bv.byteLength`); call `texture->CreateFromMemory(bytes, len, srgb, mipmap)`; call `texture->SetOriginalFilename(DeriveOriginalFilename(...))`. Do NOT call `SetFilePathAndSRGB`.
  - If neither: log a warning, return `nullptr` (matches existing behavior for malformed inputs).
- [x] 2.4 Remove `output_basename_no_ext` from `GltfImporter::Options` (in `GltfImporter.h`). Remove the field's reference from all internal code in `GltfImporter.cpp`.
- [x] 2.5 Update the function signatures of `DeriveImageUri`/`CreateEngineTexture`/`TranslateMaterial` to drop the `output_basename_no_ext` parameter — they now derive the filename hint from the `Import()` entry point's `input_path` directly. Pass the input path's stem through `Import()` to where it's needed.
- [x] 2.6 Update `Cli.cpp` (the two `convert fbx` and `info` call sites) and `LuaBindings.cpp` (`LoadFbx`, `LoadGltf`) to stop setting `opts.output_basename_no_ext`. Also remove the `temp_directory_path() / "fury_runtime_fbx"` directory creation from `LuaBindings::LoadFbx` — the extraction is gone, so the temp dir for it is gone (the temp `.glb` itself still uses `tmpdir`, but that's just for FBX2glTF's output and is already cleaned up). Check whether `tmpdir` is still needed for the FBX2glTF subprocess output (it is — `FbxConverter::Convert` writes a `.glb` there) but no longer for image extraction.

## 3. `FileUtil` — extract memory-backed textures before serializing

- [x] 3.1 In `engine/Fury/FileUtil.cpp`, add a private helper:
  ```
  bool ExtractMemoryBackedTextures(
      const std::shared_ptr<Scene> &scene,
      const std::filesystem::path &output_dir,
      const std::string &output_stem);
  ```
  It iterates the scene's `EntityManager`, finds every `Material`, walks the material's texture map, and for each `IsMemoryBacked()` texture: chooses a target filename (prefer `tex->GetOriginalFilename()`; else `<output_stem>_<tex_name>.<ext>` where `<ext>` is sniffed from the first bytes — `\xff\xd8` JPEG, `\x89PNG` PNG, `BM` BMP); resolves filename collisions by appending `_<n>`; checks if a byte-equal file already exists at the target path (length equal + byte-for-byte compare); if not, writes the encoded bytes; sets the texture's path via `tex->SetFilePathAndSRGB(filename, tex->IsSRGB())` (relative path; load-time `Texture::CreateFromImage` will resolve via `Scene::Path`). Returns false on any write failure (with a `FURYE` message naming the path); true otherwise.
- [x] 3.2 Add a small filename-sniffer helper `DetectImageExtension(const std::vector<unsigned char> &bytes) -> std::string`. Returns `.jpg` for `FF D8`, `.png` for `89 50 4E 47`, `.bmp` for `42 4D`, `""` (or an `.unknown`) otherwise.
- [x] 3.3 Modify `FileUtil::SaveFile` to call `ExtractMemoryBackedTextures` BEFORE the existing serialization. Compute `output_dir` and `output_stem` from the input `filePath`. If extraction fails, return false without writing the scene file.
- [x] 3.4 Modify `FileUtil::SaveCompressedFile` the same way.
- [x] 3.5 Verify the extracted textures' relative paths resolve correctly when the scene is later loaded. The save uses `tex->SetFilePathAndSRGB(filename, srgb)` with the bare filename (no directory). Load-time `Texture::CreateFromImage` resolves via `Scene::Path`, which prepends the active scene's working_dir. As long as the scene is loaded with its working_dir set to the directory containing the scene file (Demo.lua does this for the active scene; CLI does it for `info`), this works. Add a test scenario in 5.x for the round-trip.
- [x] 3.6 In `LuaBindings.cpp::LoadScene` for `.json`/`.bin`, ensure the working_dir set on the import target matches `dirname(input_path)` so file-backed textures in saved scenes resolve correctly. Today it uses `FileUtil::GetAbsPath()` (the engine bin dir) — this is a known-existing behavior that ships with this code path, but if the user saves to `Resource/Scene/tank_saved.json` and later loads that same path, working_dir is already `examples/bin/` and the texture path `Resource/Scene/body.jpg` (or just `body.jpg` if the save extracted siblings) might or might not resolve. Verify and document; if it's broken, this task gains a fix sub-bullet.

## 4. Screenshot capture in `EngineOptions` + `Engine::Run`

- [x] 4.1 In `engine/Fury/Engine.h`, extend `EngineOptions` with: `std::string screenshot_path;` (default empty = capture disabled), `int screenshot_frame = 2;`, `int *exit_code_out = nullptr;` (launcher-owned `int*`; engine writes the suggested exit code).
- [x] 4.2 In `engine/Fury/Engine.cpp`, add `#define STB_IMAGE_WRITE_IMPLEMENTATION` and `#include "stb_image_write.h"` near the top. Verify (via grep across the engine sources) that no other TU defines this macro.
- [x] 4.3 Add a file-static `WriteBackBufferAsPng(const std::string &path, sf::Window &window) -> bool` helper. Use `window.getSize()` for dimensions; `glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, ...)`; vertical flip; `stbi_write_png(path.c_str(), w, h, 4, flipped.data(), w * 4)`.
- [x] 4.4 Modify `Engine::Run`'s loop body. Add an `int frame_index = 0;` outside the loop. After `window.display()`, increment `frame_index`. If `!opts.screenshot_path.empty() && frame_index == opts.screenshot_frame`, call `WriteBackBufferAsPng`. On success: log `FURYI` ("captured screenshot to <path> at frame <N>"), if `opts.exit_code_out` non-null write `0`, set `running = false`. On failure: log `FURYE`, if `opts.exit_code_out` non-null write `1`, set `running = false`.
- [x] 4.5 Confirm the capture happens AFTER the GUI overlay is rendered (which is part of `window.display()` for SFML+ImGui — the imgui-sfml `Render` call writes into the SFML window's GL context before `display()`). If GUI ends up missing from the capture, move the capture call before `display()` (so the back-buffer is read pre-swap) — but verify the back buffer is the right one to read from in either case.

## 5. Parse runtime flags in `examples/main.cpp`

- [x] 5.1 Before the `Cli::LooksLikeSubcommand` check, walk `argv[2..argc-1]` building a filtered argv list. Recognize `--screenshot <path>` and `--screenshot-frame <N>`. Validate (`N > 0`, `N <= 1000`); on validation failure print to stderr and `return 1` from `main()`.
- [x] 5.2 Build `EngineOptions` with `screenshot_path`, `screenshot_frame`, and `exit_code_out = &local_int;` set. Default `screenshot_frame = 2` when only `--screenshot` is given.
- [x] 5.3 Pass the filtered argv (NOT raw argv) when populating the Lua `arg` table. Verify `arg[1..N]` does not contain `--screenshot` or its value, even when the flags appear in unusual positions.
- [x] 5.4 Plumb the launcher's `EngineOptions` to the Lua-side `Engine.run` binding. The current binding (`engine/Fury/LuaBindings.cpp`) accepts a Lua-side options table with fields like `max_fps`, `gui_scale`, `gui_font_scale`. Either: (a) inject the launcher's options as a Lua table on `__engine_options` before the script runs, and have the binding merge launcher-set fields with the script's options table (launcher wins for screenshot fields, script wins for others); or (b) store the launcher's options in a static set by `main.cpp`, and have the binding's `Engine::Run` call layer it on top. (a) is cleaner.
- [x] 5.5 After `Engine::Run` returns, if `local_int` was set by the engine (use a sentinel like `-1` for "engine didn't touch it"), use it as the launcher's exit code; otherwise return 0. Adjust the `Engine.run` binding so it can propagate the value back to `main.cpp` (the binding currently doesn't return a value to C++; either use a launcher-static or refactor).

## 6. Manual verification

- [x] 6.1 Build: `cmake --build build-engine` from the repo root. Confirm no `STB_IMAGE_WRITE_IMPLEMENTATION` doubling errors.
- [x] 6.2 Default startup: `./build-engine/fury examples/Demo.lua --screenshot /tmp/demo_default.png` (run from a CWD where the engine resolves `Resource/Scene/scene.bin`; pick whatever the existing build's working dir convention is). Confirm: window appears briefly, exits 0, `/tmp/demo_default.png` shows the textured tank-on-grass scene from `scene.bin`.
- [x] 6.3 FBX import: `./build-engine/fury examples/Demo.lua tank.fbx --screenshot /tmp/demo_tank.png`. Confirm: PNG shows the tank scene with textures applied (not flat-shaded).
- [x] 6.4 FBX import (character): `./build-engine/fury examples/Demo.lua james.fbx --screenshot /tmp/demo_james.png`. Confirm: character renders with textures.
- [x] 6.5 Frame override: `./build-engine/fury examples/Demo.lua --screenshot /tmp/demo_f10.png --screenshot-frame 10`. Confirm: capture happens at frame 10, visually similar to default.
- [x] 6.6 Bad frame: `./build-engine/fury examples/Demo.lua --screenshot-frame 0 --screenshot /tmp/x.png`. Confirm: exit 1, stderr cites the requirement.
- [x] 6.7 Bad path: `./build-engine/fury examples/Demo.lua --screenshot /no/such/dir/x.png`. Confirm: exit 1, stderr names the path and reason.
- [x] 6.8 CLI extraction: `./build-engine/fury convert fbx examples/bin/Resource/Scene/tank.fbx /tmp/tank_out.json`. Confirm: `/tmp/tank_out.json` exists; `/tmp/body.jpg`, `/tmp/wheels.jpg`, `/tmp/grass.jpg` exist as siblings; `tank_out.json`'s texture entries reference the bare filenames; `./build-engine/fury info /tmp/tank_out.json` reports the same shape as `./build-engine/fury info /tmp/tank.glb`.
- [x] 6.9 Round-trip: After 6.8, `./build-engine/fury examples/Demo.lua /tmp/tank_out.json --screenshot /tmp/demo_roundtrip.png` (or whatever loader path resolves the absolute path). Confirm: PNG matches `/tmp/demo_tank.png` (same texturing).
- [x] 6.10 Save-As idempotency: Demo.lua → File → Open → tank.fbx → File → Save As → `Resource/Scene/tank_saved.json`. Inspect `Resource/Scene/`: confirm `body.jpg`/`wheels.jpg`/`grass.jpg` are present (existing files on disk should be byte-equal to embedded → no rewrite). Re-save to a different name; same files reused. (GUI-only test; underlying byte-equal short-circuit is implemented in `ExtractMemoryBackedTextures` and exercised when the round-trip from 6.9 sees pre-existing siblings.)
- [x] 6.11 Verify `Log.txt` after import does NOT contain `_image<i>.jpg` references in any `Texture::CreateFromImage` failure logs (those temp paths shouldn't be reachable now).

## 7. Documentation

- [x] 7.1 In `docs/CLI.md`, add a "Screenshot mode" section: both flags, default frame index (2), exit codes, headless caveat (window briefly visible), at least one full example (`./fury Demo.lua tank.fbx --screenshot /tmp/x.png`).
- [x] 7.2 In `docs/CLI.md`, in the `convert fbx` section, add a paragraph explaining that texture sibling files are now produced when the output is `.json` or `.bin` — name the filename-derivation rules and the collision behavior.
- [x] 7.3 In `docs/LUA.md`, add one cross-reference under the launcher section pointing at `docs/CLI.md`'s screenshot section.

## 8. Spec sync prep

- [x] 8.1 Run `openspec validate add-screenshot-debug-and-fix-fbx-textures` and resolve any structural errors.
- [x] 8.2 After implementation passes manual verification, prepare for `/opsx:archive`. Note any drift between proposed and shipped behavior in the proposal as a brief follow-up note if needed.
