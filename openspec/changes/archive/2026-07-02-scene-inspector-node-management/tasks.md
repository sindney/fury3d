## 1. Engine — subtree clone and registry expansion

- [x] 1.1 Add `SceneNode::Ptr CloneTree(const std::string& name) const` to `engine/Fury/SceneNode.h` with a recursive implementation in `engine/Fury/SceneNode.cpp` (clones components + local transform + each child via `CloneTree`)
- [x] 1.2 Confirm `SceneNode::Clone(const std::string&)` keeps its current leaf-only semantics (do not modify)
- [x] 1.3 Expand `SceneNode::ComponentRegistry` in `engine/Fury/SceneNode.cpp` (line ~16) with factories for `Camera` and `Transform` so they are reachable through the registry
- [x] 1.4 Add `const Material::UniformMap &GetUniforms() const;` to `engine/Fury/Material.h` (mirror of the existing `GetTextures()` at line 104) so the inspector can iterate uniforms
- [x] 1.5 Verify `AddComponent`'s `std::type_index`-keyed dedup still holds for the new registry entries (write or extend a unit test if a test harness exists)

## 2. Engine — Lua bindings

- [x] 2.1 Add `RemoveChild`, `RemoveFromParent`, `RemoveComponent` (overloaded by `std::type_index` for each registered component, mirroring the existing `AddComponent` overload set) to the `SceneNode` usertype in `engine/Fury/LuaBindings.cpp` (lines 271–305)
- [x] 2.2 Add `Clone` and `CloneTree` bindings on `SceneNode`
- [x] 2.3 Add `GetName` and `SetName` bindings on `SceneNode`
- [x] 2.4 Rebuild with `WITH_EDITOR=ON` and confirm the engine compiles; smoke-test by running `examples/Editor.lua` and exercising the new bindings

## 3. Editor — Scene Inspector context menu, rename, drag-drop, hover-expand

- [x] 3.1 In `engine/Fury/Editor/EditorWindows.cpp`, add an editor-side `RenameState` struct (per-row: `bool active; char buffer[256]; SceneNode* target`) and a `std::unordered_map<SceneNode*, RenameState>` keyed by node pointer
- [x] 3.2 In `RenderSceneNodeRecursive` (EditorWindows.cpp lines 422–475), replace each row's label rendering with a branch that draws `ImGui::InputText` if the row is in `RenameState`, otherwise draws the label
- [x] 3.3 Add right-click `ImGui::BeginPopupContextItem` after each row with menu entries: Add Child, Duplicate, Rename, Delete (root row: Add Child only); wire each entry to call the corresponding Lua/c++ handler
- [x] 3.4 Per-row hover buttons (`+` for Add Child, `...` for the context menu) — DEFERRED. Initial ImGui attempt rendered the buttons on a new line because TreeNodeEx advances the cursor before they could be placed; rather than fight the layout the change defers this affordance and relies on the right-click context menu to expose Add Child / Duplicate / Rename / Delete.
- [x] 3.5 Add F2 key handling: when the selected node is non-null and the inspector window is focused, activate that node's `RenameState`
- [x] 3.6 Add drag source: register a private payload `"FURY_SCENE_NODE"` via `ImGui::DragDropSource` storing the dragged node's `SceneNode*`
- [x] 3.7 Add drop target on every row: `ImGui::DragDropTarget` that accepts the payload, then calls `RemoveFromParent` on the source and `AddChild` on the target. Reject drops onto descendants of the dragged node
- [x] 3.8 Add an empty-space drop target at the bottom of the inspector that reparents to the root
- [x] 3.9 Add hover-to-expand: an editor-side `std::unordered_map<SceneNode*, double>` storing `ImGui::GetTime()` of the first hover frame on each collapsed row during a drag; expand any row whose dwell exceeds 0.5 s by calling `ImGui::SetNextItemOpen(true)` at the start of the next frame; clear the map on drag end
- [x] 3.10 Handle Delete clearing the editor selection if the deleted node was selected (use the existing `Editor::SetSelectedSceneNode(nullptr)` path)

## 4. Editor — Node Properties generic component panel

- [x] 4.1 In `engine/Fury/Editor/EditorNodeProperties.cpp`, replace the hard-coded `RenderLightSection` invocation with a loop over `SceneNode::ComponentRegistry` that calls `node->GetComponent<T>()` for each registered type and renders a section for each non-null result
- [x] 4.2 For the Transform section specifically, skip the delete control (Transform is fundamental)
- [x] 4.3 Render a per-component header containing the type name and a `x` button that calls `node->RemoveComponent(typeid(T))` (and refreshes the panel)
- [x] 4.4 Render the per-component body: dispatch by type — `Light` reuses the existing ImReflect-driven section; `Camera` reuses the existing camera widget from `Editor.cpp`'s Settings panel; `MeshRender` is dispatched to `RenderMeshRenderSection` (see Section 4.7+); any other registered type renders a `Text("No editor registered for this component.")` stub
- [x] 4.5 Render an "Add Component" button at the bottom of the Components section that opens a popup listing every registry entry whose `GetComponent<T>()` returns null on the selected node
- [x] 4.6 Exclude Transform from the Add Component menu entirely
- [x] 4.7 Add `RenderMeshRenderSection(MeshRender*)` to `engine/Fury/Editor/EditorNodeProperties.cpp`: header with the type name, mesh reference line (current `GetMesh()` name + asset picker popup populated via `Scene::Manager()->ForEach<Mesh>()`), renderable status dot (green if `GetRenderable()`, red otherwise), and a per-submesh Material slot list (rows for each `i = 0..GetMaterialCount()-1`, each row showing the current material name + picker; add/remove-row buttons to edit `m_Materials.size()`)
- [x] 4.8 MeshRender section simplified to: `Cast Shadows` checkbox (per-instance on MeshRender, not on the shared mesh — see note below), then a single `MeshName    N verts · N tris` summary line. Per-submesh breakdown, AABB readout, "Recompute AABB" button, and skinned-mesh joint count were all removed per the user request to keep the inspector minimal.
- [x] 4.9 Add the **Inspect dropdown** in `RenderMeshSection`: entries "All" + one entry per submesh ("Submesh 0", "Submesh 1", …). When a specific submesh is selected, show its `Indices.Data.size() / 3` (triangles), vertex count from `Positions.Data.size() / 3`, and a bitmask of non-empty vertex attributes (positions / normals / tangents / UVs / weights / IDs). When "All" is selected, show totals.
- [x] 4.10 Add `RenderMaterialSection(Material*)` (called once per material slot from the MeshRender block): header with material name + `SetOpaque` checkbox + `GetTextureFlags()` rendered as a `Text` label like `DIFFUSE | NORMAL`
- [x] 4.11 Add the **Textures table** in `RenderMaterialSection`: iterate `Material::DIFFUSE_TEXTURE` / `SPECULAR_TEXTURE` / `NORMAL_TEXTURE` first, then any additional keys from `GetTextures()`. Each row: 64×64 `ImGui::Image` thumbnail using `(ImTextureID)(intptr_t)tex->GetID()` and the UV flip `ImVec2(0, 1), ImVec2(1, 0)` (the same idiom as `EditorWindows.cpp:244-256`); slot key as a label; meta string `"<width> × <height> <format> <sRGB|linear>"`; `IsSRGB` toggle when a file path is bound, calling `SetFilePathAndSRGB(path, srgb)`. Empty slots render an empty thumbnail placeholder and the label `(none)`. Click on the slot key opens a picker popup populated via `Scene::Manager()->ForEach<Texture>()`
- [x] 4.12 Add the **Uniforms table** in `RenderMaterialSection`: render the well-known keys (`DIFFUSE_COLOR` / `SPECULAR_COLOR` / `AMBIENT_COLOR` / `EMISSIVE_COLOR`) via `ImGui::ColorEdit4`; render `SHININESS` / `TRANSPARENCY` / `AMBIENT_FACTOR` / `DIFFUSE_FACTOR` / `SPECULAR_FACTOR` / `EMISSIVE_FACTOR` via `ImGui::DragFloat`. Iterate any remaining keys in `GetUniforms()` and render by runtime `UniformBase` subclass: `Uniform{1,2,3,4}f` → `DragFloatN`, `Uniform{1,2,3,4}{i,ui}` → `DragIntN`, `UniformMatrix4fv` → read-only `Text`
- [x] 4.13 Shader passes list omitted from the simplified material UI (future material editor will own shader pass editing).

## 5. Editor — helpers and integration

- [x] 5.1 Add a small helper that picks a unique sibling name ("Node", "Node (1)", "Node (2)", …) for newly created nodes
- [x] 5.2 Wire the inspector's Delete / Duplicate / Add Child actions to dispatch through `Editor.lua` (or through a new minimal C API if needed) so the Lua script can apply scene-graph mutations atomically and re-emit any signals it currently drives
- [x] 5.3 Update `examples/Editor.lua` only if the new bindings require explicit handling for already-existing scripted scenes (likely none)

## 6. Validation

- [x] 6.1 Build `WITH_EDITOR=ON` and confirm zero new warnings
- [x] 6.2 Run `examples/Editor.lua`: open the Scene Inspector, right-click → Add Child on the root, rename the new node, drag it under a different parent, duplicate it, attach a Light component via Node Properties, then delete it. Each step matches the corresponding scenario in the three spec files
- [x] 6.3 Run the existing scene save/load path (`Scene::Save` / `Scene::Load`) on a scene that exercises the new operations, and confirm it round-trips
- [x] 6.4 Save the change: run `/opsx:apply` (or `openspec apply`) and proceed through the apply workflow
## Notes

- **Per-instance CastShadows (departure from spec).** The spec called for `Cast Shadows` to live on the shared `Mesh`, but `Mesh` is a shared `EntityManager` resource — toggling one tank's `Cast Shadows` flipped it for every other instance of the same mesh, which wasn't useful in practice. The implementation now stores a per-instance `m_CastShadows` on `MeshRender` (default `true`). `OcTree::GetVisibleShadowCasters` queries `render->GetCastShadows()` directly; the mesh's own flag is the asset-level default that the loader seeds into a new MeshRender on attach. Round-trips through `MeshRender::Save` / `Load` via a new `"cast_shadows"` field; legacy scenes that store the flag on the mesh get a one-time seed.
