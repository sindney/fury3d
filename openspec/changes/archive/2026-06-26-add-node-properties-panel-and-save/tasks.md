## 1. Vendor ImReflect

- [x] 1.1 Copy `/Users/sindney/Documents/git/furyengine/ImReflect/single_header/ImReflect.hpp` to `engine/ThirdParty/ImReflect/ImReflect.hpp`. Add a top-of-file comment recording the upstream commit hash and copy date.
- [x] 1.2 In `engine/CMakeLists.txt`, add `engine/ThirdParty/ImReflect` to the include path inside the existing `if(WITH_EDITOR)` block. Verify `WITH_EDITOR=OFF` build still configures and links cleanly (smoke).
- [x] 1.3 Smoke-test: include `ImReflect.hpp` from a single editor TU and confirm the editor target compiles. Resolve any header-order / `<imgui.h>` vs `ImGui/imgui.h` issues by adapting only the editor-side include path (do not edit the vendored single header).

## 2. Reflection adapters for engine value types

- [x] 2.1 Create `engine/Fury/Editor/EditorReflect.hpp` (header-only, includes only inside `#ifdef WITH_EDITOR`). Add `tag_invoke` overloads for: `fury::Vector4` (3-component float drag, ignores `w`); `fury::Color` (RGBA color picker, opening on click); `fury::Quaternion` (rendered as Euler XYZ degrees, converts via `MathUtil` helpers).
- [x] 2.2 Add a `tag_invoke` overload for `fury::LightType` that drives an `ImGui::Combo` populated from `EnumUtil::m_LightType` (existing pair-vector). On selection, write back via the captured `Light` setter.
- [x] 2.3 Add a top-of-file convention comment: "Engine public headers must NOT include this file. New adapters belong here, not in `engine/Fury/<Type>.h`."

## 3. Node Properties window

- [x] 3.1 Create `engine/Fury/Editor/EditorNodeProperties.cpp` containing `void RenderNodePropertiesWindow(bool* open)` (forward-declared from `EditorWindows.cpp` style). Include `EditorReflect.hpp`, `Fury/SceneNode.h`, `Fury/Light.h`, `Fury/Transform.h`, `Fury/Scene.h`.
- [x] 3.2 Implement empty-state: when `g_SelectedSceneNode == nullptr` after the dangling-pointer walk, render the placeholder `"(no node selected)"`.
- [x] 3.3 Implement the dangling-pointer walk: if `g_SelectedSceneNode != nullptr` and `Scene::Active != nullptr`, do a recursive search from `Scene::Active->GetRootNode()` to verify the pointer is reachable; if not, set `g_SelectedSceneNode = nullptr` and render the empty state.
- [x] 3.4 Implement `RenderSceneNodeSection(SceneNode* node)`: renders `Name` (read-only text), `Local Position` (Vector4 via the adapter), `Local Rotation` (Quaternion via the Euler-deg adapter), `Local Scale`. After any of the editable fields changes, call `node->Recompose(false)`.
- [x] 3.5 Implement `RenderLightSection(Light* light, SceneNode* owner)`: renders `Type` (LightType combo), `Color` (Color picker), `Intensity` (DragFloat, min 0), `Inner Angle` / `Outer Angle` (in degrees in UI; convert with degrees ↔ radians wrappers), `Falloff`, `Radius`, `Cast Shadows`. After type/inner-angle/outer-angle/radius changes, call `light->CalculateAABB()`.
- [x] 3.6 In `RenderNodePropertiesWindow`, call `ImGui::SetNextWindowSize(ImVec2(320, 480), ImGuiCond_FirstUseEver)`, then `ImGui::Begin("Node Properties", open)`. Render the SceneNode section (always, when a node is selected) and the Light section (only when `node->GetComponent<Light>() != nullptr`). Use `ImGui::CollapsingHeader` for each section, default-open.
- [x] 3.7 Wire the new TU into the engine source list (already automatic if `engine/Fury/Editor/*.cpp` is globbed; otherwise add explicitly).

## 4. Editor surface: Save / current-scene tracking / shortcuts / modals

- [x] 4.1 In `engine/Fury/Editor/Editor.h`, add `std::function<void(const std::string&)> on_save;` to the `SceneIO` struct. Add `void Editor::SetCurrentScene(const std::string&, bool); void Editor::ClearCurrentScene(); std::string Editor::GetCurrentScenePath();` to the public API. Add the `WITH_EDITOR=OFF` no-op stubs.
- [x] 4.2 In `engine/Fury/Editor/Editor.cpp`, add `g_CurrentScenePath` (`std::string`) and `g_CurrentSceneIsNative` (`bool` default false). Implement `SetCurrentScene` / `ClearCurrentScene` / `GetCurrentScenePath`. Wire `Shutdown` and `ClearSceneIO` so they don't keep stale state.
- [x] 4.3 In `Editor.cpp`, add `g_ShowNodeProperties = true;` and extend `SetWindowVisible` / `GetWindowVisible` to recognize the name `"NodeProperties"`. Forward-declare `RenderNodePropertiesWindow` and dispatch in `Tick` mirroring the existing windows.
- [x] 4.4 Modify `BuildDefaultLayout` to add a RIGHT split before the BOTTOM split, and `DockBuilderDockWindow("Node Properties", right)`. Verify Reset Layout reproduces the right region.
- [x] 4.5 Modify `RenderMenuBar`'s File menu to emit, in order: `New (Ctrl+N)` → `Open ▸` → `Open… (Ctrl+O)` → `Import ▸` → `Import… (Ctrl+Shift+I)` → `Save (Ctrl+S)` → `Save As… (Ctrl+Shift+S)` → separator → `Settings` → separator → `Quit (Ctrl+Q)`. Pass platform-appropriate shortcut text via the third arg to `ImGui::MenuItem`. Use `ImGui::GetIO().ConfigMacOSXBehaviors` to choose `Cmd+` vs `Ctrl+` display strings.
- [x] 4.6 Add `g_ShowNodeProperties` toggle to the Window menu, ordered between `Scene Inspector` and `Console`. Update Reset Layout to leave `g_ShowNodeProperties = true` (it's a default-on window).
- [x] 4.7 Implement `File → Save` logic: if `g_CurrentSceneIsNative && !g_CurrentScenePath.empty() && g_SceneIO.on_save` → invoke `on_save(g_CurrentScenePath)`; else open Save As modal (set `g_SaveAsModalOpen = true`). Disable the menu item when `Scene::Active == nullptr`.
- [x] 4.8 Implement Open / Import modals: add `g_OpenModalOpen` / `g_ImportModalOpen` flags, render via a `RenderOpenImportModal(const char* title, std::function<void(const std::string&)> on_pick)` helper that draws the directory header, the scrollable selectable list (height ~240px), Open/Cancel buttons, and Esc/double-click confirms. Both modals reuse `g_SceneIO.list_files()`. Local-static selected index, reset on each `OpenPopup`.
- [x] 4.9 At the top of `Editor::Tick` (before menu bar, dockspace, modals), check shortcuts via `ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)` etc. Match the dispatch table from the spec — `Ctrl+N` → trigger New action; `Ctrl+O` / `Ctrl+Shift+I` → set the corresponding modal-open flag; `Ctrl+S` → trigger Save logic; `Ctrl+Shift+S` → set `g_SaveAsModalOpen = true`; `Ctrl+Q` → `Gui::CloseWindow()`.
- [x] 4.10 Ensure New (both menu and shortcut) calls `ClearCurrentScene()` and clears `g_SelectedSceneNode`.

## 5. Lua bindings

- [x] 5.1 In `engine/Fury/LuaBindings.cpp`, extend the `Editor.SetSceneIO` table-handler to read an additional optional `on_save` field; bind it to `Editor::SceneIO::on_save`.
- [x] 5.2 Bind `Editor.SetCurrentScene(string, bool)` and `Editor.ClearCurrentScene()` to the new C++ functions.
- [x] 5.3 Confirm the `WITH_EDITOR=OFF` no-op path in the Lua bindings still no-ops (the new functions become no-op stubs).

## 6. Editor.lua wiring

- [x] 6.1 In `examples/Editor.lua`, add a small helper `local function is_native(filename) return filename:lower():match("%.json$") or filename:lower():match("%.bin$") end`.
- [x] 6.2 Modify `open_scene` to call `Editor.SetCurrentScene(full, is_native(filename) ~= nil)` after a successful open. On failure, leave the previous tracked scene alone (don't clobber).
- [x] 6.3 Modify `save_active_scene` to call `Editor.SetCurrentScene(full, true)` after a successful save (Save As writes only `.json` / `.bin`, both native).
- [x] 6.4 Modify `on_new` to call `Editor.ClearCurrentScene()` (or `Editor.SetCurrentScene("", false)`).
- [x] 6.5 Add an `on_save` callback to the `Editor.SetSceneIO` table that picks `FileUtil.SaveFile` vs `FileUtil.SaveCompressedFile` based on the path's extension and writes `Scene.GetActive()` to the given absolute path. Add an Editor.Log info line on success / error.
- [x] 6.6 Apply the same path-tracking to the startup-scene resolution branch (`resolve_startup_scene` success path) and the `load_default_scene` fallback.

## 7. Manual / runtime verification

- [x] 7.1 Build with `WITH_EDITOR=ON` (default) and `cmake --build`. Confirm no compile errors.
- [x] 7.2 Build with `WITH_EDITOR=OFF` and confirm: configures cleanly, builds, no link errors involving `Editor::*`, no engine TU pulled in `ImReflect.hpp`.
- [x] 7.3 Run the engine: confirm Node Properties window appears docked on the right by default. Confirm Scene Inspector → click a node → properties appear; edit position → viewport reflects the change.
- [x] 7.4 Run with `Resource/Scene/scene.bin`: edit light intensity / color → confirm the rendered scene updates immediately. Press `Cmd+S` (macOS) → file is overwritten in place. Re-open → edits persisted.
- [x] 7.5 Run with an imported `.fbx` scene: press `Cmd+S` → Save As modal opens (no silent overwrite). Save as `.json` → confirm new file appears in Content Browser.
- [x] 7.6 Verify shortcuts: `Cmd+N` clears scene; `Cmd+O` opens Open modal listing files; `Cmd+Shift+I` opens Import modal; `Cmd+Shift+S` opens Save As modal; `Cmd+Q` closes the engine window.
- [x] 7.7 Verify menu shortcut text: open `File` menu on macOS, confirm `Cmd+S`, `Cmd+Shift+S`, `Cmd+O`, `Cmd+Shift+I`, `Cmd+N`, `Cmd+Q` are right-aligned. (On Windows / Linux: `Ctrl+...`.)
- [x] 7.8 Verify dangling-selection: open scene, click a node, then `File → New`. Node Properties shows `(no node selected)` and `Editor::GetSelectedSceneNode()` returns null.
- [x] 7.9 Verify Reset Layout: undock Node Properties → click `Window → Reset Layout` → re-docks on the right with the other defaults.
