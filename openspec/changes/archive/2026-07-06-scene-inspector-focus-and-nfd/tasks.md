## 1. Vendor `nativefiledialog-extended`

- [x] 1.1 Add the git submodule `engine/ThirdParty/nfd` pointing at `https://github.com/btzy/nativefiledialog-extended.git` (use the same commit as the local checkout at `/Users/sindney/Documents/git/furyengine/nativefiledialog-extended`, currently `3cd252a8f7ca32419b1ca235c2990ba6a0ecba7c`).
- [x] 1.2 Verify the `.gitmodules` entry for `engine/ThirdParty/nfd` was added and that `git submodule update --init --recursive` populates `engine/ThirdParty/nfd/CMakeLists.txt`, `src/`, `include/`, `LICENSE`.

## 2. CMake integration of nfd

- [x] 2.1 In `engine/CMakeLists.txt`, add a configure-time `if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/nfd/CMakeLists.txt") message(FATAL_ERROR "nfd submodule missing — run: git submodule update --init --recursive")` check, gated on `WITH_EDITOR`.
- [x] 2.2 Add an `if(WITH_EDITOR)` block that forces `NFD_BUILD_TESTS`, `NFD_BUILD_SDL2_TESTS`, `NFD_BUILD_GLFW3_TESTS`, and `NFD_INSTALL` to `OFF` in the cache, then calls `add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/nfd)`.
- [x] 2.3 Add `nfd::nfd` to both `target_link_libraries(fury PRIVATE ...)` lines (the `BUILD_SHARED_LIBS` branch and the default-executable branch), gated on `WITH_EDITOR` so non-editor builds do not link it.
- [x] 2.4 Verify a clean configure + build with `WITH_EDITOR=ON` produces `libnfd.a` and links it into `fury` (no `libnfd.dylib` / `nfd.dll`).
- [x] 2.5 Verify a clean configure + build with `WITH_EDITOR=OFF` does not invoke `add_subdirectory` for nfd and does not link it.

## 3. C++ frame-selection API

- [x] 3.1 In `engine/Fury/Editor/Editor.h`, declare `void FURY_API SetFrameSelectionHandler(std::function<void(SceneNode*)> handler);` and `void FURY_API FrameSelection(SceneNode* node);` inside the `WITH_EDITOR` block, plus storage for the handler (e.g., a file-static `std::function<void(SceneNode*)> g_FrameSelectionHandler;`).
- [x] 3.2 In `engine/Fury/Editor/Editor.cpp`, implement `SetFrameSelectionHandler` (assigns the file-static, replacing any prior) and `FrameSelection` (null-checks `node` and the handler, then invokes).
- [x] 3.3 Add the matching no-op inline stubs in the `#else` (WITH_EDITOR=OFF) block of `Editor.h`: `inline void SetFrameSelectionHandler(std::function<void(SceneNode*)>) {}` and `inline void FrameSelection(SceneNode*) {}`. Make sure existing call sites in `EditorWindows.cpp` compile under both build modes.

## 4. C++ Scene Inspector double-click change

- [x] 4.1 In `engine/Fury/Editor/EditorWindows.cpp` `RenderNodeRow`, remove the `if (node && !isRoot && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { DoRenameActivate(node); }` block at lines 670–672.
- [x] 4.2 In the same function, add a new branch: if the row is a leaf (already computed as `isLeaf`) and not the root, and the row was double-clicked (`ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)`), call `Editor::FrameSelection(node)`.
- [x] 4.3 Verify the existing `ImGuiTreeNodeFlags_OpenOnDoubleClick` flag remains on the row's flags so parent rows still expand/collapse on double-click (no change needed; just confirm).

**4.4 (post-apply revision)** Double-click behavior was tightened per feedback: now `!isLeaf` is also dropped from the double-click branch, so double-clicking *any* non-root row frames the camera. For parents, the expand/collapse is toggled manually by flipping the ImGui storage entry for the row's id (the next frame's `TreeNodeEx` picks up the new state and the children appear). `ImGuiTreeNodeFlags_OpenOnDoubleClick` was removed from the row's flags because the flag's press detection races with our own click check in this layout — manual storage flipping is reliable, version-agnostic, and works for both parent and leaf rows. Spec updated accordingly.

## 5. Lua binding: `Editor.SetFrameSelectionHandler`

- [x] 5.1 In `engine/Fury/LuaBindings.cpp` (inside the `WITH_EDITOR` block of the `Editor` table bindings), bind `editor_tbl["SetFrameSelectionHandler"] = [](sol::object obj) { ... }`. If `obj` is a Lua function, wrap it in `sol::protected_function` and forward to `Editor::SetFrameSelectionHandler` with an error-trapping lambda (mirror the `SetCommandHandler` pattern at lines 771–782). If `obj` is `nil`, call `Editor::SetFrameSelectionHandler(nullptr)`.
- [x] 5.2 In the `#else` (WITH_EDITOR=OFF) block, add the no-op stub: `editor_tbl["SetFrameSelectionHandler"] = [](sol::object) {};`.

## 6. Lua binding: `Editor.OpenDialog` / `Editor.SaveDialog`

- [x] 6.1 In `engine/Fury/LuaBindings.cpp`, include `nfd.h` (gated under `#ifdef WITH_EDITOR`) so the bindings can call `NFD_OpenDialog` / `NFD_SaveDialog` / `NFD_OpenDialogMultiple` / `NFD_FreePath` / `NFD_PathSet` APIs.
- [x] 6.2 Bind `editor_tbl["OpenDialog"] = [](sol::table opts) -> sol::object { ... }` returning the path string, a 1-indexed table of path strings (multi-select), or `nil`. Honor `filter`, `default_path`, `multi` keys; default `filter` to `"All"`, `multi` to `false`.
- [x] 6.3 Bind `editor_tbl["SaveDialog"] = [](sol::table opts) -> sol::object { ... }` returning the path string or `nil`. Honor `filter`, `default_path`, `default_name` keys.
- [x] 6.4 In both bindings, log any `NFD_*` error result via `FURYE` and return `nil` (do not throw).
- [x] 6.5 In the `#else` (WITH_EDITOR=OFF) block, add the no-op stubs returning `sol::nil` for both bindings.

## 7. Editor.lua: frame handler + camera framing math

- [x] 7.1 In `examples/Editor.lua` startup, call `Editor.SetFrameSelectionHandler(function(node) ... end)` that:
  - Reads `node:GetWorldPosition()` always (fallback target).
  - If the node has a `MeshRender` component (check via `node:GetComponent("MeshRender")` or equivalent Lua binding; if not exposed, fall back to `node:GetWorldAABB():Valid()`) AND `node:GetWorldAABB():Valid()` is true, use the AABB's `GetCenter()` / `GetMin()` / `GetMax()` for framing.
  - Computes `radius = (max - min):Length() * 0.5`.
  - Computes `distance = radius / math.tan(0.7854 * 0.5) * 1.25` (assuming 45° vfov; matches the camera created at line 242).
  - Picks `eye = center + normalize(1, 0.6, 1) * distance` (with `normalize(1, 0.6, 1)` precomputed or computed inline).
  - For the non-renderable path, uses `center = node:GetWorldPosition()` and a fixed `distance = 10.0` (gives a "close look" without being inside the node).
  - Updates the Lua upvalues `cam_pos = eye`, `yaw = atan2(-eye.x + center.x, -eye.z + center.z)` (matching the convention at line 327 of `Editor.lua`), `pitch = atan2(eye.y - center.y, horizontal_distance)` — clamped to ±89°.
- [x] 7.2 Verify that after the handler runs, the next `on_update(dt)` writes the new `cam_pos`/`yaw`/`pitch` into `cam_node` (no extra C++ work needed — the existing per-frame push at lines 402–404 does it).

**7.1.1 (post-apply fix)** The yaw/pitch lines used `math.atan2(...)` originally, but Lua 5.4 (vendored as `engine/ThirdParty/lua/lmathlib.c:744`) gates `math.atan2` behind `#if defined(LUA_COMPAT_MATHLIB)` — fury3d doesn't define that macro, so `math.atan2` is `nil` at runtime. Switched to `math.atan(y, x)` (same semantics, opposite arg order) which is the standard Lua 5.4 form.
- [ ] 7.3 Manually verify (in-engine) that double-clicking a mesh-leaf frames it nicely, double-clicking a non-renderable leaf looks at its position, and WASD/mouse-drag continues smoothly from the new view.

## 8. Editor.lua: replace Save-As modal with native dialog

- [x] 8.1 In `examples/Editor.lua`, locate the `on_save_as` callback registered in `Editor.SetSceneIO` (currently around lines 263–275). Replace its body so it calls `Editor.SaveDialog({filter = "scene", default_path = <scene_dir>, default_name = <current-scene-name or "untitled">})`. If `nil` is returned, do nothing. Otherwise call `FileUtil.SaveCompressedFile(scene, path)` (or the existing save helper used elsewhere in `Editor.lua`).
- [x] 8.2 In `engine/Fury/Editor/Editor.cpp`, remove `RenderSaveAsModal()` and the `g_SaveAsModalOpen` flag (and its `extern` declaration at line 75). Update the "File → Save As..." menu item handler to call the Lua-registered `on_save_as` callback (which now drives the native dialog). If the menu item previously toggled `g_SaveAsModalOpen = true` directly, change it to dispatch through `Editor::SceneIO` (e.g., a new `Editor::RequestSaveAs()` that invokes the Lua callback), mirroring how `on_save` / `on_open` already dispatch.
- [x] 8.3 Verify any cross-references to `g_SaveAsModalOpen` (e.g., in `EditorConfirmDialog.cpp` which "mirrors" its pattern) are not broken. If `EditorConfirmDialog` only mirrors the flag-then-popup *pattern* (not the variable itself), no change is needed; otherwise update accordingly.
- [ ] 8.4 Manually verify "File → Save As" launches the native Save dialog, picking a path writes the scene to that path, and cancelling leaves the dirty flag unchanged.

**8.5 (post-apply addition)** `File → Open...` was also converted to the native dialog for consistency with Save As / Import. Added `Editor::TriggerOpen()` C++ helper (mirrors `TriggerSaveAs` / `TriggerImport`), routed Ctrl+O and the menu item through it, and removed `g_OpenModalOpen` and the entire `RenderOpenImportModal` function (no longer referenced by any caller — the "Import Scene" call was already removed in task 9, the "Open Scene" call is now removed too). Lua's `on_open` callback follows the same empty-string signal pattern as `on_import`: bare filename from the `File → Open ▸ <file>` submenu → existing path; empty string from `File → Open...` → `Editor.OpenDialog({filter="json,bin", default_path=scene_dir})` → `open_scene_at_path(path)`.

## 9. Editor.lua: replace Content Browser import trigger with native dialog (optional)

- [x] 9.1 Locate the Content Browser's "Import" button / "Open Asset" flow (search `Editor.cpp` and `Editor.lua` for the import trigger). Replace the file-source picker with `Editor.OpenDialog({filter = "gltf,glb,fbx", default_path = <last import dir>, multi = true})`. Iterate the returned table and pass each path to the existing `import_scene` handler.
- [x] 9.2 If `Editor.OpenDialog` returns `nil` (cancel), do nothing.
- [ ] 9.3 Manually verify multi-select import works (select 3 glTF files, all three import).

## 10. Build & verification

- [x] 10.1 Clean build `WITH_EDITOR=ON` (default) — both `cmake` configure and the `fury` link step succeed.
- [x] 10.2 Clean build `WITH_EDITOR=OFF` — configure and link succeed without nfd.
- [ ] 10.3 Manual smoke test in the editor:
  - Double-click a parent node in Scene Inspector → expands/collapses (no rename field).
  - Double-click a mesh-leaf node → camera frames the mesh's AABB.
  - Double-click a non-renderable leaf (e.g., an empty node) → camera looks at its position.
  - F2 with a node selected → rename field activates (existing behavior preserved).
  - Right-click → "Rename" → rename field activates (existing behavior preserved).
  - File → Save As → native Save dialog appears; picking a path writes the scene.
  - File → Open → native Open dialog appears (if Content Browser import was wired up in task 9).
