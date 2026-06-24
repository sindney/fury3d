## Why

The previously-shipped scene-editor change (Demo.lua's File / Scene menu + `Importer.Load*` + `MergeInto`) works for the demo's bundled `scene.bin`, but two real flows surfaced bugs the first time a user actually exercised them:

1. **Imported FBX scenes render pitch black.** The user opened `outdoor.fbx` (which originally rendered with a fire point-light in screenshots/2.jpg — see the engine's original-feature reference image) and the viewport went black. Logs confirm the FBX → glb chain ran fine (`gltf-importer: '/tmp/fury_runtime_fbx/outdoor.glb' translated 5 material(s), 13 mesh(es), 1 glTF scene(s) -> SceneNode tree, 0 animation(s)`), but **no `Light` component was created**. `GltfImporter.cpp` does not parse `tinygltf::Model::lights` (KHR_lights_punctual) — every imported scene comes back unlit, which the deferred-Lambert pipeline renders as black.
2. **`Scene → New` + `Scene → Open → scene.json` fails with `Mesh T90 not found! / Serialization failed!`** Root cause: `Importer.LoadScene` for `.json` paths runs `Scene::Load(scene_imported, …)`, which deserializes into a *temporary* `Scene` and resolves `MeshRender`'s mesh string via `Scene::Active->GetEntityManager()` (see `MeshRender.cpp:46-58`, `Scene.cpp:21-24`). After `Scene → New`, the active scene is empty, so the lookup fails. The fix is for `Importer.LoadScene` to **make the import target active during load** (or use a load path that resolves against the scene being loaded into, not `Scene::Active`).

Also surfaced during the same review:

- The Scene menu sits *next to* the engine's built-in `File` menu (which only carries `Quit`). The user asked for the unified File → New / Import / Open / Save As shape, with Quit kept at the bottom. The current "two top-level menus that should be one" UX is sub-par.
- The user asked to confirm that Open/Import enumerates files from `Resource/Scene/` dynamically. It already does (`FileUtil.ListDirectory` is `std::filesystem::directory_iterator`, see `LuaBindings.cpp:267-307`), so this just needs to be called out and verified — no code change beyond a one-line scenario in the spec.

The goal of this change: get the imported outdoor scene to render lit, get scene.json round-trip working after a New, and consolidate the two menus into one File menu — without re-introducing the ImGui-ID-collision footgun the previous change called out (a second top-level "File" menu shares an ID hash).

## What Changes

- **glTF importer: translate `KHR_lights_punctual` to engine `Light` components.**
  - `GltfImporter::Import` SHALL iterate `tinygltf::Model::nodes` and, for any node with a `light` index ≥ 0, look up the referenced `tinygltf::Light` and attach an engine `Light` component to the emitted `SceneNode`.
  - Mapping: glTF `point` → `LightType::POINT`; glTF `spot` → `LightType::SPOT` (with `innerConeAngle` / `outerConeAngle` copied to the engine's inner/outer cone fields); glTF `directional` → `LightType::DIRECTIONAL`. `color` → engine `Color` (RGB), `intensity` → engine intensity (1:1 in v1; PBR-correct luminous-flux/lux conversion is deferred), `range` → engine radius for point/spot lights (0 or unset → engine default).
  - One info-level log line per imported light naming the source node, type, and intensity.
  - When `tinygltf::Model::lights` is empty AND the import resolves zero lights, the importer SHALL emit a single warning advising "scene has no lights — viewport will render black under deferred Lambert" so the next user hits the same wall with a clearer signal.

- **FBX → glTF conversion: pass `--khr-materials-unlit=off --lights` (or the converter's equivalent flag) so FBX2glTF emits `KHR_lights_punctual` when the source FBX contains lights.** Inspect FBX2glTF's actual CLI surface during implementation; if the binary version vendored does not emit lights, fall back to the "default light after import" sub-step below as a separate task.

- **`Importer.LoadScene` (Lua): make `.json` / `.bin` loads work against an empty active scene.** Two acceptable mechanisms:
  1. Temporarily swap `Scene::Active` to the import target during the load, restore after. (Surgical; preserves the existing engine assumption.)
  2. Change `Scene::Load` to resolve `MeshRender::Load`'s mesh/material lookups against `this->m_EntityManager` instead of `Scene::Active->m_EntityManager`. (Cleaner; eliminates a footgun for any future code that loads into a non-active scene.)
  - Choice and rationale documented in `design.md`. The acceptance bar: `Scene → New` → `Scene → Open → scene.json` produces an equivalent scene to the launch-time `scene.bin` load.

- **Menu restructure: collapse Scene menu into the File menu.** The engine-owned File menu (currently: `Quit`) SHALL be entirely Lua-driven going forward. New shape:
  - `File`
    - `New`             — clears active scene
    - `Open` ▸          — submenu listing `Resource/Scene/*.{json,bin,gltf,glb,fbx}` from `FileUtil.ListDirectory`
    - `Import` ▸        — submenu (same list) — merges into active scene
    - `Save As...`      — modal for filename, writes to `Resource/Scene/<name>.{json,bin}`
    - *(separator)*
    - `Quit`            — closes the window
  - This requires:
    - A new `Window.Close()` Lua binding so Quit can be implemented in Lua (the engine currently calls `m_Window->close()` directly from C++).
    - Removing the hard-coded `File` menu block in `Gui::ShowDefault` (lines 484-501) — the menu bar is now purely the engine's `View` menu (Profiler / GBuffer / Shadow buffers) plus whatever the Lua callback emits, and the callback is responsible for the entire File menu.
    - Demo.lua moves its current top-level `Scene` menu into a `File` BeginMenu, adds `Separator` + `MenuItem("Quit")` calling `Window.Close()`.

- **Spec touch-up: confirm `FileUtil.ListDirectory` is dynamic, not hard-coded.** Already true — add an explicit scenario to scene-editor pinning this contract.

- **CLI: forward extra `argv` to the Lua script; Demo.lua honors `arg[1]` as a startup scene path.** Today the engine treats `argv[1]` as the Lua script path (or defaults to `Demo.lua`) and ignores `argv[2..]`. Change `examples/main.cpp` to populate a Lua-side `arg` table (standard Lua convention: `arg[0]` is the script, `arg[1..N]` are the rest). Demo.lua's `on_init` SHALL inspect `arg[1]` — if set and the file exists, dispatch through `Importer.LoadScene` (which supports `.json`, `.bin`, `.gltf`, `.glb`, `.fbx`); otherwise fall back to the existing `Resource/Scene/scene.bin` startup load. The path may be absolute, or relative to `examples/bin/Resource/Scene/` (we try the literal path first, then prepend `Resource/Scene/`). This makes `./fury Demo.lua outdoor.fbx` a one-liner reproducer for the bugs the user just hit.

Non-goals (deferred):
- PBR-correct light intensity conversion (luminous flux → engine intensity). v1 is identity-mapped; the warning in the importer flags this.
- Light shadow-casting flags. Imported lights are non-shadow-casting in v1.
- IES profiles / `KHR_lights_image_based`.
- Light-tinting from glTF emissive — emissive stays a material property as today.
- Editing imported lights in-engine. Open/Save round-trip is via the existing JSON pipeline.

## Capabilities

### New Capabilities

<!-- None. -->

### Modified Capabilities

- `gltf-importer`: add a new requirement covering `KHR_lights_punctual` translation (point / spot / directional → `LightType`). Update the existing material-mapping requirement to clarify that lights are now a separate channel and PBR-vs-Lambert pipeline mismatch is still the cause of the brightness/saturation difference.
- `scene-editor`: replace the current "Scene menu (separate from File menu)" requirement with a "File menu owns New / Import / Open / Save As / Quit" requirement; clarify that `Importer.LoadScene` works for `.json` / `.bin` against an empty active scene; add scenario pinning `FileUtil.ListDirectory` enumeration of `Resource/Scene/`.

## Impact

- **Code modified**:
  - `engine/Fury/GltfImporter.cpp` — light translation loop; warning emission on zero-lights scenes.
  - `engine/Fury/LuaBindings.cpp` — `Importer.LoadScene` (.json/.bin path now sets active temporarily or uses a new `Scene::LoadInto`); new `Window.Close()` binding.
  - `engine/Fury/Scene.{h,cpp}` — optionally, a `LoadInto(scene, wrapper)` helper that resolves entity lookups against `scene`'s manager rather than `Scene::Active`. Chosen mechanism per design.md.
  - `engine/Fury/Gui.cpp` — remove the hard-coded File menu block; menu bar callback runs before View/built-ins so File renders left of View.
  - `engine/Fury/FbxConverter.cpp` — pass the FBX2glTF flag that emits lights, IF that flag exists in the vendored binary; tasks.md probes this.
  - `examples/main.cpp` — populate `lua["arg"]` (an `arg[0]=script, arg[1..N]=rest` table, Lua's standard convention) before `safe_script_file` so scripts can read launch arguments.
  - `examples/Demo.lua` — collapse two top-level menus into one `File` menu; add Quit; wire `Window.Close()`; honor `arg[1]` as a startup scene to open.
- **Asset behavior**:
  - `outdoor.fbx` re-imports with the point-light (the fire) restored. Outdoor scene matches screenshots/2.jpg again (modulo PBR-vs-Lambert tonal differences).
  - `scene.bin` and `scene.json` continue to load identically to today (no on-disk format change).
  - `Scene → New → Open → scene.json` succeeds (currently fails with `Mesh T90 not found!`).
  - `./fury Demo.lua outdoor.fbx` opens the FBX as the startup scene; `./fury Demo.lua` keeps the existing `scene.bin` default.
- **Downstream / API surface**:
  - New Lua function: `Window.Close()`. Scripts that want to terminate the demo can call it instead of relying on the C++-owned Quit menu item.
  - Engine no longer owns the `File` menu. Any Lua script that doesn't supply a `SetMenuBarCallback` will see *only* the engine's `View` menu in the top bar — Quit moves into the script's responsibility. Demo.lua provides it.
- **Specs**:
  - Delta `openspec/changes/fix-demo-scene-import-and-menu/specs/gltf-importer/spec.md` — adds the lights requirement (ADDED scenarios).
  - Delta `openspec/changes/fix-demo-scene-import-and-menu/specs/scene-editor/spec.md` — modifies the menu-shape requirement (MODIFIED), adds the empty-scene `.json` load scenario, adds the ListDirectory-is-dynamic scenario.
- **Docs**: ARCHITECTURE.md §15 (gltf-importer) gains a half-paragraph noting light translation is now in scope. No README change.
