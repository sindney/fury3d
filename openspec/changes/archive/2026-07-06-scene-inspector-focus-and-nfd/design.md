## Context

The Scene Inspector today uses `ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick` for every row, plus a manual double-click detector at `engine/Fury/Editor/EditorWindows.cpp:670` that triggers `DoRenameActivate(node)`. The result is that double-click both toggles the open state (for parents) and starts rename — which is fine for parents (the rename just no-ops on the open/close) but wrong for leaves (a leaf has nothing to expand, so the only effect of double-click is rename, which is not what users expect). Rename is independently reachable via the row's context menu and the F2 key, so double-click is redundant as a rename trigger.

The editor camera is owned by `examples/Editor.lua`. Three Lua upvalues — `cam_pos` (`Vector4`), `yaw`, `pitch` — are written into `cam_node` every `on_update(dt)` (lines 402–404). C++ cannot safely write `cam_node`'s transform directly because the next Lua `on_update` would overwrite it with the stale Lua-side `cam_pos/yaw/pitch`. Any "frame this node" action initiated from C++ must therefore update the Lua-side state, not just the scene node.

`nativefiledialog-extended` (nfd) is an MIT-licensed, actively-maintained fork of `nativefiledialog` that already ships a CMake build with platform detection (AppKit on macOS, Win32 on Windows, GTK/portal on Linux). It defaults to a static lib (`BUILD_SHARED_LIBS=OFF`) and exposes an `nfd::nfd` ALIAS target. fury3d's existing third-party layout (`engine/ThirdParty/<lib>/` as git submodules, integrated from `engine/CMakeLists.txt`) has a precedent for the `add_subdirectory()` + alias-target pattern (SFML), so nfd fits cleanly.

fury3d only supports macOS and Windows today (`engine/CMakeLists.txt:10` errors on Linux), so nfd's Linux GTK/portal path is irrelevant.

## Goals / Non-Goals

**Goals:**
- Replace double-click rename with double-click-to-focus on leaf rows.
- Preserve expand/collapse on parent-row double-click (already works via ImGui flag — must NOT regress).
- Frame the node's `WorldAABB` when renderable; otherwise frame `GetWorldPosition()`.
- Add a C++ → Lua bridge so the Lua-owned camera can be repositioned without C++ writing the transform behind Lua's back.
- Vendor `nativefiledialog-extended` under `engine/ThirdParty/nfd/` as a git submodule, statically linked, gated on `WITH_EDITOR`.
- Expose minimal Lua bindings (`Editor.OpenDialog`, `Editor.SaveDialog`) and replace the existing Save-As ImGui modal with a native dialog.

**Non-Goals:**
- Refactoring the editor camera ownership out of `Editor.lua` (the Lua-driven camera stays).
- Adding a smoothed/animated camera transition (the focus is a snap; a future change can add easing if desired).
- Multi-select framing (single node only; framing a multi-selection AABB is a future concern).
- Linux support for nfd (fury3d does not build on Linux today).
- Full migration of every ImGui modal in the editor to native dialogs (only Save-As and Content Browser import are migrated; other dialogs stay as-is).
- Per-platform UI customization (filter lists differ by OS — nfd's defaults are accepted as-is).

## Decisions

### Decision 1: Drop `ImGuiTreeNodeFlags_OpenOnDoubleClick` is NOT needed; remove the manual rename trigger instead

**Choice:** Keep `ImGuiTreeNodeFlags_OpenOnDoubleClick` on parent rows (it already produces the expand/collapse behavior we want) and remove only the `IsMouseDoubleClicked` → `DoRenameActivate` block at `EditorWindows.cpp:670–672`.

**Alternatives considered:**
- Remove the ImGui flag and handle expand/collapse manually in C++. Rejected — the ImGui flag is already correct for parents and is the canonical pattern; rolling our own would just duplicate ImGui behavior.
- Keep double-click → rename for parents only. Rejected — even on parents, double-click-then-rename is the wrong default (the row's context menu and F2 already cover rename, and double-clicking a parent is what users do to drill in).

**Why:** The user's requirement is "double-click should not rename." The minimal, lowest-risk edit is to delete the rename-on-double-click trigger. ImGui continues to handle the expand/collapse for parents via the flag.

### Decision 2: For leaf double-click, invoke a Lua-registered frame handler; do NOT have C++ compute the camera transform

**Choice:** Add `Editor::SetFrameSelectionHandler(std::function<void(SceneNode*)>)` in C++. `Editor.lua` registers a handler at startup; the C++ inspector's leaf-double-click branch invokes it. The Lua handler computes the new `cam_pos`/`yaw`/`pitch` from the node's `WorldAABB` (renderable) or `GetWorldPosition()` (non-renderable) and writes them to the Lua upvalues. The next `on_update(dt)` writes them to `cam_node` as usual.

**Alternatives considered:**
- C++ computes the eye/center and calls `cam_node->SetLocalPosition()` / `SetLocalRoattion()` directly. Rejected — the next Lua `on_update` overwrites the transform with the stale `cam_pos`/`yaw`/`pitch`. The camera would jump back one frame later.
- Add a "Lua-side camera override" flag that the next `on_update` honors. Rejected — adds shared state and a one-frame contract for marginal benefit.
- Move camera ownership into C++. Rejected — out of scope; would require also moving the WASD/mouse-drag input loop, breaking the existing `Editor.lua` script contract.

**Why:** The Lua-owned camera is a deliberate design (it lets users tune the camera via `Editor.SetCameraSettings` and WASD/drag in pure Lua). A Lua-registered callback is the minimal bridge that preserves the existing ownership model. It also matches the established pattern (`SetSceneIO`, `SetCommandHandler`, `SetCameraSettings`) — every other editor behavior that needs script-driven state goes through a Lua-registered handler.

### Decision 3: Renderability check = `MeshRender` component + valid `WorldAABB`

**Choice:** A node is "renderable with bounds" iff `node->GetComponent<MeshRender>()` returns non-null AND `node->GetWorldAABB().Valid()` is true. Otherwise the focus falls back to `node->GetWorldPosition()`.

**Why:** `MeshRender` is the only component in fury3d that produces geometry with a meaningful world AABB; `Light::GetAABB()` exists but its bounds are not a useful framing target (a directional light's AABB is typically the whole scene). Lights and other non-renderable nodes get the simpler "look at position" treatment, which is what the user asked for ("if it's not renderable, just look at that location is fine").

### Decision 4: Eye position computed from AABB size + camera fov, with a fixed diagonal direction

**Choice:** The Lua handler computes the eye as: place the camera along a fixed direction `(1, 0.6, 1)` (normalized) at a distance `d` such that the AABB fits the camera's vertical FOV with a 1.25× margin. Specifically:
- `center = aabb.GetCenter()`
- `radius = (aabb.GetMax() - aabb.GetMin()).Length() * 0.5`
- `distance = radius / tan(fovy * 0.5) * 1.25` (where `fovy` is the camera's vertical fov; defaults to `0.7854` rad ≈ 45°)
- `eye = center + normalize(1, 0.6, 1) * distance`
- Then derive `yaw`/`pitch` from `eye - center` to keep the Lua upvalues in sync (so subsequent WASD movement continues from the new view).

**Why:** A diagonal direction avoids the degenerate "looking straight down a cardinal axis" cases and gives a recognizable 3/4 view of the object — the standard "frame selection" framing in DCC tools. The 1.25× margin ensures the AABB isn't clipped at the viewport edges. Keeping the Lua `yaw`/`pitch`/`cam_pos` in sync is what makes this cooperate with the existing WASD/mouse-drag code.

### Decision 5: nfd integrated as a git submodule + `add_subdirectory()` (the SFML pattern)

**Choice:** Add `engine/ThirdParty/nfd` as a git submodule pointing at `https://github.com/btzy/nativefiledialog-extended.git`. In `engine/CMakeLists.txt`, gated on `WITH_EDITOR`:
```cmake
if(WITH_EDITOR)
    set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/nfd)
endif()
```
And in the `fury` link line:
```cmake
target_link_libraries(fury PRIVATE ... nfd::nfd)
```
Submodule-presence check at configure time (matching the existing pattern for SFML/rapidjson/tinygltf/lua/sol2/meshoptimizer).

**Alternatives considered:**
- Vendor nfd's source files directly under `engine/ThirdParty/nfd/` and `file(GLOB)` them into a static lib (the lua/meshoptimizer pattern). Rejected — nfd's CMake handles per-platform source selection (`nfd_cocoa.m` on macOS, `nfd_win.cpp` on Windows, optional `UniformTypeIdentifiers` framework on macOS ≥ 11), and reproducing that logic in `engine/CMakeLists.txt` would duplicate upstream decisions and risk drift on the next nfd bump.
- Use nfd as a system dependency (find_package). Rejected — fury3d vendored every other third-party dep; introducing a system dependency for nfd would break the `git submodule update --init --recursive` workflow.
- Link nfd as a shared library. Rejected — the user explicitly requested static linking, and it matches the engine's "single static `fury` executable" default (`BUILD_SHARED_LIBS=OFF`).

**Why:** The SFML precedent is exactly the right shape: a third-party library with its own CMake build, integrated as a submodule + `add_subdirectory()` + alias target. Static linking is already nfd's default. Gating on `WITH_EDITOR` mirrors how ImGuizmo and ImReflect are gated.

### Decision 6: nfd's `3ps/wayland-protocols/` subdirectory is preserved but inert

**Choice:** The submodule path includes nfd's `3ps/wayland-protocols/` directory (used only on Linux with `NFD_WAYLAND=ON`). It is left untouched — fury3d does not build on Linux, so the wayland path is never compiled. No `NFD_WAYLAND` / `NFD_X11` / `NFD_PORTAL` option is set by `engine/CMakeLists.txt`; nfd's defaults stand.

**Why:** Touching the submodule's contents would make future updates painful. Leaving the Linux-specific paths in place costs nothing on macOS/Windows.

### Decision 7: Lua file-dialog API returns `nil` on cancel, a path string on single-select, a table of path strings on multi-select

**Choice:** Bind:
- `Editor.OpenDialog({filter = "...", default_path = "...", multi = false}) → string|table<string>|nil`
- `Editor.SaveDialog({filter = "...", default_path = "...", default_name = "..."}) → string|nil`

Filter is a single string in nfd's format (`"png,jpg,jpeg"` or `"All"` / empty). The bindings call `NFD_OpenDialog` / `NFD_SaveDialog` directly and translate the result.

**Why:** Mirrors the existing pattern where `Editor.lua` calls into engine-provided Lua bindings and reads back primitives. Multi-select is supported on macOS and Windows, so the API exposes it; the existing Content Browser import flow uses single-select today and can adopt multi-select with a one-line change.

### Decision 8: Save-As modal is replaced by `Editor.SaveDialog`; the `g_SaveAsModalOpen` flag is retired

**Choice:** `Editor.cpp`'s `RenderSaveAsModal()` is removed (along with `g_SaveAsModalOpen` and its call site in `Editor::Tick`). The "File → Save As" menu item calls a new `Editor::SaveAsCallback` registered from Lua (mirroring the `SceneIO` pattern), which invokes `Editor.SaveDialog` and then `Editor.SceneIO.on_save_as(path)`.

**Alternatives considered:**
- Keep the modal and add native dialog as a separate menu item. Rejected — the modal is a hand-rolled ImGui file picker that the user is explicitly trying to retire by adding nfd; keeping both is worse than either alone.

**Why:** Single source of truth. The user's intent is "integrate nfd" — replacing the Save-As modal with the native dialog is the natural payoff.

## Risks / Trade-offs

- **[Risk] Removing double-click rename is a behavior change existing users may rely on.** → Mitigation: rename is still reachable via F2 (when inspector focused, existing behavior preserved) and the row's context menu. Documented in the modified spec.
- **[Risk] nfd submodule adds a `git submodule update --init --recursive` step to fresh clones.** → Mitigation: matches existing precedent (SFML, sol2, lua, etc.); the configure-time `if(NOT EXISTS) message(FATAL_ERROR)` check tells the user exactly what to run.
- **[Risk] nfd's macOS path may need `UniformTypeIdentifiers` framework on macOS ≥ 11.** → Mitigation: nfd's own CMake handles this via `check_cxx_source_compiles`; nothing for us to do.
- **[Risk] `SetFrameSelectionHandler` not registered (e.g., custom Lua script without `Editor.lua`) silently no-ops on leaf double-click.** → Mitigation: C++ checks for null handler and falls back to a no-op (or could log once at info level). Acceptable: the contract is "Lua registers if it wants focus behavior."
- **[Risk] The diagonal `(1, 0.6, 1)` framing direction produces a bad view for AABBs that are very flat (e.g., a ground plane).** → Mitigation: For flat AABBs the framing will look slightly off but still informative; a future change could pick the direction from the AABB's principal axis. Out of scope here.
- **[Trade-off] nfd's filter syntax differs from a hypothetical cross-platform filter spec; we accept nfd's string format.** → Acceptable: nfd is the only platform-native dialog in use; the format is documented in nfd's header.
- **[Trade-off] Replacing Save-As modal removes the in-editor "type the filename" affordance.** → Acceptable: native Save dialogs prompt for filename in the OS file picker, which is strictly better UX.
- **[Risk] Removing `g_SaveAsModalOpen` may interact with `EditorConfirmDialog.cpp` which mirrors that flag pattern.** → Mitigation: `EditorConfirmDialog` is a separate confirm dialog (used for "discard changes?" prompts); it is unaffected. Verify during implementation.

## Migration Plan

1. Add the `engine/ThirdParty/nfd` submodule.
2. Update `engine/CMakeLists.txt` to bring in nfd gated on `WITH_EDITOR`.
3. Add `Editor::SetFrameSelectionHandler` C++ API + Lua binding.
4. Add `Editor.OpenDialog` / `Editor.SaveDialog` Lua bindings.
5. Update `Editor.lua`: register the frame handler, replace Save-As modal call site with `Editor.SaveDialog`, replace Content Browser import trigger with `Editor.OpenDialog`.
6. Remove `RenderSaveAsModal()` / `g_SaveAsModalOpen` from `Editor.cpp` and update the File menu's Save-As entry to call the Lua-registered callback.
7. Update `EditorWindows.cpp:RenderNodeRow` to drop rename-on-double-click and add the leaf-double-click → frame handler call.
8. Build both `WITH_EDITOR=ON` and `WITH_EDITOR=OFF`; verify both compile.

**Rollback:** All edits are localized to the listed files. Reverting the commits restores the old Save-As modal, the old double-click rename, and removes the nfd submodule — no on-disk state outside the repo is touched.

## Open Questions

- Should the leaf-double-click focus also be reachable via a keyboard shortcut (e.g., `F` while a node is selected, mirroring Unreal/Blender)? The user did not request this; left out of scope but a trivial follow-up if desired.
- Should the Content Browser's asset thumbnail double-click also gain a "frame in viewport" behavior for scene-asset references? Out of scope — only the Scene Inspector is in scope per the user's request.
