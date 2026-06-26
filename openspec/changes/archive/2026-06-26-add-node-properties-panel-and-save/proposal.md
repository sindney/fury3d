## Why

The editor currently lets users select scene nodes but offers no way to view or edit their properties — selecting a node just highlights it. The Save As… modal exists, but there's no plain Save: every write asks the user to retype a filename. And the menu items have no keyboard shortcuts, so even basic actions like saving the current scene need a mouse trip through `File`. This change closes those gaps so the editor is genuinely usable for tweaking scenes, not just inspecting them.

## What Changes

- Vendor the [ImReflect](https://github.com/Sven-vh/ImReflect) single header at `engine/ThirdParty/ImReflect/ImReflect.hpp`. Compiled into the editor only (gated by `WITH_EDITOR`); engine public headers stay untouched.
- Add a new editor-owned **Node Properties** window (default-docked to the right side of the dockspace) that renders editable widgets for the currently-selected scene node and its components. v1 reflects:
  - **SceneNode**: name (read-only), local position (Vector4), local rotation (Quaternion shown as Euler degrees), local scale (Vector4)
  - **Light component** (when present): type (LightType enum dropdown), color (color picker, RGBA), intensity, inner / outer angle (radians stored, degrees in UI), falloff, radius, cast shadows
- Edits mutate the live scene immediately (in-memory). Position/rotation/scale edits call `SceneNode::Recompose`; light geometry edits call `Light::CalculateAABB`. No serialization happens until Save / Save As runs.
- Add **File → Save** that writes the in-memory scene back to the file last opened or saved. Save is enabled only when the current scene is in a writable native format (`.json` / `.bin`); when the active scene came from a non-native source (`.gltf` / `.glb` / `.fbx`) or was never opened, Save falls through to the Save As modal so the user picks a destination.
- Track the "current scene path + native flag" inside the editor. Extend `Editor::SceneIO` with a new `on_save(path)` callback and add `Editor::SetCurrentScene(path, is_native)`. Editor.lua updates it after each successful open / save_as / new.
- Add keyboard shortcuts (cross-platform via `ImGuiMod_Ctrl` — automatically Cmd on macOS, Ctrl elsewhere) and display them in the menu items via `ImGui::MenuItem(label, shortcut, …)`:
  - `Ctrl+N` → File → New
  - `Ctrl+O` → File → Open… — opens a new modal listing the same files the Open submenu already shows; user picks one and clicks Open (or Cancel)
  - `Ctrl+Shift+I` → File → Import… — same modal shape as Open, dispatched to `on_import`
  - `Ctrl+S` → File → Save (in-place, native formats only)
  - `Ctrl+Shift+S` → File → Save As…
  - `Ctrl+Q` → File → Quit (only fires while the engine window is focused; the OS keeps owning global Cmd+Q)
- Default dock layout gains a **right** region (~20% of width) that hosts the Node Properties window. Reset Layout reproduces it. The window is visible by default, toggleable via `Window → Node Properties`. `Editor::SetWindowVisible("NodeProperties", …)` is added to the Lua surface.

## Capabilities

### New Capabilities
<!-- None — this change extends existing editor-shell capability rather than introducing a new domain. -->

### Modified Capabilities
- `editor-shell`: adds the Node Properties window + reflection rendering, the File → Save menu item, the current-scene tracking on `Editor::SceneIO`, the keyboard-shortcut bindings, and the Open / Import modals; modifies the default dock layout to include the right region; modifies the `Window` menu to include the Node Properties toggle.

## Impact

- **New vendored dependency**: `engine/ThirdParty/ImReflect/ImReflect.hpp` (single header, MIT). Header-only, no link step. Bundled `magic_enum` and `visit_struct` come along inside the single header.
- **Engine source — unchanged public API**: `engine/Fury/Light.h`, `engine/Fury/SceneNode.h`, `engine/Fury/Color.h`, `engine/Fury/Vector4.h`, `engine/Fury/Quaternion.h`, `engine/Fury/EnumUtil.h` are NOT modified. The reflection adapters (`tag_invoke` overloads + `IMGUI_REFLECT` macros) live entirely inside `engine/Fury/Editor/` and only compile when `WITH_EDITOR=ON`.
- **Editor source — new files**: `engine/Fury/Editor/EditorReflect.hpp` (tag_invoke adapters for `Vector4`, `Quaternion`, `Color`, `LightType`), `engine/Fury/Editor/EditorNodeProperties.cpp` (the panel + per-component renderers).
- **Editor source — modified files**: `engine/Fury/Editor/Editor.h` (new `SceneIO::on_save`, `SetCurrentScene`, `Editor::Save()` for menu binding), `engine/Fury/Editor/Editor.cpp` (menu shortcuts, keyboard handler, Open/Import modals, current-scene tracking, dock-layout right region, Node Properties window flag), `engine/Fury/Editor/EditorWindows.cpp` (Node Properties window forward-decl + dispatch), `engine/Fury/LuaBindings.cpp` (`Editor.SetCurrentScene`, `on_save` field on the SceneIO table).
- **CMake**: `engine/CMakeLists.txt` adds `engine/ThirdParty/ImReflect` to the editor-only include list (under the existing `if(WITH_EDITOR)` guard). `WITH_EDITOR=OFF` builds remain identical.
- **Lua surface**: `Editor.SetSceneIO(...)` accepts a new optional `on_save` field. `Editor.SetCurrentScene(path, is_native)` is a new function. `Editor.lua` updates: `open_scene` / `save_active_scene` / `on_new` call `Editor.SetCurrentScene` so the C++ side knows whether Save is in-place or modal-based. A new `on_save` callback writes the active scene to the tracked path.
- **Behavior on imported FBX/GLTF**: identical to today's Save As — the user is prompted for a `.json` / `.bin` filename. No silent overwrite of imported assets.
- **Migration**: existing `imgui.ini` files are tolerated; if no `Node Properties` dock entry is present, Reset Layout (or the first-run default-layout build path) re-docks it on the right.
