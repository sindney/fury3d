## Context

The editor shell shipped in `2026-06-25-upgrade-imgui-and-add-editor` introduced the dockspace, the four built-in windows (Settings / Profiler / Scene Inspector / Console / Content Browser), the `Editor::*` C++ surface, and the Lua extension hooks (`Editor.SetSceneIO`, `Editor.SetSceneTreeProvider`, etc.). Selection state already lives on the C++ side (`g_SelectedSceneNode`), and the Save As… modal already exists.

What's missing for "actually editing scenes":

- Selecting a node in Scene Inspector has no visible effect beyond highlighting — there's no property panel.
- File → Save (in-place) doesn't exist — every write is Save As… with a fresh modal prompt.
- Menu items have no shortcuts and no shortcut hint text — no Cmd+S / Cmd+O / Cmd+Q.

The user prefers ImReflect (Sven-vh, MIT) for the property-panel rendering: it's a single header, builds widgets from struct definitions via `IMGUI_REFLECT(Type, fields…)` plus extension via `tag_invoke` for non-trivial types. The library lives at `/Users/sindney/Documents/git/furyengine/ImReflect`. Its single header is `single_header/ImReflect.hpp`.

The engine's editor is gated by `WITH_EDITOR` (CMake option, ON by default). Engine public headers (`Light.h`, `SceneNode.h`, etc.) must NOT take a hard dependency on ImReflect — `WITH_EDITOR=OFF` builds and headless library consumers must remain identical. All reflection code therefore lives inside `engine/Fury/Editor/`.

Scene serialization is already format-aware: `FileUtil::SaveFile(scene, path)` writes JSON (rapidjson) and `FileUtil::SaveCompressedFile(scene, path)` writes the engine's binary format. There is no save-back path for `.gltf` / `.fbx` — those are import-only formats.

## Goals / Non-Goals

**Goals:**
- Render an editable property panel for the selected SceneNode and its Light component using ImReflect.
- Edits commit immediately to the live scene's in-memory state. No deferred apply / undo stack — that's a follow-up.
- File → Save (Cmd+S) overwrites the file the user is currently editing, but only when that file is a writable native format (`.json` / `.bin`). Imported `.gltf` / `.fbx` scenes route Save through Save As so the user explicitly picks a `.json` / `.bin` destination.
- Menu items display their keyboard shortcuts and the shortcuts actually fire while the engine window is focused.
- Keep engine public headers free of ImReflect.

**Non-Goals:**
- Property reflection for Camera, MeshRender, AnimationPlayer, Joint, or other components. Scoped to SceneNode + Light in v1; the architecture leaves room to grow.
- Reflection of arbitrary user / Lua-defined components.
- Undo / redo, multi-select, copy / paste of properties.
- A persistent "dirty" indicator in the title bar.
- File→Save creating directories or sanitizing user-typed paths beyond what Save As… already does.
- Replacing the Open / Import submenus with the new modal — the submenus stay; the modal is what the keyboard shortcut opens.
- A native OS file dialog (we stay in-ImGui to keep dependencies frozen).
- Saving while the engine window is unfocused (Cmd+S routes to whoever has focus — that's the OS contract, not ours).

## Decisions

### Decision 1: Vendor ImReflect single header at `engine/ThirdParty/ImReflect/ImReflect.hpp`

Copy the single header from `/Users/sindney/Documents/git/furyengine/ImReflect/single_header/ImReflect.hpp` into `engine/ThirdParty/ImReflect/ImReflect.hpp`. Add the include path under `if(WITH_EDITOR)` in `engine/CMakeLists.txt`. Track the library version via a top-of-file comment in the vendored copy (commit hash + date).

**Why:** The single header is self-contained (bundles `magic_enum` and `visit_struct`); ImGui is the only external dep, which we already vendor. No build-system changes beyond an include path.

**Alternatives considered:**
- *Git submodule pointing at the upstream repo* — rejected: fragile across fresh clones, every contributor needs a recursive clone.
- *Pull just the headers we need* — rejected: the single header IS the supported distribution; pulling the multi-header form means tracking 7 files instead of 1.

### Decision 2: Reflection adapters live in editor TUs only — engine headers untouched

Create `engine/Fury/Editor/EditorReflect.hpp` containing `tag_invoke` overloads for engine value types: `Vector4`, `Quaternion` (rendered as Euler degrees), `Color`. Create `engine/Fury/Editor/EditorNodeProperties.cpp` containing the per-component renderers for `SceneNode` and `Light`, plus the window dispatch (`RenderNodePropertiesWindow`). Both files include `ImReflect.hpp` and the relevant engine headers — but no engine header includes either of them.

For `Light` (a 9-field component) and `SceneNode` (4 logical "transform" fields), use direct ImGui calls via `tag_invoke` overloads rather than `IMGUI_REFLECT` macros — the macros require modifying the type definition, which we explicitly want to avoid. The renderer functions are simple enough that the manual approach is shorter than introducing a parallel "facade struct" reflected via `IMGUI_REFLECT`.

For `LightType` (an `enum class`), use `magic_enum`-backed reflection via ImReflect's built-in enum support: define a `tag_invoke` overload that drives an `ImGui::Combo` with the names from `EnumUtil::m_LightType` (already exists).

**Why:** Honors the WITH_EDITOR=OFF build identity. Concentrates UI knowledge in the editor module, where it belongs. Keeps the upgrade path open for future component reflection (just add another `RenderXxxProperties` function).

**Alternatives considered:**
- *`IMGUI_REFLECT(Light, m_Color, m_Intensity, …)` inside `Light.h`* — rejected: `m_*` are private; would need either friend declarations or `#ifdef WITH_EDITOR` blocks inside engine public headers, both of which introduce coupling.
- *A facade struct (`LightProps`) inside the editor that mirrors `Light` and gets reflected via `IMGUI_REFLECT`* — rejected: doubles the field list, requires a sync step every frame, and the manual `tag_invoke` is shorter.

### Decision 3: Quaternion edits use Euler degrees as the UI representation, not raw quaternions

The `tag_invoke` overload for `Quaternion` converts to Euler XYZ in degrees on read, edits the three floats with `ImGui::DragFloat3("...", deg, 0.5f)`, and converts back via `MathUtil::EulerRadToQuat(rad.x, rad.y, rad.z)` on write. The internal Euler representation is rebuilt from the quaternion every frame so multiple edit sessions remain consistent.

**Why:** Direct quaternion editing (xyzw) is unintuitive and produces denormalized results. Euler is what a 3D content creator expects.

**Trade-off:** Euler is ambiguous near gimbal lock — the displayed degrees can flip after a round trip. Acceptable for v1; the underlying quaternion is the source of truth so the scene transform itself doesn't drift.

### Decision 4: Track current scene path + native flag in `Editor`, not in Lua

Add to `Editor::SceneIO`:
- `std::function<void(const std::string&)> on_save;` — invoked by File → Save when an in-place save is allowed.

Add to `Editor` namespace:
- `void Editor::SetCurrentScene(const std::string& path, bool is_native);` — called from Lua after every successful open / save_as. `path` is informational (shown in window title later); `is_native` decides whether Cmd+S is in-place or modal.
- `Editor::ClearCurrentScene()` — called from `on_new`.

Inside `Editor.cpp`:
- `std::string g_CurrentScenePath;`
- `bool g_CurrentSceneIsNative = false;`

`File → Save` menu logic:
1. If `g_CurrentSceneIsNative && g_SceneIO.on_save && !g_CurrentScenePath.empty()` — call `on_save(g_CurrentScenePath)`.
2. Else — open the Save As modal (set `g_SaveAsModalOpen = true`).

The Save menu item is greyed out when there's no active scene (`Scene::Active == nullptr`) but is selectable in the non-native case (it just routes to Save As — that's still useful UX, since the user can still save the imported scene out as JSON).

**Why:** C++ already owns `g_SelectedSceneNode`, the dock layout, and the Save As modal. Putting current-scene state next to those keeps the menu's state-machine readable.

**Alternatives considered:**
- *Lua owns it* — rejected: would force the Save As modal logic to also live in Lua, scattering UI state.
- *Auto-detect native via extension at click time* — possible but redundant: Lua already knows the native flag at open time.

### Decision 5: Keyboard shortcuts use `ImGui::Shortcut` + `ImGuiMod_Ctrl` (auto-Cmd on macOS)

ImGui 1.92.8 (already vendored) treats `ImGuiMod_Ctrl` as Cmd on macOS at runtime as long as `ImGui::GetIO().ConfigMacOSXBehaviors` is true (default true on Apple). The shortcut hint in `ImGui::MenuItem(label, shortcut, …)` is a display-only string — it doesn't actually fire the shortcut. The actual binding uses `ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)` checked once per frame.

We display shortcut text using the platform-appropriate prefix:
- macOS: `"Cmd+S"`, `"Cmd+Shift+S"`, etc.
- Windows / Linux: `"Ctrl+S"`, `"Ctrl+Shift+S"`, etc.

Selected at runtime via `ImGui::GetIO().ConfigMacOSXBehaviors`.

The dispatch table inside `RenderMenuBar` is:

| Shortcut | Action |
| --- | --- |
| `Ctrl+N` | Trigger File → New |
| `Ctrl+O` | Open the Open modal |
| `Ctrl+Shift+I` | Open the Import modal |
| `Ctrl+S` | Trigger File → Save (with the native-vs-Save-As fallthrough above) |
| `Ctrl+Shift+S` | Open the Save As modal |
| `Ctrl+Q` | `Gui::CloseWindow()` |

Shortcut routing happens unconditionally at the top of `Editor::Tick` (before any menu / modal renders) so it works whether the menu is open or not. `ImGuiInputFlags_RouteGlobal` ensures the shortcut still fires when no editor window has focus — the engine viewport behind the dockspace counts as global.

**Why:** ImGui's built-in shortcut system, used everywhere upstream, integrates with focus routing and avoids a hand-rolled key check.

**Risks:**
- Some macOS users have remapped Cmd+Q at the OS level. We don't intercept it; the OS quits the app first. This is acceptable — `Editor::Quit` was always a courtesy, the OS path remains.
- Cmd+S inside a text input (Console input, Save As filename field) is captured by ImGui's input field. This is intentional — text input takes precedence. Save still works from the menu.

### Decision 6: Open / Import modals reuse the file list from the existing submenu

Both modals render a `ImGui::Selectable`-based list inside an `ImGui::BeginChild` (scrollable, fixed height ~240px) populated by the same `g_SceneIO.list_files()` callback the submenus already use. A double-click on a file confirms; an `Open` button at the bottom confirms the highlighted file; `Cancel` dismisses. The modal title is `"Open Scene"` or `"Import Scene"`.

The modal's "highlighted" state is local-static (`static int s_OpenSelectedIndex`). Reset to 0 on each `OpenPopup`.

**Why:** Consistent with how Save As already works (modal + InputText). Adding a real file picker would mean adding a native dialog dependency — out of scope.

### Decision 7: Default dock layout adds a right region; existing imgui.ini files are tolerated

`BuildDefaultLayout` gains:
```cpp
ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.20f, nullptr, &center);
ImGui::DockBuilderDockWindow("Node Properties", right);
```

The split happens AFTER the left split and BEFORE the bottom split, so the bottom region spans the central area but not the left or right panels (matching Unity / Godot conventions).

Reset Layout (already in the Window menu) re-runs `BuildDefaultLayout`, which re-creates the right region and re-docks Node Properties.

For users with an existing `imgui.ini` from the prior change (no Node Properties dock entry), the window opens as a floating panel until the user docks it manually or hits Reset Layout. We don't auto-rebuild the layout for existing users — that would silently destroy their custom dock arrangement.

**Why:** Matches the existing layout-rebuild contract from `editor-shell` (rebuild only on first run with no ini, or on explicit Reset Layout).

### Decision 8: Property panel handles "selected node was destroyed" gracefully

`g_SelectedSceneNode` is a raw pointer; it's nulled out when:
- The user clicks `File → New` (handled today).
- The selected node is removed from the scene tree mid-frame.

Property panel safety strategy: each frame, if `g_SelectedSceneNode != nullptr`, the panel walks the scene tree from `Scene::Active->GetRootNode()` to confirm the pointer is still reachable. If not, null it out and render the empty-state. This is O(N) per frame but N is small (scenes are < 1000 nodes).

**Why:** Cheap; avoids changing `g_SelectedSceneNode` to a `weak_ptr` (which would ripple into `Editor::GetSelectedSceneNode`'s public signature).

**Alternative considered:** Subscribe to scene-mutation signals — rejected as overkill. The walk is O(N) once per frame inside an editor-only window.

## Risks / Trade-offs

- **[Risk]** ImReflect's `IMGUI_REFLECT` macro uses `visit_struct` which requires public field access. We sidestep this entirely by writing `tag_invoke` overloads for our types — but new contributors might be tempted to apply the macro to engine types. → Mitigation: a one-paragraph note in `EditorReflect.hpp` explaining the convention.
- **[Risk]** Editing transform values while the camera is animating produces visible jitter. → Mitigation: documented behavior (Cmd+S commits state, edits are live by design). The user's mental model already handles this — Unity behaves the same way.
- **[Risk]** macOS Cmd+S inside the Save As modal's filename field doesn't trigger the modal's Save button — ImGui captures the keystroke for the InputText. → Acceptable: the user can press Enter or click Save. This is consistent with macOS HIG (text fields capture text shortcuts).
- **[Risk]** ImReflect single-header recompile cost (it transitively pulls `magic_enum` + `visit_struct`) inflates build time for the editor TUs that include it. → Mitigation: only `EditorReflect.hpp` and `EditorNodeProperties.cpp` include it; other editor TUs don't.
- **[Trade-off]** Euler-degree quaternion editing introduces gimbal-lock ambiguity, but raw xyzw editing is unusable. We accept the trade.
- **[Trade-off]** No "dirty" indicator means the user might Quit without saving and lose work. v1 doesn't address this; the Quit menu item could later prompt-on-dirty. Acceptable for now since edits-then-quit-without-save is a power-user mistake, not a default workflow.

## Migration Plan

1. Vendor the single header (one file copy).
2. Add the include path to CMake (one line, gated by `WITH_EDITOR`).
3. Land the new TUs and Editor.cpp / Editor.h changes together — no intermediate broken state because the new menu items / shortcuts / window all flip on at once.
4. Update `Editor.lua` to call `Editor.SetCurrentScene` after each open / save_as / new and to register `on_save`. Existing user scripts that don't update will: keep working, lose the "Save in place" affordance (Cmd+S falls through to Save As since `g_CurrentScenePath` stays empty), and not crash.
5. Existing `imgui.ini` files: tolerated. New `Node Properties` window opens floating; `Window → Reset Layout` re-docks it on the right.
6. Rollback: revert the change set — engine headers untouched, no lingering state.
