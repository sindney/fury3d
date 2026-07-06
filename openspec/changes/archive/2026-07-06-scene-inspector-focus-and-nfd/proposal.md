## Why

The Scene Inspector's double-click is wired to in-place rename, which is the wrong default: rename is already available via the context menu and the F2 key, and double-clicking a node is the natural way to "dig into" it. Users expect double-click on a parent to expand/collapse and double-click on a leaf to bring the camera to that node — the way most DCC tools and level editors behave. Separately, the editor has no native OS file picker today (save/open dialogs are ImGui-implemented modals), so bringing in `nativefiledialog-extended` gives the asset browser, scene save-as, and scene-open flows a proper platform-native dialog with no per-platform code on our side.

## What Changes

- **Scene Inspector double-click no longer activates rename.** Rename remains available via the row's context menu ("Rename") and the F2 keyboard shortcut while the inspector has focus.
- **Double-click on a parent node with children toggles its expand/collapse state.** (ImGui's `ImGuiTreeNodeFlags_OpenOnDoubleClick` already does this; the change is making sure the row's double-click handler does not also fire a rename.)
- **Double-click on a leaf node frames that node in the viewport:**
  - If the node has a `MeshRender` component and a valid `WorldAABB`, the camera frames the AABB (eye placed along a sensible diagonal at a distance that fits the bounds in view, "close-up look" feel).
  - If the node is not renderable (no `MeshRender` or invalid AABB), the camera just looks at the node's `GetWorldPosition()`.
- **New C++ ↔ Lua bridge for camera framing.** Because the editor camera (yaw/pitch/cam_pos + cam_node) is owned by `Editor.lua`, C++ cannot safely write the camera transform directly — the next `on_update` would overwrite it with stale state. Instead, a new `Editor.SetFrameSelectionHandler(fn)` Lua binding lets the script register a framer; the C++ inspector invokes it on a leaf double-click and the Lua side repositions `cam_pos`/`yaw`/`pitch` so the next `on_update` applies the new view.
- **`nativefiledialog-extended` is integrated as a third-party dependency** under `engine/ThirdParty/nfd/` as a git submodule, built via `add_subdirectory()` (matching the SFML pattern), and linked statically into `fury` via the `nfd::nfd` alias target. Default `BUILD_SHARED_LIBS=OFF` is preserved.
- **Lua-facing file-dialog API** (`Editor.OpenFileDialog`, `Editor.SaveFileDialog`) is exposed so `Editor.lua` can replace the ImGui save-as modal with a native dialog without per-platform branching. Initial wiring replaces the Save-As modal flow; the Content Browser's import flow gains a native multi-select picker.

## Capabilities

### New Capabilities
- `editor-frame-selection`: C++ `Editor::SetFrameSelectionHandler` API and Lua binding that lets `Editor.lua` register a function which positions the editor camera to frame a given `SceneNode`. Decouples inspector-driven focus requests from the Lua-owned camera state.
- `native-file-dialog`: Integration of `nativefiledialog-extended` as a vendored submodule under `engine/ThirdParty/nfd/`, statically linked, with Lua-callable `Editor.OpenFileDialog` / `Editor.SaveDialog` bindings.

### Modified Capabilities
- `scene-inspector-node-operations`: The rename trigger requirement changes from "context menu, double-click, or F2" to "context menu or F2". A new requirement covers double-click behavior: parents toggle expand/collapse; leaf nodes request a camera frame on the node (renderable → AABB, otherwise → world position).

## Impact

- **Code**:
  - `engine/Fury/Editor/EditorWindows.cpp` (`RenderNodeRow`): drop the double-click → `DoRenameActivate` call; add a leaf-double-click branch that invokes the registered frame-selection handler.
  - `engine/Fury/Editor/Editor.h` / `Editor.cpp`: add `SetFrameSelectionHandler` / `FrameSelection` (and a `WITH_EDITOR=OFF` stub).
  - `engine/Fury/LuaBindings.cpp`: bind `Editor.SetFrameSelectionHandler` (and the new file-dialog functions) to Lua.
  - `examples/Editor.lua`: register a frame-selection handler that reads the node's `WorldAABB` or `GetWorldPosition()` and updates `cam_pos`/`yaw`/`pitch`; switch Save-As modal to `Editor.SaveDialog`; switch Content Browser import trigger to `Editor.OpenDialog`.
- **Dependencies / build**:
  - New git submodule `engine/ThirdParty/nfd` pointing at `https://github.com/btzy/nativefiledialog-extended.git`.
  - `engine/CMakeLists.txt`: `add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/nfd)` + `target_link_libraries(fury PRIVATE nfd::nfd)`, gated on `WITH_EDITOR` (the file dialog is editor-only). Submodule-presence check added at configure time.
  - nfd's macOS path links `AppKit` (and optionally `UniformTypeIdentifiers`) — handled by nfd's own CMake; we don't touch frameworks directly.
- **APIs**: New `Editor.SetFrameSelectionHandler`, `Editor.OpenDialog`, `Editor.SaveDialog` Lua bindings. Existing rename via F2 / context menu unchanged.
- **Specs**: `scene-inspector-node-operations` modified; two new specs added.
