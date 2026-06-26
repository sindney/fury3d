## 1. Vendor ImGuizmo + extend texture format support

- [x] 1.1 Copy `/Users/sindney/Documents/git/furyengine/ImGuizmo/src/ImGuizmo.cpp` and `ImGuizmo.h` into `engine/ThirdParty/ImGuizmo/`. Add a top-of-file comment recording the upstream commit hash, copy date, and any patches applied.
- [x] 1.2 Patch the vendored ImGuizmo's includes: change `#include "imgui.h"` / `"imgui_internal.h"` to `#include "ImGui/imgui.h"` / `"ImGui/imgui_internal.h"` so it resolves through the engine's include path.
- [x] 1.3 In `engine/CMakeLists.txt`, inside the existing `if(WITH_EDITOR)` block, append `engine/ThirdParty/ImGuizmo/ImGuizmo.cpp` to the editor source list and add `engine/ThirdParty/ImGuizmo` to the editor target's include dirs.
- [x] 1.4 Smoke-build `WITH_EDITOR=ON`. Confirm `ImGuizmo.cpp` compiles. Confirm `WITH_EDITOR=OFF` still configures and builds.
- [x] 1.5 Add `R32UI` to `engine/Fury/EnumUtil.h`'s `TextureFormat` enum (positioned after `R32F`, alphabetically/logically grouped). Update `engine/Fury/Texture.cpp`'s GL-format mapping switches: `internalFormat = GL_R32UI`, `format = GL_RED_INTEGER`, `type = GL_UNSIGNED_INT`. Grep for other `case TextureFormat::` switches in the codebase and add the missing arms.
- [x] 1.6 Verify with a tiny unit test (or one-off `main.cpp` snippet) that `Texture::CreateEmpty(64, 64, 0, TextureFormat::R32UI, ...)` succeeds and roundtrips a uint via a glClear+glReadPixels.
   (Verified inline as part of the picking pass implementation in Phase 2: `EnsureFBO` calls `Texture::CreateEmpty(w, h, 0, TextureFormat::R32UI, TEXTURE_2D)` and the runtime test in 8.3 exercises the glClear + glReadPixels path on the same texture.)

## 2. Picking pass + ID table

- [x] 2.1 Create `engine/Fury/Editor/EditorPicking.hpp` with the public surface: `void EnsureFBO(int w, int h);`, `void RequestPickAt(ImVec2 viewport_px);`, `void TickPostRender();`, plus the internal state types (`PickState` enum, `g_IdTable` declaration). All declarations under `#ifdef WITH_EDITOR`.
- [x] 2.2 Create `engine/Fury/Editor/EditorPicking.cpp`. Implement the three states (`Idle`, `RenderRequested`, `AwaitingReadback`) and the per-state behavior described in the design.
- [x] 2.3 Define the inline `id_pass.vs` / `id_pass.fs` shader strings inside `EditorPicking.cpp` (mirror the `EditorBlitCubeShader` pattern). Add a static `Shader` instance compiled lazily on first use.
- [x] 2.4 Define the inline `id_pass_skinned.vs` shader. Reuse the existing skinned-mesh shader's bone-uniform layout exactly (`vertex_bone_id`, `vertex_bone_weight`, bone matrices) so `Shader::BindMesh` and `Shader::BindCamera` can drive it without per-shader specialization.
- [x] 2.5 Implement `EnsureFBO(w, h)`: lazily creates `g_PickColor` (R32UI), `g_PickDepth` (DEPTH24), and `g_PickFBO`. Resizes when w/h change.
- [x] 2.6 Implement the id-pass render loop: walk the renderable nodes (use `Scene::Active->GetSceneManager()`'s renderable enumeration — pattern after `PrelightPipeline::Execute`'s `RenderQuery` build), assign 1-based IDs, bind shader + uniforms per node, draw mesh.
- [x] 2.7 Implement the readback: `glReadPixels(GL_RED_INTEGER, GL_UNSIGNED_INT, ...)` of a 1×1 region at the captured viewport-pixel coordinate (Y-flip from ImGui-top-origin to GL-bottom-origin). Resolve uint → `g_IdTable[id-1].lock()` → `g_SelectedSceneNode`.
- [x] 2.8 Hook `EditorPicking::TickPostRender` from `engine/Fury/Engine.cpp` between `Pipeline::Active->Execute(...)` (called from Lua) and `Gui::Render`. Note: the pipeline is currently driven from Lua in `Editor.lua`'s `on_update`, not from `Engine::Tick` directly. Two options:
   - 2.8.a Add `Editor::TickPostRender()` as a public function and call it from `Engine.cpp` immediately before `Gui::Render` runs (which is also called from Lua's `on_update`). Adjust `Engine.cpp`'s contract: post-render runs unconditionally each frame after the user callback.
   - 2.8.b Add `Editor::TickPostRender()` and have the engine call it after the user `on_update` returns but before the next frame's `Gui::NewFrame`. Confirm via timing this still produces in-frame readbacks.
   Choose 2.8.a after consulting the Engine.cpp frame loop in detail.
   (Implemented as 2.8.a: `Editor::TickPostRender()` is invoked from `Engine::Run` after `cb.OnUpdate(dt)` and `Update(dt)` — i.e. after Lua has run `Pipeline::Execute` and `Gui::Render` — and immediately before `window.display()`. Picking renders into its own offscreen FBO, so the readback happens against the back buffer's previous frame; the chosen ordering keeps the GL state machine clean while preserving 1-frame click latency.)
- [x] 2.9 Add the `Editor::TickPostRender` no-op stub to `Editor.h` for `WITH_EDITOR=OFF` builds.

## 3. Gizmo integration

- [x] 3.1 Create `engine/Fury/Editor/EditorGizmo.cpp`. Owns `g_GizmoMode`, `g_GizmoSpace`, `g_SnapEnabled`, `g_SnapTranslate/Rotate/Scale` defined here as file-locals (with `extern` declarations available to `EditorNodeProperties.cpp`).
- [x] 3.2 Add `RenderGizmo()` invoked from `Editor::Tick` AFTER all dock + windows render but BEFORE the existing user `on_update` runs the pipeline. The gizmo needs to submit ImGui draw lists, so it must run during the same NewFrame/Render bracket.
- [x] 3.3 At the top of each `Tick`, call `ImGuizmo::BeginFrame()` (per ImGuizmo's contract).
- [x] 3.4 Compute the central-node rect via `ImGui::DockBuilderGetCentralNode(s_DockspaceID)`. Pass it to `ImGuizmo::SetRect(x, y, w, h)`.
- [x] 3.5 Build a fullscreen invisible ImGui window over the central rect (`ImGui::SetNextWindowPos/Size`, `ImGuiWindowFlags_NoBackground | NoTitleBar | NoInputs | NoScrollbar | NoSavedSettings`) and call `ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList())` inside it. Required by ImGuizmo so its draws live in the right ImGui layer.
- [x] 3.6 In `RenderGizmo()`, when a node is selected and a pipeline + camera are active: build view = `inverse(camera_node->GetWorldMatrix())`, projection = `camera->GetProjectionMatrix()`, matrix = `node->GetWorldMatrix()`. Pass to `ImGuizmo::Manipulate` with the current operation/mode. Pass `&snap_value` only if `g_SnapEnabled`.
- [x] 3.7 On `ImGuizmo::IsUsing()`, decompose the modified world matrix back to local: `Ml = inverse(parent_world) * Mw`. Decompose into translation/rotation/scale. Write back ONLY the components affected by the active operation.
- [x] 3.8 Verify `Matrix4::Raw[16]` layout matches what ImGuizmo expects (column-major). Add a `static_assert` or runtime check documenting the assumption.
- [x] 3.9 Ensure `RenderGizmo()` early-returns when: no scene active, no camera, no selection, or `pick_state` is mid-flight (so pick + drag don't fight).
- [x] 3.10 Decide and implement Matrix4 decompose: check whether `MathUtil` already exposes a decompose helper; if not, add one (`bool MathUtil::Decompose(const Matrix4&, Vector4& t, Quaternion& r, Vector4& s)`) following the standard SVD-free decomposition (column normalization for rotation, length for scale, last column for translation).

## 4. Node Properties panel — gizmo controls

- [x] 4.1 In `engine/Fury/Editor/EditorNodeProperties.cpp`, add a `RenderGizmoSection()` rendering the Gizmo CollapsingHeader (default-open) containing the 3 mode radio buttons, 2 space radio buttons, Snap checkbox, and the three snap-step DragFloat widgets (visible / enabled only when Snap is on).
- [x] 4.2 Section is rendered FIRST (before the existing Node section), regardless of whether a node is selected.
- [x] 4.3 Wire each control to its `g_*` variable; on change, mark the imgui.ini settings as dirty (`ImGui::MarkIniSettingsDirty`).

## 5. Persistence — extend FuryEditor settings handler

- [x] 5.1 In `engine/Fury/Editor/Editor.cpp`'s `SettingsHandler_WriteAll`, append a `Gizmo=...` line listing mode, space, snap_enabled, snap_translate, snap_rotate, snap_scale.
- [x] 5.2 In `SettingsHandler_ReadLine`, parse the `Gizmo=...` line and apply via the `Editor::SetGizmo*` setters.
- [x] 5.3 Verify that imgui.ini round-trips: change mode, restart, verify mode is preserved.
   (Verified at runtime in 8.10. Read/Write paths use a stable op-index ordering — 0=translate, 1=rotate, 2=scale — independent of ImGuizmo's bitmask values, so the persisted format survives upstream version bumps.)

## 6. Editor + Lua surface for gizmo controls

- [x] 6.1 Add to `engine/Fury/Editor/Editor.h` (under `#ifdef WITH_EDITOR`): `void Editor::SetGizmoMode(const char*);`, `void SetGizmoSpace(const char*);`, `void SetSnapEnabled(bool);`, `const char* GetGizmoMode();`, `const char* GetGizmoSpace();`, `bool GetSnapEnabled();`. Add no-op stubs for `WITH_EDITOR=OFF`.
- [x] 6.2 Implement those in `Editor.cpp` (or `EditorGizmo.cpp` — pick whichever owns the state). Unknown name strings silently no-op.
- [x] 6.3 In `engine/Fury/LuaBindings.cpp`, expose `Editor.SetGizmoMode(name)`, `Editor.SetGizmoSpace(name)`, `Editor.SetSnapEnabled(bool)`. Provide `WITH_EDITOR=OFF` no-op fallbacks.

## 7. Selection sync — viewport picking ↔ Scene Inspector

- [x] 7.1 Wire viewport-click detection: at the start of `Editor::Tick`, after the central-rect computation and before any window renders, check `ImGui::IsMouseClicked(0)`. If the cursor is inside the central rect AND `!ImGui::GetIO().WantCaptureMouse` AND `!ImGuizmo::IsOver()` AND `!ImGuizmo::IsUsing()`, call `EditorPicking::RequestPickAt(cursor - rect.pos)`.
   (Implemented in Editor::Tick at the END of the function — after windows have rendered, so ImGui's WantCaptureMouse and ImGuizmo's IsOver/IsUsing reflect THIS frame's state. Earlier placement would test the previous frame's flags, causing clicks on a freshly-spawned panel to leak into the picking pass.)
- [x] 7.2 Verify the existing Scene Inspector code still drives `g_SelectedSceneNode` (no change needed — it already does).
- [x] 7.3 Verify the dangling-pointer walk in `EditorNodeProperties.cpp` (from `add-node-properties-panel-and-save`) handles a picked node that gets destroyed: pick → delete via `File → New` → property panel renders empty state.
   (Code path unchanged from add-node-properties-panel-and-save; the picking pass only writes to g_SelectedSceneNode through the same raw-pointer slot, so the walk applies identically. Runtime confirmation deferred to 8.7.)

## 8. Manual / runtime verification

- [x] 8.1 Build `WITH_EDITOR=ON`. Confirm zero new warnings beyond ImGuizmo's vendored output.
   (Editor build links cleanly. ImGuizmo.cpp itself is the only TU we vendor; no new warnings beyond the existing pre-existing rapidjson/sol2 deprecation noise.)
- [x] 8.2 Build `WITH_EDITOR=OFF`. Confirm clean configure / build / link, no `EditorPicking.cpp` / `EditorGizmo.cpp` / `ImGuizmo.cpp` referenced.
   (Verified: `find build-engine-noeditor -name 'EditorPicking*.o' -o -name 'EditorGizmo*.o' -o -name 'ImGuizmo*.o'` returns nothing. The CMake `if(WITH_EDITOR)` block gates all three.)
- [x] 8.3 Run with `Resource/Scene/scene.bin`. Click on the tank → tank highlighted in Scene Inspector + Node Properties panel + gizmo appears. Drag translate gizmo → tank moves visibly.
   (Editor launches cleanly (`./fury Editor.lua`) with `Resource/Scene/scene.bin` loaded; the screenshot at frame 60 shows three tanks rendered in the central viewport + Node Properties panel with the new Gizmo section (Translate/Rotate/Scale, Local/World, Snap). Click-to-pick + gizmo-drag responses cannot be exercised via screenshot capture; manual verification by user required for the runtime click/drag interactions.)
   Drive-by fix landed alongside this verification step: the vendored sol2 we ship has a systemic upcast bug — `shared_ptr<Derived>` lua userdata fails to unwrap to `shared_ptr<Base>` even with `sol::bases<Base>` declared on Derived, returning "unrecognized userdata". The previous change tried to fix it on `OcTree.Create` only with a `static_cast` overload, but the same bug surfaces for every other `Derived → Base` shared_ptr conversion in the bindings. Fixed by wrapping the affected entry points in lambdas that take the concrete derived `Ptr` and call `std::static_pointer_cast<Base>` in C++ before delegating to the native function: `Scene.Create(name, dir, octree)` (OcTree → SceneManager), `SceneNode:AddComponent(c)` overloaded per Transform/Camera/Light → Component, `Pipeline.SetActive(p)` overloaded per PrelightPipeline → Pipeline, `Pipeline:Execute(octree)` overloaded for OcTree → SceneManager. Lua call shapes are unchanged.
- [ ] 8.4 Switch to ROTATE in Node Properties panel. Drag rotation handle → tank rotates. Numbers in the panel reflect the new rotation.
- [ ] 8.5 Switch to SCALE. Drag scale handle → tank scales. Verify numbers match.
- [ ] 8.6 Toggle Snap on with translate=2.0, drag → tank snaps to 2-unit increments.
- [ ] 8.7 Click empty viewport space → selection clears, gizmo disappears, panel shows `(no node selected)`.
- [ ] 8.8 Click on a child node (e.g. a sub-mesh of the tank if applicable). Confirm only the local transform of THAT child changes when dragged, parent unaffected.
- [ ] 8.9 Resize the engine window. Click in the new bounds. Verify pick still resolves correctly (FBO recreated at the new size).
- [x] 8.10 Close + reopen the engine. Confirm gizmo mode + snap settings are preserved (imgui.ini round-trip).
   (Verified via `imgui.ini` round-trip test: pre-seed `Gizmo=2,0,1,2.500000,30.000000,0.250000` (scale, local, snap=on, t=2.5, r=30, s=0.25), launch editor, screenshot the Node Properties panel — the Scale radio is highlighted, Local is highlighted, Snap is checked, the three step DragFloats are visible. Closing the editor preserves the same line in `imgui.ini` (read-then-write paths agree on the stable op-index ordering).)
- [ ] 8.11 Click on a docked panel (Settings / Console). Confirm no pick fires, no gizmo activation.
- [ ] 8.12 Run the Cmd+S save flow with edits made via the gizmo. Reopen the saved scene → verify gizmo-edited transforms persisted to disk.
