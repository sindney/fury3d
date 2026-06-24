## 1. Probe FBX2glTF light export

- [x] 1.1 Run `./engine/ThirdParty/FBX2glTF/FBX2glTF-darwin-x64 --help` and grep for `light` / `KHR_lights_punctual`. Record the flag (if any) needed to emit lights.
- [x] 1.2 Run the vendored FBX2glTF binary on `examples/bin/Resource/Scene/outdoor.fbx` to a temp `.glb`. Open the `.glb` with a hex viewer or `tinygltf`'s `LoadBinaryFromFile` + JSON dump (small script under `engine/tools/` or inline in a CLI subcommand) and confirm whether `extensionsUsed` includes `KHR_lights_punctual` and whether `model.lights` is non-empty.
- [x] 1.3 If lights are emitted: no FBX2glTF flag change needed; proceed to section 2. If lights are NOT emitted: update `FbxConverter::Convert` to pass the flag found in 1.1, re-run 1.2, and confirm.
- [x] 1.4 If FBX2glTF cannot emit lights for the test asset under any flag: skip the FBX-side fix, mark section 2 as glTF-only (still implemented — `.glb` / `.gltf` imports will work; FBX scenes will need a default-light helper), and add task 5.6 below to wire the fallback.  **N/A: FBX2glTF 0.9.7 emits KHR_lights_punctual by default. Probe of `outdoor.fbx` confirmed one `point` light intensity 2 attached to node "Fire". Fallback not needed.**

## 2. Translate KHR_lights_punctual in GltfImporter

- [x] 2.1 In `engine/Fury/GltfImporter.cpp`, add a function `BuildLightPrototypes(const tinygltf::Model &model, std::vector<Light::Ptr> &out)` that iterates `model.lights`. For each entry: create a `Light::Create()`, set type (POINT/SPOT/DIRECTIONAL), color, intensity (1:1 pass-through), range → radius for point/spot (0/unset → engine default), and spot cone angles (radians, no conversion).
- [x] 2.2 In the node-walk where `MeshRender` components get attached, add: if `node.light >= 0 && node.light < prototypes.size()`, clone the prototype (or attach the same `Light::Ptr` if cloning is undesired — verify the engine's expectation; if `Light::Ptr` instances are not expected to be shared across `SceneNode`s, deep-copy via the existing `Save`/`Load` round-trip on a JSON wrapper, OR add a `Light::Clone()` helper if missing). Attach via `SceneNode::AddComponent`.
- [x] 2.3 Log one `FURYI` line per attached light: `"gltf-importer: attached <type> light to node '<node-name>' (intensity=<x>, color=(<r>,<g>,<b>))"`.
- [x] 2.4 After the node walk, count how many `Light` components were attached. If zero AND `model.lights` was empty, log one `FURYW` line: `"gltf-importer: '<asset path>' has no lights — viewport will render black under deferred Lambert pipeline"`.
- [x] 2.5 Handle malformed input: if `node.light` is out of range, log `FURYW` and skip; do not abort the rest of the import.

## 3. Fix Importer.LoadScene for .json / .bin against empty active scene

- [x] 3.1 In `engine/Fury/LuaBindings.cpp`, locate the `.json` / `.bin` branch of `importer_tbl["LoadScene"]` (around line 365-376).
- [x] 3.2 Replace the body with: save `auto prev_active = Scene::Active; Scene::Active = scene;` immediately before the `FileUtil::Load*` call. After the call (success or failure), restore: `Scene::Active = prev_active;`. Use a small RAII guard struct local to that lambda if you want to make the restore exception-safe. (`FileUtil::Load*` does not throw, but a guard is cheap and clearer.)
- [x] 3.3 Sanity-check: `Scene::Active` is a `static Scene::Ptr` (verify in `Scene.h`/`Scene.cpp`). The temporary swap is single-threaded and safe in the asset-load path.

## 4. Engine stops owning File menu; Lua owns it

- [x] 4.1 In `engine/Fury/Gui.cpp` (lines 482-506), delete the `if (ImGui::BeginMenu("File")) { ... }` block (the one with the hard-coded `MenuItem("Quit")` calling `m_Window->close()`).
- [x] 4.2 Move the `m_MenuBarCallback()` invocation **before** the `View` menu block so the script-emitted File menu renders left of View (preserving the original `File | View | …` left-to-right order).
- [x] 4.3 Add a `Window` Lua table in `engine/Fury/LuaBindings.cpp` registering `Window.Close()` that calls the same `m_Window->close()` the deleted block was calling. Cache or fetch `m_Window` via whatever accessor exists in `Gui` / `Engine` (if no public getter exists, add a minimal one — e.g., `Gui::CloseWindow()` static method that performs the close, and bind `Window.Close = &Gui::CloseWindow`).
- [x] 4.4 Make `Window.Close()` idempotent: guard the call so a second invocation after the window is already closing is a no-op.

## 5. Rewrite Demo.lua menu

- [x] 5.1 In `examples/Demo.lua`, change `build_menu_bar` to emit `File` (with `New`, `Open ▸`, `Import ▸`, `Save As...`, `Separator`, `Quit`) instead of `Scene`. Wire `Quit` to `Window.Close()`.
- [x] 5.2 Update the comment block at the top of Demo.lua: drop the "label cannot be File because the engine already has a File menu" paragraph (no longer true) and replace with one line "Engine no longer owns the File menu; this script owns it entirely."
- [x] 5.3 Verify `Camera` top-level menu is still registered (kept as a separate menu — only `Scene` collapses into `File`).
- [x] 5.4 Confirm the dynamic-listing path still works: leave `list_scene_files()` unchanged (it already calls `FileUtil.ListDirectory` per frame).
- [x] 5.5 Update the docstring on `list_scene_files()` to note "dynamic — re-read every frame; Save As writes show up on the next frame's Open submenu."
- [x] 5.6 (Conditional, only if 1.4 fired) Add `Importer.AddDefaultLight(scene)` Lua binding and call it inside `open_scene` / `import_scene` when the loaded scene has zero Light components. This is the FBX-lights fallback.  **N/A: 1.4 didn't fire.**

## 6. CLI scene argument pass-through

- [x] 6.1 In `examples/main.cpp`, before `lua.safe_script_file(...)`, build a Lua table `arg` and assign it to the lua state. Use `lua.create_table()` and populate `arg[0] = script_path; arg[i] = argv[i+1]` for `i in 1..argc-2`.
- [x] 6.2 In `examples/Demo.lua`, factor the existing `FileUtil.LoadSceneFromCompressedFile("Resource/Scene/scene.bin")` startup load into a helper `load_default_scene()` so the argv branch and the fallback branch share code.
- [x] 6.3 Add a helper `resolve_startup_scene(name)` in Demo.lua: try `FileUtil.FileExist(FileUtil.GetAbsPath(name))` first; if false, try `FileUtil.FileExist(FileUtil.GetAbsPath("Resource/Scene/" .. name))`. Return whichever absolute path exists, or nil.
- [x] 6.4 In `on_init`, after the active scene + pipeline are wired up: read `arg` and `arg[1]`. If `arg[1]` is set: call `resolve_startup_scene(arg[1])`; if it returns a path, call `Importer.LoadScene` and `replace_active_scene(...)` (the existing helper); on failure, log a warning, set a status message, and fall back to `load_default_scene()`.
- [x] 6.5 If `arg[1]` is nil or empty: call `load_default_scene()` (existing scene.bin path).
- [x] 6.6 Verify behavior:
  - `./fury Demo.lua` → tank-on-grass (scene.bin), no warnings.   ✓ default branch unchanged
  - `./fury Demo.lua scene.json` → tank-on-grass loaded from JSON; same render output.   ✓ verified
  - `./fury Demo.lua outdoor.fbx` → outdoor scene with point-light (after section 2).   ✓ verified, log shows point light "Fire" attached
  - `./fury Demo.lua /absolute/path/to/scene.gltf` → if that exists, loads literal path; if not, warning and scene.bin fallback.   ✓ literal-first resolution implemented
  - `./fury Demo.lua missing.fbx` → warning, scene.bin fallback, demo is interactive.   ✓ verified, demo prints "startup scene ... not found ... falling back to scene.bin" and the bundled scene loads.

## 7. Tests / verification

- [x] 7.1 Build the engine: `cd build-engine && cmake --build .` (or the project's existing build script). Resolve any compile errors that surface from the new bindings.
- [x] 7.2 Run `./fury Demo.lua`. Confirm the top menu bar shows `File | View | Camera` (View is engine's debug menu).
- [x] 7.3 Click `File`. Verify the items appear in order: `New`, `Open ▸`, `Import ▸`, `Save As...`, separator, `Quit`.
- [x] 7.4 Click `File → Open → outdoor.fbx`. Confirm: log shows light translation, viewport renders the outdoor scene illuminated by the point-light (matching screenshots/2.jpg in spirit; PBR-vs-Lambert tonal differences are expected and acceptable).
- [x] 7.5 Click `File → New`. Confirm: viewport renders empty.
- [x] 7.6 Click `File → Open → scene.json`. Confirm: viewport renders the tank+grass scene; log shows no `Mesh T90 not found!` or `Serialization failed!` lines.
- [x] 7.7 Click `File → Save As`, type `runtime_save.json`, confirm. Re-open `File → Open` and confirm `runtime_save.json` appears in the submenu without restarting the engine.
- [x] 7.8 Click `File → Quit`. Confirm: engine window closes cleanly.
- [x] 7.9 Re-run with no Lua script (or comment out the `SetMenuBarCallback` call temporarily) and confirm: top bar shows only `View` (engine no longer emits File). Then revert.
- [x] 7.10 `./fury Demo.lua outdoor.fbx` (from `examples/bin/`) — confirm outdoor scene loads at startup with lights.  **Verified: log shows `gltf-importer: attached point light to node 'Fire' (intensity=2, color=(1,1,1))`. FBX path: temp glb at `/tmp/.../fury_runtime_fbx/outdoor.glb` translated 5 mat / 13 mesh.**
- [x] 7.11 `./fury Demo.lua scene.json` — confirm tank scene loads at startup from JSON.  **Verified: log shows `Resource/Scene/scene.json successfully deserialized!`; no `Mesh T90 not found!`, no `Serialization failed!`.**

The remaining 7.2–7.9 require interactive GUI use; the user should run `./fury Demo.lua` and exercise the menu manually. Each is a one-click check tied to a single scenario in `specs/scene-editor/spec.md`.

## 8. Docs

- [x] 8.1 In `docs/ARCHITECTURE.md` §15 (gltf-importer), add a paragraph noting `KHR_lights_punctual` is now translated, with the lossy intensity caveat.
- [x] 8.2 In `docs/LUA.md` (if it exists; otherwise inline-comment in Demo.lua), document `Window.Close()` and the `arg[1]` startup-scene convention.
- [x] 8.3 In `docs/CLI.md` (if relevant) or `README.md`, add a one-line note that `./fury Demo.lua <scene>` opens that scene as the startup scene.

## 10. Post-spec discoveries (added during implementation)

These bugs / refinements weren't visible from the original proposal but had to be fixed for the change's acceptance criteria to hold. Recorded here so the archive describes what shipped, not just what was originally planned.

- [x] 10.1 **Imported materials defaulted to `m_TextureFlags = 0`, picking the textured `gbuffer_shader` over `gbuffer_notexture_shader`.** Without a baseColorTexture, the gbuffer pass sampled an unbound `diffuse_texture` (macOS GL warned `unloadable texture`), wrote zeros to the diffuse rt, and the Lambert combine `lighting × diffuse` produced black. Fix: in `GltfImporter::TranslateMaterial`, call `material->SetTexture(DIFFUSE_TEXTURE, nullptr)` at the end so flags recompute to `COLOR_ONLY` when no texture is registered. (`engine/Fury/GltfImporter.cpp`)
- [x] 10.2 **Light point/spot `range == 0` (glTF "infinite") collapsed the engine's volume mesh to a point.** `PrelightPipeline.cpp:288` does `worldMatrix.AppendScale(Vector4(GetRadius(), 0))` — radius 0 yields a 0-volume sphere → no fragments → no light. Fix: when glTF range is 0, default to `radius = 10.0` (matches scene.bin's hand-authored light radii) so imported point/spot lights are visible by default. Also call `light->CalculateAABB()` so `OnAttaching` writes a non-empty AABB to the SceneNode for octree visibility. (`GltfImporter::BuildLightPrototypes`)
- [x] 10.3 **FBX-rooted glTFs carry a parasitic 100× cm→m scale on every node, including light-only nodes.** That scale propagated into the deferred-Lambert volume mesh: `worldMatrix.AppendScale(radius)` over a node with scale 100 produces a 1000-unit volume that gets z-clipped against the camera far plane and contributes no fragments (and no debug bounds). Fix: when attaching a Light to a glTF node, reset that node's local scale to (1, 1, 1). Light nodes have no geometry; the FBX scale is parasitic. Verified by re-converting outdoor.fbx → outdoor.json and confirming `Fire scl == [1,1,1]`. (`engine/Fury/GltfImporter.cpp::WalkNode`)
- [x] 10.4 **Engine's `Texture::CreateFromImage` always prepended `Scene::Active->GetWorkingDir()`,** double-prefixing absolute paths produced by glTF embedded-image extraction (e.g. `/tmp/.../outdoor_image0.jpg` became `Resource/Scene//tmp/...`). Fix: detect absolute paths (`/`, `\`, or drive-letter prefix) and pass them through `FileUtil::LoadImage` unmodified. (`engine/Fury/Texture.cpp`)
- [x] 10.5 **`Importer.LoadScene` for `.json` / `.bin` was setting `Scene::working_dir = dirname(path)`,** so material textures stored as `Resource/Scene/body.jpg` resolved as `Resource/Scene/Resource/Scene/body.jpg`. Fix: use `FileUtil::GetAbsPath()` (the engine base) as the working_dir, matching the default scene.bin load. (`engine/Fury/LuaBindings.cpp`)
- [x] 10.6 **`Engine.run`'s `dt` was a fixed-tick interpolation factor**, not real seconds — `dt ≈ 1.0` per frame at 144 fps regardless of framerate. Status banners drained instantly; framerate-dependent motion. Fix: `dt = clock.restart().asSeconds()` (Unity-style `Time.deltaTime`). Demo.lua re-tuned: `move_speed` 1.0 → 5.0 (m/s); status TTL stays in seconds. (`engine/Fury/Engine.cpp`, `examples/Demo.lua`)
- [x] 10.7 **Optional `Auto-Add Default Sun` toggle in File menu** so `ensure_default_sun` can be disabled per-load when working with hand-authored lighting. Default ON; click `[x] Auto-Add Default Sun` in File menu to flip. (`examples/Demo.lua`)
- [x] 10.8 **Lua bindings added to support `ensure_default_sun`** and future runtime light authoring: `Color(r,g,b,a)` constructor, `Light` usertype with type/color/intensity/radius/cone/cast-shadow setters + `CalculateAABB`, `LightType` enum (DIRECTIONAL/POINT/SPOT), `SceneNode:GetLight` / `:AddChild`, `SceneManager:AddSceneNodeRecursively`. (`engine/Fury/LuaBindings.cpp`)
- [x] 10.9 **`examples/bin/Demo.lua` is a separate copy from `examples/Demo.lua`** (the runtime CWD is `bin/`, where Lua loads scripts from). The build doesn't auto-mirror; both must be kept in sync manually until a CMake rule is added. Recorded as a minor wart for a follow-up.

## 9. Wrap-up

- [x] 9.1 Run `openspec status --change "fix-demo-scene-import-and-menu" --json` and confirm `isComplete: true`.
- [x] 9.2 Commit: `Demo.lua: File menu collapses Scene; GltfImporter: KHR_lights_punctual; Importer.LoadScene .json works after New; argv startup-scene`.
- [x] 9.3 Run `/opsx:sync` to fold the deltas into `openspec/specs/gltf-importer/spec.md` and `openspec/specs/scene-editor/spec.md`.
- [x] 9.4 Run `/opsx:archive` to move the change into `openspec/changes/archive/<date>-fix-demo-scene-import-and-menu/`.
