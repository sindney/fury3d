## 1. Render Target + Pipeline Extension (foundation, non-breaking)

- [x] 1.1 Add a `RenderTarget` abstraction (if one doesn't already exist) wrapping an FBO with one `RGBA8` color texture + one `DEPTH24` attachment, with `GetFramebuffer()`, `GetColorTexture()`, `Resize(w,h)`, `GetSize()`, lazy allocate, and explicit release. Place under `engine/Fury/` (e.g., `RenderTarget.{hpp,cpp}`).
- [x] 1.2 Add `void PrelightPipeline::Execute(SceneManager* scene, RenderTarget* target = nullptr)` (and update the base `Pipeline` virtual signature) — when `target == nullptr` bind the default framebuffer (current behavior); when non-null, bind `target->GetFramebuffer()` for all passes. Keep every other `Execute` behavior identical. Verify `examples/Editor.lua:377` (`Pipeline.GetActive():Execute(octree)`) still compiles and runs unchanged.
- [x] 1.3 Verify a `WITH_EDITOR=OFF` build still compiles and runs a sample (e.g., any non-editor sample calling `Pipeline::Execute(scene)`) with no behavioral change.

## 2. Viewport Window + Dockspace Flags

- [x] 2.1 In `engine/Fury/Editor/Editor.cpp`, drop `ImGuiDockNodeFlags_PassthruCentralNode` from the `ImGui::DockSpaceOverViewport` call at line 574.
- [x] 2.2 Add `g_ShowViewport = true` and a `Viewport` entry to the window-visibility switch in `Editor::SetWindowVisible` / `GetWindowVisible` (`Editor.cpp:669-690`) and to the `Window` menu loop (`Editor.cpp:459-475`). Add `Viewport` to the `RenderViewportWindow(&g_ShowViewport)` dispatch in `Editor.cpp:596-601`.
- [x] 2.3 Implement `RenderViewportWindow(bool* show)` in `engine/Fury/Editor/EditorWindows.cpp`: `ImGui::Begin("Viewport", show, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)`; if `*show && ImGui::GetContentRegionAvail().x > 0 && .y > 0`: ensure the editor's `RenderTarget` matches the content-rect size, invoke `Pipeline::Execute(scene, rt)` (or signal `Editor.lua` to do so — see 2.5), then `ImGui::Image((ImTextureID)(intptr_t)rt->GetColorTexture()->GetId(), avail, ImVec2(0,0), ImVec2(1,1))`. `ImGui::End()`.
- [x] 2.4 Update `BuildDefaultLayout` (`Editor.cpp:194-214`) to dock the `Viewport` window into the central node (the node returned by `ImGui::DockBuilderGetCentralNode(s_DockspaceID)` after the left/right/bottom splits) on first run / Reset Layout.
- [x] 2.5 Decide the render-trigger path: either the editor calls `Pipeline::Execute` directly from `RenderViewportWindow` (preferred — keeps `Editor.lua:377`'s `Pipeline.GetActive():Execute(octree)` redundant for the editor path), OR expose a callback so `Editor.lua`'s `on_update` calls `Pipeline::Execute` with the editor-supplied `RenderTarget`. Pick one and remove the now-dead back-buffer `Pipeline::Execute` call from `Editor.lua` when the editor is active. Document the choice in the implementation.
- [x] 2.6 Verify: dock/undock/tab/float the Viewport window; the 3D scene follows it and fills its content rect with no stretching.

## 3. Camera Aspect Derivation

- [x] 3.1 In `RenderViewportWindow` (or the chosen render-trigger site), before `Pipeline::Execute`, read `ImGui::GetContentRegionAvail()`, compute `aspect = avail.x / max(avail.y, 1)`, and call `camera->PerspectiveFov(fovy, aspect, near, far)` on the active pipeline camera. Preserve `fovy` / near / far from the existing camera (replace the hard-coded `1.778` at `examples/Editor.lua:230`).
- [x] 3.2 When the Viewport window is hidden or collapsed, leave the camera aspect at its last visible-frame value (do NOT reset to a hard-coded constant).
- [x] 3.3 Verify: resizing the Viewport window updates the scene's aspect on the next frame with no distortion; vertical FOV is preserved.

## 4. Selection-Changed Signal + SetSelectedSceneNode Routing

- [x] 4.1 Add `Signal<SceneNode*>& Editor::OnSelectionChanged();` to `engine/Fury/Editor/Editor.h` and a file-scope `static Signal<SceneNode*> g_SelectionChangedSignal;` in `Editor.cpp`. Return a reference from the accessor.
- [x] 4.2 Add `void Editor::SetSelectedSceneNode(SceneNode* node)` to `Editor.h` / `Editor.cpp` that sets `g_SelectedSceneNode = node` and emits `g_SelectionChangedSignal(node)`.
- [x] 4.3 Route every existing write to `g_SelectedSceneNode` through `Editor::SetSelectedSceneNode`: `EditorPicking.cpp:263` (deselect) and `:272` (select); `EditorWindows.cpp:429` and `:455` (Inspector tree clicks); `Editor.cpp:228` (File→New), `:404` (Open submenu), `:589` (Open modal), `:661` (Shutdown); `EditorNodeProperties.cpp:296, 301` (dangling-pointer cleanup). Replace each `g_SelectedSceneNode = X;` with `Editor::SetSelectedSceneNode(X);`.
- [x] 4.4 Verify: selecting via viewport, via Inspector, and via File→New each fire `OnSelectionChanged` exactly once (add a temporary subscriber that logs).

## 5. Move TRS Gizmo into Viewport-Window Content-Rect Space

- [x] 5.1 In `Editor::Tick` (`Editor.cpp:610-625`), replace the full-window rect computation (`ImGui::GetMainViewport()->Pos/Size`) with the Viewport window's content rect. Capture the content rect during `RenderViewportWindow` (store `g_ViewportContentMin` / `g_ViewportContentSize` as file-scope state) and pass it to `RenderGizmo(g_ViewportContentMin, g_ViewportContentSize)`.
- [x] 5.2 In `EditorGizmo::RenderGizmo` (`EditorGizmo.cpp:59-60`), `ImGuizmo::SetRect` now receives the Viewport content rect (the parameter names already say `central_rect_min/size` — semantics finally match the names).
- [x] 5.3 Add a guard in `RenderGizmo`: if the Viewport window is hidden, collapsed, or has a zero-size content rect, skip gizmo rendering entirely that frame.
- [x] 5.4 Verify: gizmo lands exactly on the selected node inside the docked Viewport window; dock the Viewport window to the right edge and confirm the gizmo follows; gizmo disappears when the Viewport window is hidden.

## 6. Move Picking FBO + Coordinates into Viewport-Window Content-Rect Space

- [x] 6.1 In `EditorPicking::EnsureFBO(w, h)` (`EditorPicking.cpp:118-166`), size the FBO to the Viewport window's content-rect size (use the `g_ViewportContentSize` captured in step 5.1) instead of `InputUtil::GetWindowSize()`. Clamp to ≥1×1.
- [x] 6.2 In `Editor::Tick`'s click-capture block (`Editor.cpp:635-650`), replace the `DockBuilderGetCentralNode` rect gate with "cursor is inside `g_ViewportContentMin/Size` AND the Viewport window is hovered AND `!WantCaptureMouse` AND no gizmo in use". Capture the click position relative to `g_ViewportContentMin` (`mp - g_ViewportContentMin`), NOT relative to `ImGui::GetMainViewport()->Pos`.
- [x] 6.3 In `EditorPicking::DoReadback` (`EditorPicking.cpp:245-275`), Y-flip against the Viewport content-rect height (`y = g_ViewportContentSize.y - 1 - captured_y`) instead of the full SFML window height. Verify `g_IdTable` is still rebuilt from `Scene::Active->GetSceneManager()`'s renderable nodes.
- [x] 6.4 In `EditorPicking::DoIdPass` (`EditorPicking.cpp:172-239`), use the same camera view + projection matrices the main pipeline used this frame (including the viewport-content-rect-derived aspect from step 3.1).
- [x] 6.5 Add a guard: skip the picking pass entirely (discard the request, `pick_state = Idle`) when the Viewport window is hidden or has a zero-size content rect.
- [x] 6.6 Verify: true-click a node inside the docked Viewport window → correct selection; dock the Viewport window elsewhere → picking still works in the new position.

## 7. Click-vs-Drag Disambiguation

- [x] 7.1 Replace the `IsMouseClicked(Left)` pick trigger in `Editor::Tick` (`Editor.cpp:635-650`) with the two-phase state machine from design Decision 4 / viewport-picking spec: on `IsMouseClicked(Left)` inside the Viewport content rect, record `g_PickDownPos` + `g_PickDownFrame` + `g_PickIsDrag = false` (do NOT call `RequestPickAt` yet).
- [x] 7.2 While LMB is held, track displacement from `g_PickDownPos`; if it exceeds `ImGui::GetIO().MouseDragThreshold`, set `g_PickIsDrag = true`.
- [x] 7.3 On `IsMouseReleased(Left)`: if `!g_PickIsDrag` AND release position within threshold of `g_PickDownPos` AND Viewport window still hovered → `Picking::RequestPickAt(release_pos_relative_to_content_rect)`. Reset `g_PickDownPos`, `g_PickIsDrag`. If `g_PickIsDrag` or release is outside the content rect → no pick.
- [x] 7.4 Verify: a drag (press, move >threshold, release) does NOT schedule a pick and `g_SelectedSceneNode` is unchanged; a true click (press + release at same spot) DOES select; clicking empty space deselects.

## 8. Lua `IsPickInFlight` Accessor + Editor.lua Camera-Drag Gate

- [x] 8.1 Expose `bool Editor::IsPickInFlight()` in `Editor.h` (returning `Picking::IsPickInFlight()` from `EditorPicking.hpp:28`) and bind it to Lua as `Editor.IsPickInFlight()` in the editor Lua bindings (`LuaBindings.cpp` or wherever the `Editor` table is registered).
- [x] 8.2 In `examples/Editor.lua:318`, change the camera-drag LMB-down gate to `local lmb_down = focused and has_mo and input:GetMouseDown(MouseButton.Left) and not Editor.IsPickInFlight()`.
- [x] 8.3 Provide the no-op stub for `WITH_EDITOR=OFF` (returns `false`) so user scripts compile against both builds.
- [x] 8.4 Verify: true-click a node and immediately start a second drag — the second drag's camera motion is suppressed while `IsPickInFlight()` is true, then resumes once the pick resolves.

## 9. Selection Visualization Overlay

- [x] 9.1 Define `kSelectionColor` (e.g., `Color(0.0f, 1.0f, 0.5f, 1.0f)`) as a file-scope constant in `engine/Fury/Editor/Editor.cpp` (or a new `EditorSelectionViz.cpp`).
- [x] 9.2 Implement `DrawSelectionOverlay(cameraNode)` (in `Editor.cpp` or a new `EditorSelectionViz.{hpp,cpp}`): if `g_SelectedSceneNode == nullptr` return. Call `RenderUtil::Instance()->BeginDrawMeshs(camera)` / `BeginDrawLines(camera)` as appropriate. Branch on the node's components:
  - Has `Light` with `LightType::POINT`: `scaledWorld = node->GetWorldMatrix(); scaledWorld.AppendScale(Vector4(light->GetRadius(), light->GetRadius(), light->GetRadius(), 1.0));` then `DrawMesh(light->GetMesh(), scaledWorld, kSelectionColor)`.
  - Has `Light` with `LightType::SPOT`: `DrawMesh(light->GetMesh(), node->GetWorldMatrix(), kSelectionColor)` (mesh is pre-shaped by `EvaluateVolume`).
  - Has `Light` with `LightType::DIRECTIONAL`: skip (infinite bounds).
  - Else (mesh / other): `DrawBoxBounds(node->GetWorldAABB(), kSelectionColor)`.
  Call `EndDrawMeshes()` / `EndDrawLines()`.
- [x] 9.3 Invoke `DrawSelectionOverlay(cameraNode)` from `Editor::TickPostRender` (after `Pipeline::Execute`, before `Gui::Render`) so the wireframe composites over the 3D scene in the Viewport render target. Only invoke when the Viewport window is visible.
- [x] 9.4 Verify: select a mesh node → green wireframe AABB; select a point light → green wireframe sphere of radius `Light::GetRadius()`; select a spot light → green wireframe cone of the correct height/base radius; deselect → overlay disappears; overlay draws with all Profiler debug toggles off.

## 10. Light Volume Mesh Sync Fix

- [x] 10.1 In `engine/Fury/Editor/EditorNodeProperties.cpp:244` (and any other site that calls `Light::CalculateAABB()` on a geometry-affecting edit), add an adjacent `light->EvaluateVolume()` call so `Light::GetMesh()` returns a mesh consistent with the current `Type` / `InnerAngle` / `OutterAngle` / `Radius`.
- [x] 10.2 Verify: edit a spot light's `Radius` or `Outer Angle` in the Node Properties panel → the selection cone (and the Profiler's `LIGHT_BOUNDS` overlay) updates to the new shape on the next frame (no stale mesh).

## 11. Window Menu + Visibility API Wiring

- [x] 11.1 Add `Viewport` to the `Window` menu bullet list in `Editor.cpp:134-141` (top of the built-in-window list, per the modified editor-shell spec).
- [x] 11.2 Confirm `Editor::SetWindowVisible("Viewport", …)` / `Editor::GetWindowVisible("Viewport")` work end-to-end (toggled from the menu, from Lua, and from the default visibility flag).
- [x] 11.3 Verify the `Window → Reset Layout` action docks the Viewport window into the center and the `PassthruCentralNode` flag is no longer set (no transparent hole in the center).

## 12. Build + Manual Verification

- [x] 12.1 Build with `WITH_EDITOR=ON` (default) and resolve any compile/link errors.
- [x] 12.2 Build with `WITH_EDITOR=OFF` and confirm the engine still builds and runs a non-editor sample with no behavioral change (the `Pipeline::Execute` default argument path).
- [x] 12.3 Manual pass: (a) dock/undock/tab/float the Viewport window — scene + gizmo + pick follow; (b) true-click selects, drag doesn't pick; (c) click empty space deselects; (d) select point/spot light + mesh node — correct wireframe shape each; (e) resize the Viewport window — aspect updates, no distortion; (f) `Window → Reset Layout` restores the center-docked Viewport.
- [x] 12.4 Run `openspec validate editor-viewport-overhaul --strict` (or the project's equivalent validation command) and resolve any spec-validation errors.

## 13. Camera-Input Gating Fix (post-validation, bug report)

After the change was implemented, two camera-input bugs surfaced from manual play-testing in `examples/Editor.lua`:

- The undocked Viewport window's title bar counted as "viewport hover" (`ImGui::IsWindowHovered` is true over the entire window including chrome), so grabbing the title bar to move the window ALSO rotated the camera.
- WASD translation did not work simultaneously with mouse drag rotation — the two gates could fall out of sync, and dragging over the title bar / borders / scrollbars broke the LMB-drag latch.

Fix:

- [x] 13.1 Add `Editor::IsViewportContentHovered()` (`engine/Fury/Editor/Editor.h`, `.cpp`) — strictly checks that the cursor is inside the captured content rect (`g_ViewportVisible && size > 0 && ImGui::GetIO().MousePos` inside `[g_ViewportContentMin, g_ViewportContentMin + g_ViewportContentSize]`). Distinct from `IsViewportHovered`, which is window-level and includes chrome.
- [x] 13.2 Add the `WITH_EDITOR=OFF` no-op stub and Lua binding (`Editor.IsViewportContentHovered`) in `engine/Fury/LuaBindings.cpp`, mirroring the existing `IsViewportHovered` bindings.
- [x] 13.3 In `examples/Editor.lua`, switch the `has_mo` predicate from `Editor.IsViewportHovered()` to `Editor.IsViewportContentHovered()` so dragging the Viewport window chrome never arms camera-drag. Gate the WASD block on `Editor.IsViewportContentHovered()` too, so drag and WASD share a single predicate and stay in sync.
- [x] 13.4 Rebuild with `WITH_EDITOR=ON` + verify the binary is fresh; manual pass: dragging the undocked Viewport title bar moves the window without rotating the camera; clicking + dragging inside viewport content rotates; WASD translates while LMB is held inside content; WASD is silent when the cursor is over a docked panel (e.g., SceneInspector).
- [x] 13.5 Add `io.ConfigWindowsMoveFromTitleBarOnly = true` in `engine/Fury/Gui.cpp::Initialize`. Without this, the ImGui default lets click+drag on any empty/id-less widget (including the Viewport's `ImGui::Image` of the render-target texture) move the undocked Viewport window — so dragging inside the content both rotates the camera AND drifts the window. Title-bar-only matches Unity/Unreal/Godot and lets the Viewport content rect stay a clean camera-input surface.
