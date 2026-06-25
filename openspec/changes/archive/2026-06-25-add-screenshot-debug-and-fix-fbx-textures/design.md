## Context

Two findings drive this change:

1. **Why FBX imports render untextured.** `tank.fbx` and `james.fbx` carry their JPEG textures embedded in the FBX. The current FBX→glTF chain (FBX2glTF subprocess → tinygltf load → engine import) re-embeds them into a `.glb` (FBX2glTF default). The engine then:
   - Configures tinygltf with `SetImagesAsIs(true)` so the encoded bytes stay in the bufferView (no decode), then
   - Synthesizes a temp URI like `/tmp/fury_runtime_fbx/tank_image0.jpg` for each embedded image, and (on the CLI write path) calls `ExtractEmbeddedImages` to write those bytes to disk before texture upload, then
   - Calls `Texture::CreateFromImage(synthetic_path, ...)` to upload eagerly.

   The runtime Lua path (`Importer.LoadFbx`) sets `Options::output_basename_no_ext` correctly, so extraction *does* run. But the *saved scene* that the CLI's `convert fbx tank.fbx /tmp/out.json` produces references those temp paths in the `.json` — making the saved scene non-portable (move it to another machine, the texture paths are dead). And the runtime `Texture` carries an absolute temp path as its `m_FilePath`, so re-saving the scene from the editor preserves the same dead reference.

   The actual symptom in the user's screenshot — flat-shaded geometry — is the failure mode that occurs when the temp-extracted file is missing or the extraction step silently failed. Even when the chain works, the architecture is wrong: the texture's source-of-truth lives in two places (memory inside `tinygltf::Buffer`, and a temp file we just wrote), and the saved scene's reference is to the temp file, which is the *less* permanent location.

   The fix the user proposed — and the right one — is to keep embedded bytes in memory inside the engine `Texture`, only extracting them to disk when the scene is **saved** (which is the moment we need a persistent file reference). This:
   - Eliminates the runtime/CLI behavioral difference (both paths are now identical: in-memory upload).
   - Eliminates the temp-file lifetime question entirely (no temp files).
   - Makes saved `.json`/`.bin` scenes portable (sibling files written next to the output).
   - Lets the user re-save the same scene to a different output location and have the textures travel along.

2. **Why we need a screenshot mode.** Visual bugs like (1) are tedious to verify from a CLI loop. The user wants the agent to run the engine with a Lua script, capture the rendered viewport at a known frame, and hand the image back for inspection. Headless GL on macOS / Linux SFML is non-trivial (no clean "headless context" without EGL surfaceless or OSMesa), so the simplest correct approach: open the same `sf::Window`, render N frames, capture, exit. The window is briefly visible — acceptable for a debug feature.

The engine already runs OpenGL 3.3 core, vendors `stb_image_write.h` next to `stb_image.h`, and has a single main loop in `Engine::Run`. The capture is a small patch in the engine, plus argv parsing in the launcher.

## Goals / Non-Goals

**Goals:**

- FBX/glTF imports with embedded textures render correctly: encoded bytes flow from the bufferView straight to GPU via `stbi_load_from_memory`. No temp files, no synthetic URIs.
- Saved scenes are portable: `convert fbx in.fbx out.json` produces an `out.json` that references sibling files written next to it (`out.json` + `body.jpg` + `wheels.jpg` + `grass.jpg`), with sensible filenames preserved when the source carries them.
- The save path is idempotent: re-saving the same scene to the same output skips identical-byte rewrites.
- The `fury` runtime accepts `--screenshot <path> [--screenshot-frame N]` and produces a PNG of frame N. Works for any Lua script without modifying it.
- All changes are scoped to the engine (C++); no Lua-side script changes required.

**Non-Goals:**

- True offscreen / headless rendering. The SFML window will appear briefly. Adding real headless rendering would require CGL pbuffers (deprecated on macOS) or rewriting the SFML window backend; not worth it for a debug capture feature.
- Recording video / multi-frame captures. One PNG per invocation.
- Re-encoding decoded image bytes back to PNG/JPEG. We pass the encoded bytes verbatim — JPEG stays JPEG, PNG stays PNG.
- Inline-embedding texture bytes into `.json`/`.bin` scenes (e.g. base64 in JSON, dedicated section in the LZ4 envelope). Sibling files are simpler, human-debuggable, and match the existing `scene.json` convention.
- Streaming texture downloads (deleting `m_EncodedBytes` after the GPU upload to save memory). Holding the encoded bytes for the texture's lifetime is fine — JPEG is small (~50–500 KB per image) and dropping the bytes would prevent re-saving the scene to a different location.
- PBR pipeline. Engine remains Lambert in v1.

## Decisions

### Decision 1: Texture state machine — three sources, three states

`Texture` already implicitly has two states (file-backed via `m_FilePath`, procedural via `CreateEmpty` + `SetPixels`). We add a third — memory-backed — without changing the existing two:

| State        | `m_FilePath`  | `m_EncodedBytes` | Source            | Save serializes  |
|--------------|---------------|------------------|-------------------|------------------|
| File-backed  | non-empty     | empty            | `CreateFromImage` | `path`           |
| Memory-backed| empty         | non-empty        | `CreateFromMemory`| (must extract first) |
| Procedural   | empty         | empty            | `CreateEmpty`     | format/w/h/...   |

Memory-backed is a transitional state: an importer puts a texture there; the save path moves it to file-backed before serializing.

`m_EncodedBytes` is `std::vector<unsigned char>` (the natural type — `stbi_load_from_memory` accepts `unsigned char*`). `m_OriginalFilename` is `std::string`. Both are private, default-empty.

### Decision 2: `CreateFromMemory` mirrors `CreateFromImage` but reads from a buffer

```cpp
void Texture::CreateFromMemory(const unsigned char *bytes, size_t len, bool srgb, bool mipmap) {
    DeleteBuffer();
    int channels;
    int w = 0, h = 0;
    unsigned char *pixels = stbi_load_from_memory(bytes, static_cast<int>(len), &w, &h, &channels, 0);
    if (!pixels) { FURYE << "Texture::CreateFromMemory: stbi_load_from_memory failed: " << stbi_failure_reason(); return; }
    m_Width = w; m_Height = h; m_Mipmap = mipmap;
    // ... same glTexStorage2D / glTexSubImage2D as CreateFromImage ...
    stbi_image_free(pixels);
    m_EncodedBytes.assign(bytes, bytes + len);    // retain for save-time extraction
    // m_FilePath stays empty — caller may set m_OriginalFilename via a new setter
}
```

`stbi_load_from_memory` is already linked (`#define STB_IMAGE_IMPLEMENTATION` in `FileUtil.cpp`). No new dependency.

A small companion `Texture::SetOriginalFilename(const std::string&)` is added so the importer can record the hint without exposing the field directly.

### Decision 3: Save-time extraction lives in `FileUtil::SaveFile` and `SaveCompressedFile`

`FileUtil::SaveFile(scene, output_path)` gains a pre-serialize step. The traversal walks the scene's `EntityManager`'s materials and their textures. For each memory-backed texture:

```cpp
// Pseudocode in FileUtil::SaveFile / SaveCompressedFile
fs::path output_dir = fs::path(output_path).parent_path();
std::string output_stem = fs::path(output_path).stem().string();

auto entities = scene->GetEntityManager();
for (auto &mat_ent : entities->GetByType(typeid(Material))) {
    auto mat = std::static_pointer_cast<Material>(mat_ent);
    for (auto &[slot, tex] : mat->GetTextures()) {
        if (!tex || !tex->IsMemoryBacked()) continue;
        std::string filename = ChooseExtractFilename(tex, output_stem, output_dir);  // collision suffixed
        fs::path out_file = output_dir / filename;
        if (!FileEqualsBytes(out_file, tex->GetEncodedBytes())) {
            std::ofstream f(out_file, std::ios::binary);
            if (!f) { FURYE << ...; return false; }
            f.write(reinterpret_cast<const char *>(tex->GetEncodedBytes().data()), tex->GetEncodedBytes().size());
        }
        tex->SetFilePathRelative(filename);   // texture transitions to file-backed
    }
}
return DoExistingSaveTraversal(scene, output_path);
```

Helper `ChooseExtractFilename` prefers the texture's `m_OriginalFilename` (e.g. `body.jpg`); falls back to `<output_stem>_<texture_name>.<ext>` where `<ext>` is sniffed from the first bytes; appends `_<n>` on collision.

`FileEqualsBytes` is a simple length-then-byte-compare; no SHA-1 needed for files this small. Reuse-on-equal is the idempotency story.

`Material::GetTextures()` already exists (or is trivially added — the texture map is internal but iterable).

A `Texture::IsMemoryBacked()` method is added: `return m_FilePath.empty() && !m_EncodedBytes.empty();`.

A `Texture::SetFilePathRelative(const std::string&)` setter is added (or `SetFilePathAndSRGB` is reused — the existing one sets both fields and is the natural fit). Setting `m_FilePath` is enough for `Texture::Save` to serialize the new path.

**Alternative considered (rejected):** Embed the bytes inline in the JSON via base64. Simpler in some ways (no sibling files to manage) but bloats the JSON, breaks the existing `body.jpg` sibling convention, and makes scenes harder to inspect/diff. Sibling files match existing conventions.

**Alternative considered (rejected):** Have the importer extract to disk eagerly, then have re-saves re-extract from the in-memory bytes if they exist. Equivalent to the current architecture in performance terms, but doesn't fix the temp-file portability problem (the *first* save still references temp paths if the user just imports and renders without saving).

### Decision 4: Where the extraction lives — `FileUtil`, not `Scene::Save`

Two reasons:
- `Scene::Save` runs as part of the JSON traversal and doesn't know the output path or output directory. `FileUtil::SaveFile` does — it gets the path as a parameter. Plumbing the directory into the traversal would be uglier than keeping the extraction at the boundary.
- `FileUtil::SaveCompressedFile` (LZ4) needs the same extraction. Hosting it in `FileUtil` shares the code naturally between both functions; both call a private `ExtractMemoryBackedTextures(scene, output_dir, output_stem)` helper.

### Decision 5: Original filename hints — preserved through FBX → glTF → engine

FBX2glTF emits `image.name` set to the original FBX texture filename (verified: `tank.fbx` → glb has `image.name = "wheels.jpg"`, `"body.jpg"`, `"grass.jpg"`). The importer captures this via `Texture::SetOriginalFilename(image.name)`. Save-time extraction prefers this name. So `convert fbx tank.fbx /tmp/out.json` produces `/tmp/body.jpg` etc., not `/tmp/out_image0.jpg`. The user sees recognizable filenames, and re-imports from `/tmp/out.json` resolve textures naturally.

When `image.name` is empty (some glb authors leave it blank), we fall back to `<output_stem>_image<i>.<ext>` for the synthesized name — same as the current behavior, just relocated to save time.

### Decision 6: Screenshot — runtime flag in `examples/main.cpp`, capture in `Engine::Run`

Two layers:

1. **Flag parsing in `examples/main.cpp`.** Walk `argv` after `argv[1]` looking for `--screenshot` and `--screenshot-frame`; remove them (and their values) from the slice handed to Lua's `arg`. Validate (`--screenshot-frame N > 0` and ≤ 1000). Build an `EngineOptions` extension carrying the values.

2. **Capture in `Engine::Run`.** Add a frame counter to the loop. After `window.display()` for the frame whose index matches `opts.screenshot_frame`, call a file-static helper:

```cpp
namespace {
    bool WriteBackBufferAsPng(const std::string &path, sf::Window &window) {
        const sf::Vector2u sz = window.getSize();
        std::vector<unsigned char> pixels(sz.x * sz.y * 4);
        glReadPixels(0, 0, sz.x, sz.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // GL is bottom-left origin; PNG is top-left. Flip vertically.
        std::vector<unsigned char> flipped(pixels.size());
        for (unsigned y = 0; y < sz.y; ++y) {
            std::memcpy(&flipped[y * sz.x * 4], &pixels[(sz.y - 1 - y) * sz.x * 4], sz.x * 4);
        }
        return stbi_write_png(path.c_str(), sz.x, sz.y, 4, flipped.data(), sz.x * 4) != 0;
    }
}
```

`STB_IMAGE_WRITE_IMPLEMENTATION` is `#define`d in `Engine.cpp` (one TU only — `FileUtil.cpp` defines `STB_IMAGE_IMPLEMENTATION`, which is a different macro). Verified no other TU defines either implementation macro for `stb_image_write.h`.

After capture, set `running = false` to exit cleanly. `Engine::Run` already calls `cb.OnShutdown` on exit, so cleanup runs. Communicate the exit code back via a new `EngineOptions::exit_code_out` `int *` field (the launcher passes `&local_int`; engine writes 0 on success, 1 on capture failure; launcher returns it from `main`).

### Decision 7: Frame index defaults to 2

Frame 1 is the first render after `on_init`, when state is built but `on_update` hasn't run. Frame 2 captures after one `on_update` tick. For a user-supplied editor that builds state in `on_init` and renders in `on_update`, frame 2 is safe. Users override with `--screenshot-frame N`.

### Decision 8: No Lua API for screenshot

Capture is opaque to the running script. No callbacks. The flags are stripped from `arg` before Lua sees them.

### Decision 9: Documentation

- `docs/CLI.md` gains a "Screenshot mode" section describing flags + recipe.
- `docs/CLI.md` gains a note in the `convert fbx … .json` section that texture sibling files are now produced.
- `docs/LUA.md` gains one cross-reference line.

## Risks / Trade-offs

[**Risk: Memory growth from holding encoded bytes.**] Each memory-backed texture retains its encoded bytes for its lifetime. JPEGs are typically 50–500 KB; even a heavy 100-texture scene is ~30 MB. → Acceptable. If we ever ship something with thousands of HD textures, we add an opt-in "drop bytes after first save" mode. v1 keeps it simple.

[**Risk: Save-path file write failures.**] `FileUtil::SaveFile` is a hot path; adding sibling-file writes adds new failure modes. → Mitigation: each write is checked; failure aborts the save with a clear `FURYE` message naming the path. The scene file itself is not written until extraction succeeds, so a failure leaves the user's working directory in a clean state (no stale half-extracted files plus a stale scene).

[**Risk: Filename collisions on save.**] Two textures with the same original filename or same synthesized name. → Numeric suffix collision-resolution scheme; tested in spec scenarios.

[**Risk: PBR-discarded materials still look different from scene.json.**] Even with textures resolved, the FBX import sets `metallicFactor` and `roughnessFactor` aside (engine is Lambert), so the rendered tank may not match scene.json byte-for-byte. → Acceptable for v1; documented in the importer warning.

[**Risk: Window flash during screenshot.**] User sees an SFML window for 50–100 ms. → Acceptable for a debug feature; documented in CLI.md.

[**Risk: stb_image_write doubling.**] Defining `STB_IMAGE_WRITE_IMPLEMENTATION` in two TUs causes link errors. → Mitigation: define only in `Engine.cpp`; verify a clean build before merging.

[**Risk: Test coverage.**] Engine has no automated visual-regression harness. → The screenshot mode is the foundation for one. v1 ships manual verification (compare PNGs by eye); v2 could compare against golden images. Out of scope for this change.

## Migration Plan

No on-disk file format changes. No Lua API changes. No breaking changes to the CLI subcommand surface.

One internal API removal: `GltfImporter::Options::output_basename_no_ext` and the `ExtractEmbeddedImages` function are deleted. The only in-tree callers (`Cli.cpp:convert fbx`, `LuaBindings.cpp:LoadFbx`) are updated in the same change. Out-of-tree callers (none known) would see a compile error and need to drop the field assignment.

Roll out in a single commit; rollback is a revert.

## Open Questions

None. The architecture is committed: in-memory texture loading + on-save extraction.
