## Context

The editor today renders the 3D scene straight to the SFML back buffer and lets it show through the dockspace's empty center via `ImGuiDockNodeFlags_PassthruCentralNode` (`engine/Fury/Editor/Editor.cpp:574-576`). Consequently:

- The 3D viewport cannot be docked, tabbed, or resized independently like every other editor panel.
- The TRS gizmo's `ImGuizmo::SetRect` is clamped to the full SFML window's pos/size (`engine/Fury/Editor/Editor.cpp:610-625` → `engine/Fury/Editor/EditorGizmo.cpp:59-60`), and the picking FBO is sized to the full window with pick coordinates captured in full-window pixels (`engine/Fury/Editor/EditorPicking.cpp:118-166`, `:245-275`). The archived `2026-06-26-add-3d-picking-and-gizmo` design (Decision 5, lines 155-169) explicitly documented this as a workaround that inverts once a docked viewport is introduced — this change is that inversion.
- Camera-drag (`examples/Editor.lua:311-332`, fires on plain LMB-down with `!WantCaptureMouse`) and picking (`engine/Fury/Editor/Editor.cpp:635-650`, fires on `IsMouseClicked(Left)` with the same gate) are structurally coupled: any LMB-down over the central region arms the camera look AND schedules a pick. A click-look selects whatever node happens to be under the cursor; a drag still resolves a pick at the initial click position.
- There is no selection visualization. `Pipeline::DrawDebug` (`engine/Fury/Pipeline.cpp:695-759`) already draws wireframe spheres (point lights, scaled by `Light::GetRadius()`), cones (spot lights, pre-shaped by radius/outer angle in `Light::EvaluateVolume`), and AABBs (`RenderUtil::DrawBoxBounds`) — but only behind Profiler toggle flags, for ALL nodes, never for the selected node alone.
- There is no selection-changed event; `g_SelectedSceneNode` (`engine/Fury/Editor/Editor.cpp:78`) is a raw pointer written from four sites and polled every frame by every consumer. The `Signal<T>` template exists (`engine/Fury/Signal.h`) but is not wired to selection.

ImGui docking is already enabled (`engine/Fury/Gui.cpp:37`). ImGuizmo is already integrated. The engine has existing `Texture`/FBO infrastructure. No new third-party dependencies are required.

## Goals / Non-Goals

**Goals:**
- Introduce a dockable "Viewport" ImGui window that displays the 3D scene rendered to an offscreen render target, with the camera projection's aspect derived from the window's content rect.
- Move the TRS gizmo and the picking pass into viewport-window-content-rect space (coordinate space + FBO sizing + `ImGuizmo::SetRect`), as called for by the archived picking-and-gizmo design.
- Decouple picking from camera-drag: picking fires only on a true click (press + release at the same spot within a movement threshold, no intervening drag); picking nothing on a true click deselects.
- Add editor-only selection visualization (wireframe AABB / sphere / cone) for the selected node, reusing the existing `RenderUtil` debug-draw primitives.
- Wire a `Signal<SceneNode*>` selection-changed event and expose `Picking::IsPickInFlight()` to Lua so `Editor.lua`'s camera-drag can short-circuit during an in-flight pick (defense in depth alongside the click-vs-drag gate).
- Keep `WITH_EDITOR=OFF` builds unchanged: the render-to-back-buffer path remains the default when no editor viewport RT is supplied.

**Non-Goals:**
- Multi-viewport / detached OS windows (`ImGuiConfigFlags_ViewportsEnable` remains off per `editor-shell` spec).
- Rendering multiple simultaneous viewports (e.g., a top/front/side quad-viewport layout). One dockable Viewport window in v1.
- Outline shaders / stencil-based silhouette outlining. Selection feedback is wireframe-only in v1 (matches the existing Profiler debug overlays).
- Changing the camera-drag input modality (still plain LMB-drag). The click-vs-drag gate handles the conflict; we do not introduce Alt/middle-mouse modifiers.
- Refactoring `g_SelectedSceneNode` into a selection set (multi-selection). Still a single selection.
- Persisting the Viewport window's dock position across a layout reset (it follows the same `imgui.ini` rules as every other window).

## Decisions

### Decision 1: Render the 3D scene to an offscreen color+depth render target, blit into the Viewport window via `ImGui::Image`

The `PrelightPipeline::Execute` final pass currently writes to the default framebuffer (the SFML back buffer). We add an optional `RenderTarget*` argument to `Pipeline::Execute` (default `nullptr` = back buffer, preserving non-editor behavior). When the editor supplies a `RenderTarget` (color `RGBA8` + `DEPTH24`), the pipeline binds it as the color attachment for all passes; the editor then `ImGui::Image`s the color texture into the Viewport window's content rect.

**Why not keep rendering to the back buffer and crop with ImGui:** Cropping would require the docked window's rect to always sit atop the back-buffer region where the scene was drawn — which breaks the moment the user tabs/floats/undocks the Viewport window. Rendering to a texture decouples the 3D scene's drawable region from the OS window entirely, which is exactly what dockability requires.

**Why a single shared RT instead of one-per-viewport:** v1 has exactly one Viewport window. A single `RenderTarget` owned by the editor, recreated when the content-rect size changes, is the simplest correct design. Per-viewport RTs are a multi-viewport follow-up.

**Aspect ratio:** each frame, after `ImGui::Begin("Viewport")`, the editor reads `ImGui::GetContentRegionAvail()`, derives `aspect = avail.x / max(avail.y, 1)`, and calls `camera->PerspectiveFov(fovy, aspect, near, far)` before `Pipeline::Execute`. This replaces the hard-coded `1.778` aspect at `examples/Editor.lua:230`.

**Alternatives considered:** (a) Render to back buffer + `PassthruCentralNode` retained — rejected, cannot dock/tab the viewport. (b) Render the scene in a separate offscreen GLFWwindow — rejected, adds window-management complexity and breaks single-context simplicity.

### Decision 2: Drop `ImGuiDockNodeFlags_PassthruCentralNode`; the Viewport window is the center-docked tab in the default layout

With the 3D scene now living inside a real ImGui window, the `PassthruCentralNode` trick is obsolete and actively harmful (it would leave a transparent hole in the center that shows the empty back buffer). The dockspace drops that flag. `BuildDefaultLayout` gains a center-docked Viewport window (the dockspace's central node is no longer "empty").

**Why not keep `PassthruCentralNode` as a fallback when the Viewport window is hidden:** mixing modes doubles the coordinate-space reasoning in the gizmo + picking code for a corner case (user explicitly hid the Viewport window). Simpler to say: if the Viewport window is hidden, the gizmo and picking are no-ops that frame. The spec calls this out.

### Decision 3: Picking FBO is sized to the Viewport window's content rect; pick coordinates are viewport-content-rect-relative

Today the picking FBO is sized to `InputUtil::GetWindowSize()` (full SFML window) and pick coords are `mp - viewport_pos` where `viewport_pos = ImGui::GetMainViewport()->Pos` (`engine/Fury/Editor/EditorPicking.cpp:118-166`, `engine/Fury/Editor/Editor.cpp:610-625`). After this change:

- The picking FBO is sized to the Viewport window's content rect (clamped to ≥1×1).
- The id-pass renders using the SAME camera matrices the main pipeline used (which now project into the viewport-content-rect aspect — see Decision 1).
- Pick coordinates are captured relative to the Viewport window's content-rect top-left (`mp - viewport_window_content_min`), Y-flipped against the content-rect height for the `glReadPixels` call.
- `Editor::Tick`'s click-capture gate becomes "cursor is inside the Viewport window's content rect AND the Viewport window is hovered/focused AND `!WantCaptureMouse` AND no gizmo is in use" — replacing the current `DockBuilderGetCentralNode` rect check.

Because `EditorPicking` and `EditorGizmo` already plumb a single rect through (`RenderGizmo(central_rect_min, central_rect_size)`, `Picking::RequestPickAt(ImVec2)`), the change is localized to `Editor::Tick`'s placement code. The archived picking-and-gizmo design Decision 5 predicted exactly this.

**GPU memory win:** the picking FBO shrinks from full-window (e.g., 1920×1080 R32UI + DEPTH24 ≈ 16 MB) to viewport-content-rect size (often a few hundred × a few hundred). Called out in the archived design.

### Decision 4: Click-vs-drag disambiguation — defer the pick to mouse release, gate on a movement threshold

Today picking schedules on `IsMouseClicked(Left)` (the down edge). We switch to a two-phase state machine inside the editor (NOT in `Editor.lua`'s camera-drag, which continues to arm on LMB-down):

1. On `IsMouseClicked(Left)` inside the Viewport window's content rect (with `!WantCaptureMouse`, no gizmo in use): record `g_PickDownPos = mouse_pos` and `g_PickDownFrame = current_frame`. Do NOT schedule a pick yet.
2. While LMB is held: track the max cursor displacement from `g_PickDownPos`. If it exceeds `kPickDragThresholdPx` (default 4 px — matches ImGui's own drag threshold `ImGui::GetIO().MouseDragThreshold`), mark `g_PickIsDrag = true`.
3. On `IsMouseReleased(Left)`: if `!g_PickIsDrag` AND the release position is within the threshold of the down position AND the Viewport window is still hovered: schedule the pick via `Picking::RequestPickAt(...)` with the release position (in viewport-content-rect-relative coords). Reset `g_PickDownPos`, `g_PickIsDrag`.

**Why threshold-based instead of "exact same pixel":** a perfectly still human click still jitters 1–2 px. ImGui's own `MouseDragThreshold` (default 4 on macOS with `ConfigMacOSXBehaviors`, 6 elsewhere) exists for exactly this reason; we reuse it.

**Why defer to release rather than schedule-on-down + cancel-on-drag:** `Picking::RequestPickAt` is already idempotent (folds in-flight requests, `EditorPicking.cpp:278-285`), but the two-frame state machine (`RenderRequested → AwaitingReadback → Idle`) means a cancel-after-schedule would need a new `Cancel()` path and would still burn one frame of GPU work on every drag. Deferring to release means drags never touch the picking FBO at all.

**Camera-drag stays on LMB-down:** `Editor.lua:318-332` is unchanged on the input side. The camera still yaws/pitches on LMB-drag. The conflict is resolved because picking no longer fires on the down edge — a drag produces camera motion and no pick; a true click (no movement) produces a pick and no camera motion (the camera-drag's delta is zero when the cursor doesn't move).

**Defense in depth:** expose `Picking::IsPickInFlight()` to Lua so `Editor.lua`'s camera-drag can additionally short-circuit while a pick's readback is pending. This catches the edge case where a true click schedules a pick and the user immediately starts a second drag — the second drag won't fight a stale pick.

**Picking nothing on a true click deselects:** unchanged from today's `id == 0 → g_SelectedSceneNode = nullptr` (`EditorPicking.cpp:263`). The spec already covers this; we keep it.

### Decision 5: Selection visualization reuses `RenderUtil::DrawBoxBounds` / `RenderUtil::DrawMesh` wireframe, driven by a selection-changed `Signal`

The editor adds a `DrawSelectionOverlay(cameraNode, query)` function called from `Editor::TickPostRender` (after the main pipeline draw, before `Gui::Render`) when `g_SelectedSceneNode != nullptr`. It uses the existing `RenderUtil` primitives:

- **Mesh / non-light nodes**: `RenderUtil::DrawBoxBounds(node->GetWorldAABB(), kSelectionColor)` — identical to `Pipeline::DrawDebug`'s `meshBoundsOn` path (`engine/Fury/Pipeline.cpp:713-717`).
- **`LightType::POINT`**: take `light->GetMesh()` (the unit ico-sphere), scale its world matrix by `Vector4(light->GetRadius(), 0.0f)`, call `RenderUtil::DrawMesh(mesh, scaledWorld, kSelectionColor)` — mirroring `Pipeline.cpp:738-753`.
- **`LightType::SPOT`**: `light->GetMesh()` is already pre-shaped (cone with `bottomR = tan(outerAngle*0.5) * radius`, translated down by `height*0.5`) by `Light::EvaluateVolume` (`engine/Fury/Light.cpp:233-243`); call `RenderUtil::DrawMesh(mesh, node->GetWorldMatrix(), kSelectionColor)`.

`kSelectionColor` is a single editor constant (e.g., `Color(0.0, 1.0, 0.5, 1.0)` — distinct from the Profiler's `light->GetColor()`-tinted debug overlays so the two are visually distinguishable).

**Why wireframe instead of an outline shader:** wireframe reuses existing `RenderUtil` API with zero new shaders. Outline shaders (stencil inflate / inverted hull) are a larger follow-up and aren't needed for the user's "show bounding box / sphere / cone like the profiler debug view" ask — the profiler debug view IS wireframe.

**Why draw in `TickPostRender` rather than inside `Pipeline::DrawDebug`:** `Pipeline::DrawDebug` is gated by `PipelineSwitch` flags and draws for all nodes; selection viz is a single-node editor concern. Drawing it from the editor side keeps the pipeline unaware of selection and lets the overlay draw regardless of which `PipelineSwitch` flags are on. The `RenderUtil` is a singleton accessible from the editor (`engine/Fury/RenderUtil.h:32`).

**Stale mesh guard:** `Light::GetMesh()` lazily builds once from `EvaluateVolume()` and the Node Properties panel calls `CalculateAABB()` but not `EvaluateVolume()` on radius/angle edits (`engine/Fury/Editor/EditorNodeProperties.cpp:244`). To keep the selection cone/sphere in sync, the editor SHALL call `light->EvaluateVolume()` once before drawing when the light's cached mesh fields are dirty. The simplest correct approach: have the Node Properties panel call `EvaluateVolume()` (in addition to `CalculateAABB()`) on geometry-affecting edits. This is a small bug-fix folded into this change.

### Decision 6: Selection-changed `Signal<SceneNode*>` + Lua `Picking::IsPickInFlight()` accessor

- Add `Signal<SceneNode*>& Editor::OnSelectionChanged()` returning a reference to a file-scope signal. Every write to `g_SelectedSceneNode` goes through a new `Editor::SetSelectedSceneNode(SceneNode*)` helper that sets the pointer and emits the signal. The four existing write sites (`EditorPicking.cpp:263,272`, `EditorWindows.cpp:429,455`, the clear-on New/Open/Shutdown in `Editor.cpp`) are routed through this helper.
- Selection visualization does NOT need to subscribe to the signal (it polls `g_SelectedSceneNode` every frame anyway, which is fine for rendering). The signal is for future consumers and for the spec contract. We still add it now because the proposal calls it out and it's cheap.
- Expose `Picking::IsPickInFlight()` to Lua as `Editor.IsPickInFlight()` (returns bool). `Editor.lua`'s camera-drag block adds `and not Editor.IsPickInFlight()` to its LMB-down gate (`examples/Editor.lua:318`).

**Why not just rely on the click-vs-drag gate (Decision 4):** the gate handles the common case but not the "true click immediately followed by a drag" race — the pick is in-flight (two-frame state machine) when the drag starts. `IsPickInFlight()` lets the camera-drag yield to the resolving pick. Belt and suspenders.

### Decision 7: `Pipeline::Execute` gains an optional `RenderTarget*` argument (default `nullptr`)

```cpp
void PrelightPipeline::Execute(SceneManager* scene, RenderTarget* target = nullptr);
```

When `target == nullptr`: bind the default framebuffer (current behavior, used by `WITH_EDITOR=OFF` and by any non-editor sample). When `target != nullptr`: bind `target->GetFramebuffer()` for all passes; the editor's Viewport window then `ImGui::Image`s `target->GetColorTexture()`.

The `RenderTarget` is owned by the editor (allocated lazily, recreated when the Viewport window's content-rect size changes). The camera aspect (Decision 1) is set before `Execute` is called, so the pipeline doesn't need to know about the viewport rect — it just renders into whatever target it's given with whatever camera matrices it's handed.

**Why an argument instead of a `Pipeline::SetRenderTarget` method:** the argument makes the per-frame intent explicit and avoids hidden state. A method would tempt callers to set it once and forget; the argument forces the editor to pass it every frame.

## Risks / Trade-offs

- **[Risk] Existing `imgui.ini` files will have no Viewport window entry → on first launch the Viewport window floats instead of docking center.** → Mitigation: `BuildDefaultLayout` (triggered by `Window → Reset Layout`) places it correctly. The first-launch experience is a floating Viewport window the user can drag into place once; acceptable for an editor. No automatic layout rebuild (consistent with the existing "don't auto-rebuild when imgui.ini exists" policy at `editor-shell` spec line 112-116).
- **[Risk] Render-to-texture adds one extra GPU pass (the `ImGui::Image` blit) per frame plus the RT clear.** → Mitigation: the blit is a single textured quad, negligible vs. the 3D scene. The picking FBO simultaneously shrinks (Decision 3), which is a net GPU-memory and bandwidth win.
- **[Risk] Camera aspect changing every frame as the user resizes the Viewport window could cause visual jitter in the scene frustum.** → Mitigation: this is expected behavior in every dockable-viewport editor (Unity/Unreal/Godot). Aspect is recomputed but the FOV is preserved; the only visible effect is the correct aspect for the new window shape, which is the desired outcome.
- **[Risk] Click-vs-drag threshold feels laggy (pick fires on release, not press).** → Mitigation: the threshold is 4 px and the release typically follows the press within tens of milliseconds for a true click. The two-frame pick state machine already added ~1 frame of latency; the release-deferral adds at most one more frame. Imperceptible to humans, and the payoff (no spurious picks on drag) is the whole point.
- **[Risk] `Light::EvaluateVolume()` stale-mesh fix (Decision 5) changes Node Properties behavior.** → Mitigation: `EvaluateVolume()` was always intended to be called on geometry-affecting edits; the existing code simply forgot to. The fix makes the Profiler's `LIGHT_BOUNDS` overlay correct too, so it's a strict improvement.
- **[Risk] Modifying `Pipeline::Execute`'s signature could break sample projects calling it directly.** → Mitigation: the argument defaults to `nullptr` (back-buffer), so existing call sites (`examples/Editor.lua:377` → `Pipeline.GetActive():Execute(octree)`) compile and behave unchanged. Non-editor samples are unaffected.
- **[Trade-off] Single Viewport window (no multi-viewport).** → Accepted for v1. Multi-viewport is a follow-up that the new RT infrastructure makes straightforward (allocate one RT per Viewport window).

## Migration Plan

1. **Build order**: implement the `RenderTarget` + `Pipeline::Execute` argument first (non-breaking, default back-buffer). Then the Viewport window + dockspace flag change. Then move gizmo + picking rects. Then click-vs-drag. Then selection viz + signal + Lua accessor.
2. **No on-disk format changes**: `imgui.ini` gains a Viewport window entry naturally; old `imgui.ini` files load fine (Viewport floats until Reset Layout). Scene files are untouched.
3. **Rollback**: if the dockable-viewport path has issues, `WITH_EDITOR=OFF` builds are unaffected. An `#if 0`-style guard around the `RenderTarget` branch in `Pipeline::Execute` restores back-buffer rendering without reverting the rest. The click-vs-drag and selection-viz changes are independently revertible.
4. **Testing**: manual verification covers (a) dock/undock/tab the Viewport window and confirm the scene + gizmo + pick follow it, (b) click-vs-drag on a node (click selects, drag doesn't), (c) click empty space deselects, (d) select a point light → wireframe sphere, select a spot light → wireframe cone, select a mesh → wireframe AABB, (e) `WITH_EDITOR=OFF` build still runs samples.

## Open Questions

- Should the Viewport window capture right-click for a context menu (e.g., "Frame selected", "Snap camera to node")? **Out of scope for v1**; the right-mouse button is currently unused over the viewport, so it's free for a follow-up.
- Should the selection wireframe color be user-configurable (Settings → Themes)? **Deferred**; `kSelectionColor` is a compile-time constant in v1.
- Should the camera-drag move to a modifier (e.g., Alt+LMB) instead of plain LMB, now that picking is click-only? **Deferred**; the click-vs-drag gate resolves the conflict without changing muscle memory. Revisit if users report that a no-movement click still occasionally rotates the camera by a sub-threshold amount.
