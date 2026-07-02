## Why

The Scene Inspector today (`EditorWindows.cpp::RenderSceneInspectorWindow`) is a read-only tree: nodes are listed, expanded via arrow / double-click, and click-selected, but there is no way to mutate the scene from the panel itself. Users currently have to script every hierarchy mutation — add child, rename, duplicate, reorder, attach a component — in `Editor.lua` because none of those operations are surfaced in the editor UI. The Node Properties panel likewise lists only the `Light` component and offers no way to add a new component to the selected node. This change closes the loop so the Scene Inspector and Node Properties panel become the primary, interactive authoring surface for a scene's hierarchy and component composition.

## What Changes

- **Right-click context menu on every node row** in the Scene Inspector with: Rename, Duplicate (subtree), Delete, Add Child. Rename happens in-place via an `InputText` triggered on menu pick or `F2`.
- **Per-row hover affordances** — a small "+" and "..." buttons that appear on hover to make Add Child and the context menu discoverable without forcing a right-click.
- **Drag-and-drop reparenting** — nodes become drag sources, every node row becomes a drop target. A drop onto a node reparents the dragged subtree under it. A drop on empty space in the inspector reparents to the root. Invalid drops (dropping a node onto one of its own descendants) are rejected.
- **Hover-to-expand** — while dragging, hovering over a collapsed node for a short dwell-time expands it so the user can drop onto a deeper descendant without first manually expanding.
- **In-place rename** — `F2` or double-click on a non-arrow, non-checkbox area opens an editable text field bound to `SceneNode::SetName`. `Esc` cancels, `Enter` commits.
- **Node Properties — component list + Add Component button** at the bottom of the panel. Each attached component renders as a header with a `x` delete button (the `Transform` component is fundamental and cannot be removed). The Add Component menu enumerates every registry-registered component and **deduplicates**: if the node already has a `Light`, the menu does not offer `Light`.
- **Subtree duplication** — extend `SceneNode::Clone` (currently leaf-only) so Duplicate produces a deep copy including descendants and their components. The original node name gets a `(copy)` suffix.
- **Component registry expansion** — register factories for every component the engine exposes so the Add Component menu can offer them. Currently only `MeshRender` and `Light` are registered; `Camera` and `Transform` are exposed in Lua but absent from the registry.
- **Node Properties — MeshRender section** for nodes that carry a `MeshRender` component. Renders the mesh reference, mesh submesh breakdown with per-submesh vertex/index/triangle counts (the engine has no real LODs — the "section dropdown" maps onto `SubMesh` index, picking which submesh to inspect), mesh AABB and skinned-mesh metadata, the list of material slots, and per-material sub-panels that surface texture thumbnails (`ImGui::Image` of the texture's GL handle), known-key uniforms categorized as `*_COLOR` (color picker), `*_FACTOR` / `SHININESS` / `TRANSPARENCY` (sliders), and any custom uniform keys (rendered by runtime type), plus a read-only list of shader passes by name.

## Capabilities

### New Capabilities

- `scene-inspector-node-operations`: Right-click context menu, in-place rename, duplicate, delete, drag-and-drop reparenting with hover-to-expand for collapsed targets.
- `scene-inspector-component-panel`: Per-component headers with delete affordances, Add Component menu with registry-driven enumeration and deduplication (with the `Transform` component protected from removal), and detailed MeshRender / Mesh / Material sections (submesh dropdown, AABB, texture thumbnails with meta, known-key categorized uniforms, read-only shader pass list).
- `scene-node-subtree-clone`: Engine-level deep-clone of a node and its descendants, including attached components, so Duplicate in the inspector yields a faithful copy.

### Modified Capabilities

- `editor-shell`: Add new editor-side affordances (drag-drop, context menus, input fields) to the ImGui inspector. This is an additive UI surface and does not change existing dock layout / menu-bar requirements, so no delta spec is required — the editor shell spec covers the implementation of the new window chrome generically.
- `scene-editor`: New Lua bindings on `SceneNode` (remove/clone/rename) plus a documented hook in `Editor.lua` so the inspector's actions drive scene mutations through Lua. Again additive — no requirement changes.

## Impact

- **Engine / Scene graph** — `engine/Fury/SceneNode.{h,cpp}`: extend `Clone` to recurse, add `CloneTree` helper, ensure `SetName` is reachable from the Lua binding surface, and document that `AddComponent` already deduplicates by `std::type_index` so the inspector-side dedup is policy enforcement, not a workaround.
- **Component registry** — `engine/Fury/SceneNode.cpp` (line ~16, `ComponentRegistry` initializer): add factories for `Camera` and `Transform` so they can be added through the inspector.
- **Editor / ImGui** — `engine/Fury/Editor/EditorWindows.cpp` (`RenderSceneInspectorWindow` at line 477 and the recursive helpers `RenderSceneNodeRecursive` / `RenderTreeFromProvider` at lines 422–475): introduce per-row hover buttons, context menu, drag source/target payloads, hover-dwell auto-expand, and inline rename `InputText`. First uses of `ImGui::BeginPopupContext*` and `ImGui::DragDrop*` in the editor — kept localized so they can be reused for the Content Browser → Viewport drag-drop.
- **Editor / Node Properties** — `engine/Fury/Editor/EditorNodeProperties.cpp` (`RenderNodePropertiesWindow` at line 256): replace the bespoke `RenderLightSection` block with a generic "Components" loop that lists every component on the selected node with a delete button, plus an Add Component menu at the bottom. Add `RenderMeshRenderSection(MeshRender*)` plus helpers `RenderMeshSection(Mesh*)` and `RenderMaterialSection(Material*)` implementing the three-level layout: mesh reference + renderable status + material slot list at the root, mesh submesh dropdown with per-submesh vertex/index/triangle counts and AABB at the mesh level, and per-material texture thumbnail rows, known-key categorized uniforms, and read-only shader pass list at the material level.
- **Lua bindings** — `engine/Fury/LuaBindings.cpp` (`SceneNode` usertype, lines 271–305): add `RemoveChild`, `RemoveFromParent`, `RemoveComponent`, `Clone`, `CloneTree`, `GetName`, `SetName`.
- **Material accessor** — `engine/Fury/Material.h` (mirror of existing `GetTextures`): add `const UniformMap &GetUniforms() const;` so the inspector can iterate uniforms.
- **Editor script** — `examples/Editor.lua`: register a `SceneTreeProvider` if needed, wire the new bindings to whichever inspector callbacks the editor exposes, and centralize the action handlers (delete / duplicate / rename / reparent / add-component) so policy remains in Lua.
- **Documentation** — `openspec/specs/scene-editor/spec.md`: add a brief note on the new node-mutation bindings and the Add Component menu contract.
- **No breaking API changes.** All additions; existing scripts and scenes continue to work.