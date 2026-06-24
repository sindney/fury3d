## Context

The previous scene-editor change shipped Demo.lua's editor menu, glTF importer, FBX-via-glTF importer, and `Importer.MergeInto`. Two regressions appeared in first real use:

- The deferred-Lambert pipeline renders a scene with no lights as black. `GltfImporter` never read glTF's `KHR_lights_punctual` (point/spot/directional light defs that live on `tinygltf::Model::lights` and are referenced from nodes via `node.light`). FBX2glTF can emit these for FBX files that carry lights, so the data is reaching the importer's input — it's just dropped on the floor. Outdoor scene's "fire" point-light disappears, scene is black.
- `Importer.LoadScene("…/scene.json")` fails after `Scene → New`. The current `.json` / `.bin` path in `LuaBindings.cpp:359-380` creates a *new, non-active* `Scene` and calls `FileUtil::LoadFile(scene, path)`. Internally, `MeshRender::Load` resolves the mesh/material name **against `Scene::Active`** (see `MeshRender.cpp:48-55`: `Scene::Manager()` returns `Scene::Active->GetEntityManager()`, `Scene.cpp:21-24`). Right after `New`, the active scene's `EntityManager` was cleared, so `Get<Mesh>("T90")` returns null → `Serialization failed!`.

The user also asked for a UI tidy: collapse the two top-level menus (`File` carrying Quit, `Scene` carrying New/Open/Import/Save As) into a single `File` menu, and confirm Open/Import is dynamic.

Constraints:

- The engine's deferred-Lambert pipeline is the only renderer in v1. The translation from glTF `intensity` (luminous-flux candela/lumen, depending on light type, per KHR_lights_punctual) to the engine's unit-less intensity is lossy. A 1:1 mapping ships now and a "this is unit-less for v1" caveat goes into the importer's log.
- ImGui dedupes top-level menu items by label hash. The previous change deliberately did not call its menu "File" because the engine's built-in menu already used that label. Collapsing into one File menu therefore requires the engine to *stop emitting* its own File block — Lua becomes the single owner.
- No on-disk format change for `scene.json` / `scene.bin`. We're not migrating data; we're fixing the load path.
- FBX2glTF is a binary we vendor (`engine/ThirdParty/FBX2glTF/`). We can pass CLI flags but cannot patch its source.

## Goals / Non-Goals

**Goals:**

- Imported FBX / glTF scenes that contain lights render correctly under the deferred-Lambert pipeline (no black viewport when a source asset clearly has a light).
- `Scene → New → Open → scene.json` succeeds against the bundled `Resource/Scene/scene.json` and produces an equivalent scene to the launch-time `scene.bin` load (same node, mesh, material counts; same render output modulo determinism).
- The top bar shows a single `File` menu with `New / Open ▸ / Import ▸ / Save As… / ─ / Quit`. `Scene` menu is gone.
- `Open` and `Import` submenus continue to populate dynamically from `Resource/Scene/` (no hard-coding) — captured in a spec scenario so it can't silently regress.

**Non-Goals:**

- PBR-correct light units. Intensity is a 1:1 float pass-through in v1 (with a warning in the importer log).
- Shadow-casting state for imported lights. Imported lights are non-shadow-casting (the engine's pipeline already supports shadow toggling per-light via the existing Light component; we just leave the imported defaults at "off").
- A general-purpose "load any Scene into any target" engine refactor. We fix the narrow Lua-binding entry point.
- IES profiles, area lights, or `KHR_lights_image_based`.
- Editing imported lights from the demo UI.

## Decisions

### D1. Translate `KHR_lights_punctual` directly inside `GltfImporter::Import`

Read `tinygltf::Model::lights` once into a vector of pre-converted engine `Light` *prototypes* (type, color, intensity, range, cone angles). When walking nodes, if `node.light >= 0`, clone the prototype, attach it as a component on the emitted `SceneNode`, and log one info line.

**Mapping:**

| glTF | engine | Notes |
|---|---|---|
| `point` | `LightType::POINT` | `range` → engine radius; `0` or unset → engine default (use the existing default constant, currently 10.0 in `Light.cpp` defaults) |
| `spot` | `LightType::SPOT` | `range` → radius; `innerConeAngle` / `outerConeAngle` → engine inner / outer; both in **radians**, no conversion |
| `directional` | `LightType::DIRECTIONAL` | range ignored |
| `color` (linear RGB float[3]) | engine `Color` | direct copy |
| `intensity` (float, lumens for point/spot, lux for directional) | engine intensity | 1:1 pass-through; warning logged once per import |

**Why this over the alternative (parse via a tinygltf extension hook in user code):** tinygltf already populates `Model::lights` and the `Node::light` index when `KHR_lights_punctual` appears in `extensionsUsed`; no extra parsing work. The hook is one C++ block inside the existing node walk.

**Alternative considered:** Wait for an HDR/PBR pipeline before importing lights. Rejected — the user can already author lights in JSON today and have them work; the importer is the missing piece, not the renderer.

### D2. Fix `Scene → New → Open → scene.json` by routing through a `LoadInto(scene, …)` path that resolves entities against the **scene being loaded into**, not `Scene::Active`

Concretely, change the `.json` / `.bin` branch of `Importer.LoadScene` to temporarily set `Scene::Active = imported_scene` for the duration of `FileUtil::LoadFile`, then restore the prior `Scene::Active`. This is **option (1) from the proposal** — surgical, no engine API churn.

**Why over option (2) ("pass the EntityManager through Scene::Load + MeshRender::Load"):** option (2) is the *right* long-term fix but touches `MeshRender::Load`, `Material::Load` if it has a similar lookup, possibly `AnimationClip::Load` and `Joint::Load` — every load-time consumer of `Scene::Manager()`. That's a 5+-file refactor for one bug. Option (1) takes one binding-level patch (3 lines: save, set, restore) and a one-line scenario in `scene-editor` spec pinning the contract. If we later add multi-scene-active support, option (2) becomes mandatory; for v1 single-scene shipping, option (1) is the correct altitude.

**Alternative considered:** Replace `Importer.LoadScene` for `.json` / `.bin` with "load directly into the active scene, no temp scene." Rejected — it would break the symmetry with `LoadGltf` / `LoadFbx` which return a fresh `Scene`, and the existing `Demo.lua` flow (`imported = Importer.LoadScene(p); replace_active_scene(imported)`) would no longer be consistent. Keeping the return-shape uniform across all four LoadScene branches is worth 3 lines.

**Risk:** if anything during `FileUtil::LoadFile` reads `Scene::Active` for a *different* purpose (e.g., a render thread reading the active scene mid-load), the swap is observable. Mitigation: the engine is single-threaded for asset load; render happens in `Engine::Run`'s main loop, not while a Lua callback is mid-execution. No threading concerns in v1.

### D3. Engine stops owning the File menu; Demo.lua becomes the sole File-menu provider

`Gui::ShowDefault` (lines 482-506) removes the `if (ImGui::BeginMenu("File")) { ... }` block and just runs `m_MenuBarCallback` plus the engine's `View` menu. Quit-via-menu becomes a Lua responsibility.

We add a `Window.Close()` Lua binding that invokes the engine's window-close path (the same `m_Window->close()` that the C++ Quit menu item currently triggers). Demo.lua's `File` menu ends with `Separator + MenuItem("Quit") { Window.Close() }`.

**Why this over "engine emits File first, Lua appends to File":** ImGui doesn't expose a clean "extend an already-open menu" API across BeginMainMenuBar/Begin/EndMenu boundaries — you'd have to interleave engine `if (BeginMenu("File"))` with Lua's items inside the *same* BeginMenu block, which means passing the Lua callback into the engine's File-menu code or having two callbacks with implicit ordering. Letting Lua own the entire File menu is simpler, requires one new binding (`Window.Close()`), and keeps the engine's menu code small.

**Alternative considered:** Add a new `Gui::SetFileMenuCallback` so the engine keeps `File ▸ Quit` and Lua injects items above the separator. Rejected — it's a second menu callback in addition to `SetMenuBarCallback`, doubling the surface for one demo. If a future user wants the engine to own Quit, they can re-introduce that callback; not today.

**Ordering note:** ImGui renders menu items in call order. Today the engine renders File then View then Lua's callback. After this change: View first (engine), then Lua's callback (which emits File). The user sees `View | File` left-to-right instead of `File | View`. To preserve the original left-to-right order, **move the Lua callback invocation to before the View block** in `Gui::ShowDefault`.

### D4. FBX2glTF light export

Inspect the vendored FBX2glTF binary's `--help` output. If it has a flag to suppress light export (the default is usually to emit), we leave defaults and they come through. If it doesn't emit lights by default for some build, we explicitly pass the enable flag. Task in `tasks.md` probes this with `./FBX2glTF-darwin-x64 --help | grep -i light` and records the resolved flag.

If FBX2glTF does not emit lights at all for the test FBX (`outdoor.fbx`), fall back to a smaller secondary fix: a Lua-side helper `Importer.AddDefaultLight(scene)` so users can call it after `Importer.LoadFbx`. Document the workflow in the demo. This stays a separate task; the primary path is "lights come through" via D1.

### D5. Dynamic Open/Import list — affirm, don't refactor

`FileUtil.ListDirectory(path, exts)` already calls `std::filesystem::directory_iterator` (LuaBindings.cpp:266-307) and runs every frame from the menu callback. The fix is a one-line **spec scenario** pinning this behavior so a future "let me cache that for perf" refactor can't silently break the contract.

### D6. CLI scene-arg pass-through

`examples/main.cpp` currently consumes `argv[1]` as the Lua script path (or defaults to `Demo.lua`) and discards `argv[2..]`. We change it to populate Lua's standard `arg` table convention: `arg[0]` is the script path, `arg[1..N]` are the remaining `argv` entries. This is the same convention as the `lua` interpreter itself, so scripts written elsewhere drop in cleanly.

Demo.lua's `on_init` inspects `arg[1]`:
- If unset → load `Resource/Scene/scene.bin` (today's default behavior).
- If set → try as a literal path first; if `FileUtil.FileExist` returns false, prepend `Resource/Scene/` and retry. Dispatch through `Importer.LoadScene` (handles `.json`, `.bin`, `.gltf`, `.glb`, `.fbx` per extension).
- If `Importer.LoadScene` returns `nil` → log a warning, fall back to the default `scene.bin`. The demo doesn't refuse to start over a bad argv.

**Why this over a `--scene` flag or a C++ side-channel:** the engine already routes `argv[1]` as the script path; once the script has the rest of `argv`, every script can define its own arg conventions. No engine policy required.

**Alternative considered:** Have the engine parse `argv[1]` as a scene file when its extension is `.fbx/.gltf/.glb/.json/.bin` and `Demo.lua` otherwise. Rejected — it's a special-case in the engine for one demo. Lua-side handling keeps the engine free of asset-extension awareness.

**Path resolution trade-off:** the "literal first, then `Resource/Scene/` prefix" fallback lets users type `./fury Demo.lua outdoor.fbx` (short form) and `./fury Demo.lua /abs/path/foo.fbx` (absolute) both work. Tab-completing a path with `Resource/Scene/outdoor.fbx` also works (it's a literal that exists). The fallback is a Lua-side convenience, not a spec contract — the spec scenario pins absolute and `Resource/Scene/`-relative paths only.

## Risks / Trade-offs

- [glTF intensity is unit-less in v1] → log one warning per import (info-level, mentions "intensity passthrough; PBR units deferred") so a future PBR migration knows where to look.
- [Imported lights have no shadow-casting state — they fall back to engine defaults, which may be off] → spec scenario asserts that *the light is present and visible*; shadows are explicitly non-goal. Imported scenes' shadows are author's-call via JSON edit until the editor exposes per-light UI.
- [`Scene::Active` swap during `FileUtil::LoadFile`] → see D2 risk. Mitigated by single-threaded asset load.
- [Engine no longer renders the Quit menu item] → if a third-party script loads but doesn't provide a `SetMenuBarCallback` that emits Quit, the user can't quit via menu. They can still close the window via the OS chrome (red ✕ button). Acceptable, but a `Window.Close()` binding plus updated Demo.lua keeps Quit available in the shipped script.
- [`Window.Close()` Lua binding shape] → keep it minimal: `Window.Close()` (no args). Future flags (force-close-saving-prompt, etc.) deferred.
- [FBX2glTF may not emit lights for some FBX sources] → D4 fallback (`Importer.AddDefaultLight`) tracked as a secondary task. The primary fix's acceptance is "the outdoor scene's point-light renders", which we verify post-implementation.
