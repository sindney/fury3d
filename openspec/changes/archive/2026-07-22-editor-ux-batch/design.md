# Design: editor-ux-batch

## Context

Six independent workstreams batched into one change. Two are carried over from `add-animation-system` (tasks 21.7 and 20.8/17.3) with substantial prior lldb investigation; four are new editor-UX / correctness items. Engine unit convention is **1 unit = 1 cm** (`docs/ARCHITECTURE.md` §5.1, established by anim task 21.4) — a 1-m threshold is 100 engine units.

Current state facts the design builds on:

- Import UX is Lua-orchestrated: `examples/Editor.lua` `open_scene_at_path` / `import_scene_at_path` call `Importer.LoadScene(path)` (LuaBindings.cpp:808-853; `.fbx` goes through the FBX2glTF subprocess first), then `replace_active_scene` (open) or `Importer.MergeInto` (import). Native dialogs run inside the Lua `SceneIO` callbacks.
- `EditorConfirmDialog.h` exposes `Editor::RequestConfirmDialog(title, message, onResult(bool))` — a queued Yes/No modal rendered from `Editor::Tick`. C++ only today; no Lua binding.
- Import flags: `g_ImportFlags` + `Editor::SetImportFlag/GetImportFlag` (Editor.cpp:793-806), surfaced in Settings → Import (EditorWindows.cpp:121-128, pattern: `auto_default_sun`), already bound to Lua (Editor.lua:392).
- Content Browser: `RenderContentBrowserWindow` (EditorWindows.cpp:1588) renders tiles from `CollectTiles` (EditorWindows.cpp:1098), which enumerates **Mesh, Material, Texture, AnimationClip**; no filtering exists.
- Profiler window: tabs `FPS` / `GBuffer` / `Shadows` (EditorWindows.cpp:544-552); the FPS tab also carries memory, drawcall/tri/mesh/light counts, Debug Overlays, and the LOD histogram.
- Skinned bounds: `MeshRender::OnAttaching` copies `mesh->GetAABB()` once into `SceneNode::SetModelAABB` (MeshRender.cpp:231-238). `Mesh::CalculateAABB()` already has a skinned branch that blends every vertex by `Joint::GetFinalMatrix()` (Mesh.cpp:477-508), but nothing calls it after pose updates. `SetModelAABB` recomputes local+world AABBs (SceneNode.cpp:235-246).

## Goals / Non-Goals

**Goals:**

- Fix GUI scrolling after native (NFD) dialogs close, for good, with lldb-verified root cause (merges anim 20.8 + 17.3).
- Ship a toggleable reference grid in the editor viewport (unparks anim 21.7).
- Catch metre-authored glTF/FBX imports (bounds < 1 m) and offer one-click auto-scale; user-toggleable in Settings → Import.
- Type filter + fuzzy name search in the Content Browser.
- Rename Profiler `FPS` tab → `Perf`.
- Per-frame skinned-mesh AABB from the current joint pose.

**Non-Goals:**

- Persisting import scale decisions into asset metadata / an import-settings file per asset.
- Auto-scaling native `.json`/`.bin` scenes (already engine units; detection still runs but should never trigger — if it does, the dialog is informative, not wrong).
- Grid customization (cell size, colors, axis highlighting) beyond a sensible default + show/hide toggle.
- O(joints) approximate bounds or GPU-skinning-based bounds; exact CPU blend only (see Decisions 6).
- Fixing `SceneNode::FindChildRecursively` hash bug (noted in anim 20.7 as follow-up; unrelated).

## Decisions

### 1. Scroll bug — re-seed `MousePos` on focus regain, from the 1× logical source

**Root cause (lldb, 2026-07-16, carried from anim 20.8):** NFD dialog → SFML `FocusLost` → `io.AddFocusEvent(false)` (imgui_impl_sfml3.cpp:201) → ImGui `ClearInputMouse` sets `MousePos = (-FLT_MAX, -FLT_MAX)`. If the mouse doesn't move after the dialog closes, no `MouseMoved` fires → `HoveredWindow = NULL` → `UpdateMouseWheel` early-returns → no scroll anywhere. Simultaneously the Lua wheel handler (Editor.lua:503) steals the wheel for camera speed because `WantCaptureMouse=false`. Restore happens when any `MouseMoved` arrives (minimize/restore, window switch, title-bar click).

**Fix:** in the SFML backend's `FocusGained` handler, re-seed `io.MousePos` (via `io.AddMousePosEvent`) with the current cursor position in the SAME logical (1×) coordinate space as `MouseMoved` events. `sf::Mouse::getPosition(window)` **cannot** be used on macOS Retina — SFML's `InputImpl.mm:189-203` multiplies by `[view displayScaleFactor]` (2× on Retina) while `window->getSize()` and the `MouseMoved` path are 1× logical. Options, in order of preference:

1. Platform shim in the backend (macOS): reach the `NSView` from the `sf::Window` system handle and call the same 1× helper SFML's mouse-move path uses (`cursorPositionFromEvent:nil` logic), guarded `#ifdef __APPLE__`; other platforms use `sf::Mouse::getPosition` (verify per-platform units first).
2. Patch vendored SFML `InputImpl.mm` to return logical coords from `getMousePosition(relativeTo:)` (drop the `* scale`) — only after verifying `Window::getSize()` is logical on macOS; must be a minimal annotated patch.
3. Fallback: synthesize a `MouseMoved` event on `FocusGained`.

Do NOT just stop calling `AddFocusEvent(false)` — already tried and reverted: the retained `MousePos` is the stale pre-dialog position (user moves mouse during the dialog), which leaves `HoveredWindow` wrong in a different way.

**Verification (lldb, active debugging required):** re-create the probe scripts (prior ones at `/tmp/probe5.py` / `probe6.py` / `probe7.py` — state-at-click, wheel-handler, window-list-vs-MousePos; rewrite if gone): BP on `imgui_impl_sfml3.cpp`'s wheel handler after File→Open, print `io.MousePos`, `io.DisplaySize`, `g.HoveredWindow`. Fixed = `MousePos` inside `DisplaySize` and `HoveredWindow` non-null immediately after dialog close, with no mouse movement. Also re-run the anim 17.3 property-panel repro (same root cause) and grep for other `sf::Mouse::getPosition` callers to gauge the latent 2× bug's blast radius.

### 2. Reference grid — screen-space grid via depth reconstruction in a dedicated pass

Three approaches failed and were deleted (line rendering invisible after the composite pass corrupts depth; depth-sampling shader invisible; scene-node quad never entered the pipeline's render query). Decision: implement the standard editor infinite-grid as a **fullscreen triangle pass after scene color is final, before gizmo/editor UI**: bind the resolved depth texture, reconstruct world position per fragment, emit grid lines with `fwidth`-based anti-aliasing, alpha-blend over the scene, keep depth writes off. This is transform-independent, infinite, and immune to the scene-graph query problem that killed approach 3.

Two-stage plan: (a) lldb/inspect why the depth-sampling attempt saw no usable depth (wrong binding? pre-resolve? reversed-Z?) — this decides whether the pass reads the existing depth attachment or needs a copied/resolved depth texture; (b) implement the pass wired like an existing debug overlay, gated by a `g_ShowGrid` flag with a Settings → Editor checkbox and a View menu item (both were removed with the parked code; re-add).

**Alternative kept in reserve:** debug why the scene-node quad wasn't picked up by the SceneManager render query (check `MeshRender` registration + pipeline query filters) — only if depth reconstruction proves impossible with the current pipeline's depth lifetime.

### 3. Import unit-scale — Lua-side detection, C++ confirm dialog, powers-of-100 escalation

**Where:** in `open_scene_at_path` / `import_scene_at_path` (Editor.lua) immediately after `Importer.LoadScene` succeeds — the editor UX layer owns the prompt. Rejected: inside the C++ importer/`LoadScene` binding, because `Importer.LoadScene` is a runtime API also used headless (CLI, tests, `render-mesh`) where a modal must never appear.

**New bindings (LuaBindings.cpp):**

- `Scene:ComputeWorldAABB()` → `min, max` (Vector4 pair) or nil when the scene has no finite bounds (empty / lights-only). C++ helper unions child world AABBs from the scene root.
- `Editor.RequestConfirmDialog(title, message, callback)` → thin wrapper over the existing `Editor::RequestConfirmDialog` queue helper. Yes → `callback(true)`, No/Esc → `callback(false)`.
- `Editor.GetImportFlag(name)` already exists via the flag map (verify; `SetImportFlag` is used from Editor.lua:392).

**Detection + scale math:** `maxDim = max(size.x, size.y, size.z)`. If `maxDim < 100` (1 m): `scale = 100; while (maxDim * scale < 100) scale *= 100;` — powers of 100 keep the metric relationship (m→cm, then dm→cm, mm→cm…). Dialog message shows measured size and the computed factor: e.g. *"Imported scene bounds are 1.55 units across (~1.6 cm) — likely authored in metres. Scale root nodes by 100×?"* or by 10000× for a mm-scale asset. Skip when: flag disabled, AABB invalid/infinite, or scene has no meshes.

**Application:** multiply `SetLocalScale` on the imported scene's root node(s) — for Open, before `replace_active_scene`; for Import, on the nodes `MergeInto` added (or the imported scene's roots before merging). This matches the established skinned-mesh pattern (100× on the SceneNode; bind-pose math stays consistent in unscaled space below — do NOT bake scale into vertices or joints).

**Setting:** `auto_scale_detect` import flag, default `true`, checkbox in Settings → Import next to `auto_default_sun`, read from Lua before prompting.

### 4. Content Browser filters — toolbar row above the grid

Add a toolbar row in `RenderContentBrowserWindow` before the tile loop: a type combo (`All`, `Mesh`, `Material`, `Texture`, `AnimationClip` — exactly the `CollectTiles` set) and an `InputTextWithHint("Search…")`. Fuzzy match = case-insensitive subsequence (e.g. `spz` matches `Sponza`), a small local helper — no ranking needed (grid stays name-sorted). Filters compose (type AND text). Persist neither across sessions (window-local statics; consistent with other editor UI state).

### 5. Profiler tab rename — `FPS` → `Perf`

`BeginTabItem("FPS")` (EditorWindows.cpp:544) → `"Perf"`, plus the window comment (EditorWindows.cpp:151) and any overlay label that reads as a tab name. The `FPS %d` overlay text (EditorWindows.cpp:197) stays — it labels a number, not the tab. Spec delta updates the tab-order requirement in `editor-shell`.

### 6. Viewport top toolbar — gizmo controls left, Debug Overlays combo right

Add a one-line toolbar row at the top of `RenderViewportWindow` (before the scene `ImGui::Image`), reducing the scene content rect by the bar height. Layout: gizmo controls flush-left, a spring spacer (`ImGui::SetCursorPosX` to right-align), then the Debug Overlays combo flush-right. Confirmed with the user: overlays on the right, not stacked left.

**Moving in, from Node Properties** (`RenderGizmoSection`, EditorNodeProperties.cpp:69-106): the Translate/Rotate/Scale radios + `Snap` checkbox. The three snap-step `DragFloat`s don't fit a bar → they collapse into a `Snap▾` popup (small button next to the Snap checkbox opening a `BeginPopup` with the three drags). The whole "Gizmo" `CollapsingHeader` is removed from Node Properties; state globals (`g_GizmoOp`, `g_SnapEnabled`, `g_SnapTranslate/Rotate/Scale`) stay where they are (EditorGizmo.cpp) — only the UI surface moves. Persistence via `MarkIniSettingsDirty` is preserved. The Node section's Local/World readout radio is node-bound state and STAYS in Node Properties.

**Moving in, from the Profiler Perf tab** (EditorWindows.cpp:213-270): the `Debug Overlays` multi-select combo with its five static booleans and the per-frame `Pipeline::SetSwitch` block. The state + SetSwitch calls move wholesale next to the combo in the toolbar; the Perf tab keeps the FPS graph, memory/drawcall counters, the LOD histogram section, and the OcTree Spatial readout — only the combo leaves. Side effect to note: the SetSwitch block currently runs only while the Perf tab is visible; in the toolbar it runs whenever the Viewport is visible (near-always), which makes overlay state more predictable, not less.

Rationale: mode/overlay toggles are viewport-scoped decisions — putting them on the viewport matches Unity/UE muscle memory and shortens the mouse path. Rejected alternative: keep both in place and add a second viewport surface (state duplication, drift).

### 7. Settings window default-collapse

One-line behavior change: remove `ImGuiTreeNodeFlags_DefaultOpen` from the Import `CollapsingHeader` (EditorWindows.cpp:121); Editor keeps `DefaultOpen`, Engine is already collapsed. Sections the user opens remain open for the session via ImGui's normal tree state (persisted in imgui.ini per-window settings as usual).

### 8. Skinned mesh bounds — exact CPU recompute, gated on pose change

Hook where the Animator finishes applying the pose for the frame (the tick that writes joint-node TRS — locate precisely during implementation; `Animator::Display` only sets render-alpha, the pose itself is written in the update pass). For every `MeshRender` on the animated node subtree whose mesh `IsSkinnedMesh()`: `mesh->CalculateAABB()` (skinned branch already blends vertices by `Joint::GetFinalMatrix()`), then `owner->SetModelAABB(mesh->GetAABB())`. Gate on "a state was active and advanced this frame" so a paused/stopped Animator costs nothing.

Rejected for v1: O(joints) approximation (union of joint world positions + per-joint influence radius precomputed at import) — cheaper but looser; character meshes are a few thousand triangles, so exact CPU blend is affordable. Revisit if profiling on a heavy skin shows cost.

Must verify: the octree observes the changed world AABB (re-insert / dirty path when `SetModelAABB` lands on an octree-member node — trace `OcTree` update-on-move; if bounds changes don't propagate, add the missing invalidation). Visual check: Profiler → Perf → Debug Overlays → `Draw Mesh Bounds` on the Fox mid-walk-cycle — box tracks the deer, not the bind pose.

## Risks / Trade-offs

- [Scroll fix touches vendored SFML/backend and focus semantics] → minimal patch, `#ifdef __APPLE__` where possible, verify non-Retina + Windows/Linux paths unchanged; lldb before/after evidence required.
- [Per-frame CPU skinning for bounds is O(verts × 4 joint blends)] → gate on pose-changed; measure on Fox; fall back to joint-union approximation if hot.
- [Auto-scale dialog annoys on legitimately tiny assets (a 5-cm figurine)] → Yes/No is non-destructive and defaults are sane; the Settings toggle disables detection entirely; threshold (1 m) + powers-of-100 keep prompts rare and factors sensible.
- [Scaling import roots interacts with skinned meshes / lights] → scale the root only (never bake into vertices/joints — the established convention); verify Fox walk + Sponza (already cm-scale, must NOT prompt) + a metres glTF (must prompt, 100×).
- [Grid pass reads depth that the pipeline may not keep bound post-composite] → stage (a) investigation exists precisely for this; fallback is a depth copy in the pass setup.
- [Skinned AABB currently also unions bind-pose positions (static fallback runs after the skinned branch)] → conservative superset; acceptable — bounds never under-cover.

## Migration Plan

No data migration. Land in this order: (1) Perf rename + Content Browser filters + Settings flag (pure UI, no risk), (2) import unit-scale, (3) skinned bounds, (4) grid, (5) scroll fix — the last two are the lldb-heavy items and benefit from the batch's earlier wins being in.

## Open Questions

- ~~Does `window->getSize()` on macOS return logical or physical pixels?~~ **RESOLVED (lldb, 2026-07-17):** logical — `getSize()=(1280,720)` matches `io.DisplaySize` and the MouseMoved coordinate space; `backingScaleFactor=2` (Retina) but `[view displayScaleFactor]=1` for SFML's non-layer-backed GL view, so `sf::Mouse::getPosition(window)` also returns logical 1× coords on this configuration. That made the simple re-poll fix viable without an SFML patch or Cocoa shim (implemented in `imgui_impl_sfml3.cpp` FocusGained; lldb-verified `io.MousePos` re-seeded to real cursor position after a focus cycle).
- Where exactly does the animation update pass write joint-node TRS (the correct per-frame bounds hook)? Confirm against `AnimationState`/`AnimationPlayer` during implementation.
- Grid: is the depth attachment sampleable after the lighting composite, or does the pass need a resolve/copy? (Stage (a) of Decision 2.)
