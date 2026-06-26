## 1. Vendor ImGuizmo + extend texture format support

- [ ] 1.1 Copy `/Users/sindney/Documents/git/furyengine/ImGuizmo/src/ImGuizmo.cpp` and `ImGuizmo.h` into `engine/ThirdParty/ImGuizmo/`. Add a top-of-file comment recording the upstream commit hash, copy date, and any patches applied.
- [ ] 1.2 Patch the vendored ImGuizmo's includes: change `#include "imgui.h"` / `"imgui_internal.h"` to `#include "ImGui/imgui.h"` / `"ImGui/imgui_internal.h"` so it resolves through the engine's include path.
- [ ] 1.3 In `engine/CMakeLists.txt`, inside the existing `if(WITH_EDITOR)` block, append `engine/ThirdParty/ImGuizmo/ImGuizmo.cpp` to the editor source list and add `engine/ThirdParty/ImGuizmo` to the editor target's include dirs.
- [ ] 1.4 Smoke-build `WITH_EDITOR=ON`. Confirm `ImGuizmo.cpp` compiles. Confirm `WITH_EDITOR=OFF` still configures and builds.
- [ ] 1.5 Add `R32UI` to `engine/Fury/EnumUtil.h`'s `TextureFormat` enum (positioned after `R32F`, alphabetically/logically grouped). Update `engine/Fury/Texture.cpp`'s GL-format mapping switches: `internalFormat = GL_R32UI`, `format = GL_RED_INTEGER`, `type = GL_UNSIGNED_INT`. Grep for other `case TextureFormat::` switches in the codebase and add the missing arms.
- [ ] 1.6 Verify with a tiny unit test (or one-off `main.cpp` snippet) that `Texture::CreateEmpty(64, 64, 0, TextureFormat::R32UI, ...)` succeeds and roundtrips a uint via a glClear+glReadPixels.

## 2. Picking pass + ID table

- [ ] 2.1 Create `engine/Fury/Editor/EditorPicking.hpp` with the public surface: `void EnsureFBO(int w, int h);`, `void RequestPickAt(ImVec2 viewport_px);`, `void TickPostRender();`, plus the internal state types (`PickState` enum, `g_IdTable` declaration). All declarations under `#ifdef WITH_EDITOR`.
- [ ] 2.2 Create `engine/Fury/Editor/EditorPicking.cpp`. Implement the three states (`Idle`, `RenderRequested`, `AwaitingReadback`) and the per-state behavior described in the design.
- [ ] 2.3 Define the inline `id_pass.vs` / `id_pass.fs` shader strings inside `EditorPicking.cpp` (mirror the `EditorBlitCubeShader` pattern). Add a static `Shader` instance compiled lazily on first use.
- [ ] 2.4 Define the inline `id_pass_skinned.vs` shader. Reuse the existing skinned-mesh shader's bone-uniform layout exactly (`vertex_bone_id`, `vertex_bone_weight`, bone matrices) so `Shader::BindMesh` and `Shader::BindCamera` can drive it without per-shader specialization.
- [ ] 2.5 Implement `EnsureFBO(w, h)`: lazily creates `g_PickColor` (R32UI), `g_PickDepth` (DEPTH24), and `g_PickFBO`. Resizes when w/h change.
- [ ] 2.6 Implement the id-pass render loop: walk the renderable nodes (use `Scene::Active->GetSceneManager()`'s renderable enumeration — pattern after `PrelightPipeline::Execute`'s `RenderQuery` build), assign 1-based IDs, bind shader + uniforms per node, draw mesh.
- [ ] 2.7 Implement the readback: `glReadPixels(GL_RED_INTEGER, GL_UNSIGNED_INT, ...)` of a 1×1 region at the captured viewport-pixel coordinate (Y-flip from ImGui-top-origin to GL-bottom-origin). Resolve uint → `g_IdTable[id-1].lock()` → `g_SelectedSceneNode`.
- [ ] 2.8 Hook `EditorPicking::TickPostRender` from `engine/Fury/Engine.cpp` between `Pipeline::Active->Execute(...)` (called from Lua) and `Gui::Render`. Note: the pipeline is currently driven from Lua in `Editor.lua`'s `on_update`, not from `Engine::Tick` directly. Two options:
   - 2.8.a Add `Editor::TickPostRender()` as a public function and call it from `Engine.cpp` immediately before `Gui::Render` runs (which is also called from Lua's `on_update`). Adjust `Engine.cpp`'s contract: post-render runs unconditionally each frame after the user callback.
   - 2.8.b Add `Editor::TickPostRender()` and have the engine call it after the user `on_update` returns but before the next frame's `Gui::NewFrame`. Confirm via timing this still produces in-frame readbacks.
   Choose 2.8.a after consulting the Engine.cpp frame loop in detail.
- [ ] 2.9 Add the `Editor::TickPostRender` no-op stub to `Editor.h` for `WITH_EDITOR=OFF` builds.

## 3. Gizmo integration

- [ ] 3.1 Create `engine/Fury/Editor/EditorGizmo.cpp`. Owns `g_GizmoMode`, `g_GizmoSpace`, `g_SnapEnabled`, `g_SnapTranslate/Rotate/Scale` defined here as file-locals (with `extern` declarations available to `EditorNodeProperties.cpp`).
- [ ] 3.2 Add `RenderGizmo()` invoked from `Editor::Tick` AFTER all dock + windows render but BEFORE the existing user `on_update` runs the pipeline. The gizmo needs to submit ImGui draw lists, so it must run during the same NewFrame/Render bracket.
- [ ] 3.3 At the top of each `Tick`, call `ImGuizmo::BeginFrame()` (per ImGuizmo's contract).
- [ ] 3.4 Compute the central-node rect via `ImGui::DockBuilderGetCentralNode(s_DockspaceID)`. Pass it to `ImGuizmo::SetRect(x, y, w, h)`.
- [ ] 3.5 Build a fullscreen invisible ImGui window over the central rect (`ImGui::SetNextWindowPos/Size`, `ImGuiWindowFlags_NoBackground | NoTitleBar | NoInputs | NoScrollbar | NoSavedSettings`) and call `ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList())` inside it. Required by ImGuizmo so its draws live in the right ImGui layer.
- [ ] 3.6 In `RenderGizmo()`, when a node is selected and a pipeline + camera are active: build view = `inverse(camera_node->GetWorldMatrix())`, projection = `camera->GetProjectionMatrix()`, matrix = `node->GetWorldMatrix()`. Pass to `ImGuizmo::Manipulate` with the current operation/mode. Pass `&snap_value` only if `g_SnapEnabled`.
- [ ] 3.7 On `ImGuizmo::IsUsing()`, decompose the modified world matrix back to local: `Ml = inverse(parent_world) * Mw`. Decompose into translation/rotation/scale. Write back ONLY the components affected by the active operation.
- [ ] 3.8 Verify `Matrix4::Raw[16]` layout matches what ImGuizmo expects (column-major). Add a `static_assert` or runtime check documenting the assumption.
- [ ] 3.9 Ensure `RenderGizmo()` early-returns when: no scene active, no camera, no selection, or `pick_state` is mid-flight (so pick + drag don't fight).
- [ ] 3.10 Decide and implement Matrix4 decompose: check whether `MathUtil` already exposes a decompose helper; if not, add one (`bool MathUtil::Decompose(const Matrix4&, Vector4& t, Quaternion& r, Vector4& s)`) following the standard SVD-free decomposition (column normalization for rotation, length for scale, last column for translation).

## 4. Node Properties panel — gizmo controls

- [ ] 4.1 In `engine/Fury/Editor/EditorNodeProperties.cpp`, add a `RenderGizmoSection()` rendering the Gizmo CollapsingHeader (default-open) containing the 3 mode radio buttons, 2 space radio buttons, Snap checkbox, and the three snap-step DragFloat widgets (visible / enabled only when Snap is on).
- [ ] 4.2 Section is rendered FIRST (before the existing Node section), regardless of whether a node is selected.
- [ ] 4.3 Wire each control to its `g_*` variable; on change, mark the imgui.ini settings as dirty (`ImGui::MarkIniSettingsDirty`).

## 5. Persistence — extend FuryEditor settings handler

- [ ] 5.1 In `engine/Fury/Editor/Editor.cpp`'s `SettingsHandler_WriteAll`, append a `Gizmo=...` line listing mode, space, snap_enabled, snap_translate, snap_rotate, snap_scale.
- [ ] 5.2 In `SettingsHandler_ReadLine`, parse the `Gizmo=...` line and apply via the `Editor::SetGizmo*` setters.
- [ ] 5.3 Verify that imgui.ini round-trips: change mode, restart, verify mode is preserved.

## 6. Editor + Lua surface for gizmo controls

- [ ] 6.1 Add to `engine/Fury/Editor/Editor.h` (under `#ifdef WITH_EDITOR`): `void Editor::SetGizmoMode(const char*);`, `void SetGizmoSpace(const char*);`, `void SetSnapEnabled(bool);`, `const char* GetGizmoMode();`, `const char* GetGizmoSpace();`, `bool GetSnapEnabled();`. Add no-op stubs for `WITH_EDITOR=OFF`.
- [ ] 6.2 Implement those in `Editor.cpp` (or `EditorGizmo.cpp` — pick whichever owns the state). Unknown name strings silently no-op.
- [ ] 6.3 In `engine/Fury/LuaBindings.cpp`, expose `Editor.SetGizmoMode(name)`, `Editor.SetGizmoSpace(name)`, `Editor.SetSnapEnabled(bool)`. Provide `WITH_EDITOR=OFF` no-op fallbacks.

## 7. Selection sync — viewport picking ↔ Scene Inspector

- [ ] 7.1 Wire viewport-click detection: at the start of `Editor::Tick`, after the central-rect computation and before any window renders, check `ImGui::IsMouseClicked(0)`. If the cursor is inside the central rect AND `!ImGui::GetIO().WantCaptureMouse` AND `!ImGuizmo::IsOver()` AND `!ImGuizmo::IsUsing()`, call `EditorPicking::RequestPickAt(cursor - rect.pos)`.
- [ ] 7.2 Verify the existing Scene Inspector code still drives `g_SelectedSceneNode` (no change needed — it already does).
- [ ] 7.3 Verify the dangling-pointer walk in `EditorNodeProperties.cpp` (from `add-node-properties-panel-and-save`) handles a picked node that gets destroyed: pick → delete via `File → New` → property panel renders empty state.

## 8. Manual / runtime verification

- [ ] 8.1 Build `WITH_EDITOR=ON`. Confirm zero new warnings beyond ImGuizmo's vendored output.
- [ ] 8.2 Build `WITH_EDITOR=OFF`. Confirm clean configure / build / link, no `EditorPicking.cpp` / `EditorGizmo.cpp` / `ImGuizmo.cpp` referenced.
- [ ] 8.3 Run with `Resource/Scene/scene.bin`. Click on the tank → tank highlighted in Scene Inspector + Node Properties panel + gizmo appears. Drag translate gizmo → tank moves visibly.
- [ ] 8.4 Switch to ROTATE in Node Properties panel. Drag rotation handle → tank rotates. Numbers in the panel reflect the new rotation.
- [ ] 8.5 Switch to SCALE. Drag scale handle → tank scales. Verify numbers match.
- [ ] 8.6 Toggle Snap on with translate=2.0, drag → tank snaps to 2-unit increments.
- [ ] 8.7 Click empty viewport space → selection clears, gizmo disappears, panel shows `(no node selected)`.
- [ ] 8.8 Click on a child node (e.g. a sub-mesh of the tank if applicable). Confirm only the local transform of THAT child changes when dragged, parent unaffected.
- [ ] 8.9 Resize the engine window. Click in the new bounds. Verify pick still resolves correctly (FBO recreated at the new size).
- [ ] 8.10 Close + reopen the engine. Confirm gizmo mode + snap settings are preserved (imgui.ini round-trip).
- [ ] 8.11 Click on a docked panel (Settings / Console). Confirm no pick fires, no gizmo activation.
- [ ] 8.12 Run the Cmd+S save flow with edits made via the gizmo. Reopen the saved scene → verify gizmo-edited transforms persisted to disk.
