## Why

The Node Properties panel from `add-node-properties-panel-and-save` lets users edit transforms numerically, but tweaking 3D position / rotation / scale by typing numbers is tedious — a 3D viewport begs for direct manipulation. Beyond that, the editor has no way to select a node by clicking it in the viewport: today the only entry point is the Scene Inspector tree, which doesn't scale once a scene has hundreds of nodes. This change closes both gaps with a viewport gizmo (ImGuizmo) and a click-to-pick path backed by an offscreen ID-render pass.

## What Changes

- Vendor [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) sources at `engine/ThirdParty/ImGuizmo/`. Compile only into the editor target (`WITH_EDITOR=ON`); engine public headers stay untouched. The 1.92.5-WIP `ImGuizmo.cpp` / `ImGuizmo.h` from `/Users/sindney/Documents/git/furyengine/ImGuizmo/src/` are the units we vendor.
- Add a **Picking pass** (editor-only) that renders renderable scene nodes into an offscreen `R32UI` texture sized to the viewport, with each pixel storing the node's ephemeral picking ID. The pass runs lazily — only on the frames where the editor needs to resolve a click — not every frame.
- Add a **node ↔ ID table** (`std::vector<std::weak_ptr<SceneNode>>`) rebuilt by the editor each pick-frame. Picking IDs are array indices + 1 (0 reserved for "no hit"). The table lives in editor TUs only.
- Wire **left-click in the viewport** (when the gizmo isn't hovered and ImGui doesn't want the mouse) to schedule a picking-pass + readback. The readback happens on the next frame after the picking pass renders, then maps the read ID back to a `SceneNode*` and writes `g_SelectedSceneNode`.
- Add **ImGuizmo to the editor's Tick** rendering. When a node is selected, draw a TRS gizmo on its world transform inside an invisible full-viewport ImGui window (matching ImGuizmo's required setup). On drag-end, decompose the gizmo's resulting world matrix and write the local-space delta back to the SceneNode (`SetLocalPosition` / `SetLocalRoattion` / `SetLocalScale` + `Recompose(false)`).
- Add **gizmo mode controls** at the top of the Node Properties panel: a 3-button row (Translate / Rotate / Scale) and a Local/World toggle. State lives in editor C++. No keyboard shortcuts in v1 — explicit user choice.
- Add **gizmo snap toggles** in the Node Properties panel: snap-step text inputs for translate (default 1.0), rotate (default 15°), scale (default 0.1) and a single "Snap" checkbox that enables snapping for the active mode. Snap state persists via the existing `imgui.ini` settings handler.
- Selection sync: clicking in the viewport selects in the Scene Inspector (via shared `g_SelectedSceneNode`); clicking in the Scene Inspector still drives the gizmo's rendered position.

## Capabilities

### New Capabilities
- `viewport-picking`: editor-only render-to-ID pass + click-to-select pipeline. Owns the ID-table lifecycle, the offscreen RT, the readback queue, and the click→ID→node resolution.

### Modified Capabilities
- `editor-shell`: extends the Node Properties window with gizmo-mode / snap controls, adds the in-viewport gizmo render to `Editor::Tick`, adds the click-handler that calls into `viewport-picking`. Documents the selection-sync contract between picking and Scene Inspector.

## Impact

- **New vendored dependency**: ImGuizmo at `engine/ThirdParty/ImGuizmo/ImGuizmo.{cpp,h}`. ~3000 LOC C++ unit, no external deps beyond ImGui (already vendored). MIT licensed. Adds one `.cpp` to the editor-only build.
- **New editor source**: `engine/Fury/Editor/EditorPicking.{hpp,cpp}` (offscreen RT lifecycle, ID pass shader, readback queue, ID↔node table, click resolver).
- **New editor source**: `engine/Fury/Editor/EditorGizmo.cpp` (ImGuizmo per-frame setup, mode/snap state, world↔local matrix transforms, drag-commit logic).
- **Editor source — modified**: `engine/Fury/Editor/Editor.{h,cpp}` (gizmo + picking integration in `Tick`; new public `Editor::SetGizmoMode/SetGizmoSpace/SetSnapEnabled` for testability/Lua); `engine/Fury/Editor/EditorNodeProperties.cpp` (gizmo-mode buttons + snap UI at top of panel).
- **Engine source — unchanged public API**: SceneNode / Light / Pipeline / RenderUtil headers do NOT change. The picking pass uses `Pipeline::Active->GetCurrentCamera()` and walks `Scene::Active->GetSceneManager()->GetRenderableNodes()` (the public surface today). It does not need new render-side hooks.
- **CMake**: `engine/CMakeLists.txt` adds `engine/ThirdParty/ImGuizmo/ImGuizmo.cpp` to the editor-only source list and the include path. `WITH_EDITOR=OFF` builds remain identical.
- **GPU resources**: one new offscreen FBO (R32UI color attachment + depth24 attachment) sized to the engine's drawable size. Re-created on resize; otherwise persisted. ~16 MB at 4K (single 32-bit channel + depth).
- **Performance**: ID pass + readback runs only on click frames (~once per click). Steady-state cost is zero. Click latency: ID pass + 1-frame `glReadPixels` stall ≈ 1–3ms on the tank scene, perceptually instant.
- **Lua surface**: optional `Editor.SetGizmoMode("translate"|"rotate"|"scale")`, `Editor.SetGizmoSpace("local"|"world")`, `Editor.SetSnapEnabled(bool)` exposed for power-user scripts. No required Lua changes — Editor.lua keeps working as-is.
- **Layering**: ImGuizmo draws via ImGui's draw lists, so its output goes through `Gui::Render` together with the rest of the editor UI — no new GL state to manage outside the existing flow.
- **No data-format changes** — picking IDs are ephemeral (rebuilt per click); not persisted to scene files. The gizmo writes through the same `SceneNode::SetLocal*` setters the Node Properties panel already uses, so saves stay correct without changes.
- **Migration**: lands on top of `add-node-properties-panel-and-save` (which provides the panel, selection state, and current-scene tracking). Without that change merged first, the gizmo has no panel to put its mode buttons in. This change therefore depends on `add-node-properties-panel-and-save` being archived first.
