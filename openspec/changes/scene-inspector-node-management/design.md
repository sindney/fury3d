## Context

The Scene Inspector today (`engine/Fury/Editor/EditorWindows.cpp::RenderSceneInspectorWindow`, lines 422–477) is read-only: nodes are listed and expanded via arrow / double-click, click-selects set the editor's selected node, and nothing else happens. All scene mutations — add child, delete, duplicate, rename, attach component — currently require editing `examples/Editor.lua` or the scene JSON. The Node Properties panel (`engine/Fury/Editor/EditorNodeProperties.cpp::RenderNodePropertiesWindow`, line 256) hard-codes a `RenderLightSection` for the `Light` component and offers no generic add/remove affordance.

The engine's `SceneNode` (`engine/Fury/SceneNode.{h,cpp}`) already provides:

- Hierarchy API: `AddChild`, `RemoveChild`, `RemoveChildAt`, `RemoveFromParent`, `Replace` (lines 401–465).
- `Ptr Clone(const std::string &name) const` (line 177) — **leaf-only**: copies local TRS + components but does NOT recurse into `m_Childs`.
- `bool AddComponent(...)` (line 569) keyed by `std::type_index` — duplicates already collapse at the engine layer; the inspector just needs to expose the menu policy that mirrors this.
- `static std::unordered_map<std::string, std::function<...>> ComponentRegistry` (line 35, defined at SceneNode.cpp line 16) — currently only registers `MeshRender` and `Light`, even though `Camera` and `Transform` are exposed in Lua.

GUI stack is ImGui v1.92.8-docking + ImGuizmo + ImReflect, vendored at `engine/ThirdParty/ImGui/`. The editor exposes its full ImGui surface directly — no editor-side widget abstraction. Selection is centralized at `Editor::SetSelectedSceneNode` (Editor.h) and broadcasts through `OnSelectionChanged`.

Lua bindings (`engine/Fury/LuaBindings.cpp` lines 271–305) are missing `RemoveChild`, `RemoveFromParent`, `RemoveComponent`, `Clone`, `GetName`, `SetName`. The editor's mutation policy therefore lives partially in C++ (selection) and partially in Lua (scene construction in `Editor.lua`), which is the pattern this change preserves.

## Goals / Non-Goals

**Goals:**

- Make the Scene Inspector the authoritative interactive surface for hierarchy manipulation.
- Make the Node Properties panel generic over components with add/remove affordances.
- Preserve the existing division of labor: C++ editor owns UI, Lua owns mutation policy.
- Keep `SceneNode::AddComponent`'s type-keyed dedup as the engine-of-record, with the inspector's menu just reflecting that.

**Non-Goals:**

- Undo/redo stack — separate proposal.
- Multi-selection / box-select in the Scene Inspector.
- Drag-drop between Scene Inspector and Viewport (Content Browser → Viewport is a future change).
- Keyboard navigation (Up/Down to move selected node, Tab to indent, etc.).
- Reordering siblings within the same parent via drag-drop (only reparenting across parents).
- Persistence of inspector state (which rows are expanded) across sessions.

## Decisions

### 1. Engine-side `CloneTree` helper alongside `Clone`

`SceneNode::Clone` is leaf-only today. Adding a separate `SceneNode::Ptr CloneTree(const std::string& name) const` that recurses into `m_Childs` (calling `CloneTree` on each child and `AddChild`-ing the result under the new node) is the cleanest path:

- **No breaking change** to `Clone` — scripts that already depend on the leaf-only behavior continue to work.
- New Lua binding `CloneTree` is registered alongside `Clone`.
- The inspector's "Duplicate" command calls `CloneTree`, suffixed with `(copy)` (or `(copy N)` on collision).

**Alternative considered:** extending `Clone` to recurse. Rejected — silently changes the semantics of every existing caller, including the JSON deserializer's potential use of `Clone` for templating.

### 2. Components stored via `std::unordered_map<std::type_index, ...>` already deduplicates

`SceneNode::m_Components` is keyed by `std::type_index`, so a second `AddComponent<Light>()` on a node already returns `false` and replaces nothing. The inspector's "Add Component" menu must mirror this by **filtering** the registry list at render time: for each registered type, query `GetComponent<T>()` on the selected node and hide the menu entry if it returns non-null.

**Transform is non-removable.** The `Transform` component is created implicitly when `SceneNode::Create` runs and lives for the lifetime of the node. The properties panel must hide the `x` button on the Transform header and surface this in the menu (or simply not register Transform in the "Add Component" menu — only register `Camera`, `Light`, `MeshRender`).

### 3. Drag-and-drop payload uses raw `SceneNode*` pointer in an ImGui `const char*` payload

ImGui's `DragDropPayload` is a `(type, data, size)` triple. The editor registers a private payload type `"FURY_SCENE_NODE"` and stores the dragged node's `SceneNode*`. Pointer-sized payloads are safe because both source and target live in the same process / same ImGui context.

**Alternative considered:** storing the node's name string and re-resolving via `FindChildRecursively`. Rejected — name collisions are legal (no sibling uniqueness enforcement), so pointer identity is the only reliable identifier.

### 4. Hover-to-expand uses a small per-node timer tracked in a hash map

`ImGui::SetDragDropPayload` on the source side fires `DragDropSource` events continuously while held. On the target side, each `DragDropTarget` block inspects whether the hovered row is collapsed (`ImGui::TreeNodeBehavior` returns `IsOpened() == false`). If so, it stores the current `ImGui::GetTime()` in a `std::unordered_map<SceneNode*, double>` keyed by the hover target. Each frame, any entry whose elapsed time exceeds `kHoverExpandDelay` (default 0.5 s) triggers `ImGui::SetNextItemOpen(true)` for that node at the start of the next frame. The map is cleared whenever a drag ends or the panel is rebuilt.

**Alternative considered:** ImGui's `ImGuiTreeNodeFlags_OpenOnDoubleClick` + a synthetic double-click. Rejected — conflates user intent (double-click to open) with drag intent.

### 5. Rename state lives in the inspector (not the SceneNode)

The inspector maintains a small per-row struct `RenameState { bool active; std::string buffer; SceneNode* target; }` stored in a `std::unordered_map<SceneNode*, RenameState>`. While `active` is true, the row's label is replaced with `InputText` seeded from the buffer; `Enter` commits (`SetName` + close), `Esc` cancels (close without commit), focus loss commits. Only one rename is active at a time (F2 on a different row cancels the previous).

This keeps `SceneNode` purely about scene semantics — it has `SetName` / `GetName` already, and doesn't need to know about editor state.

### 6. Lua binding additions — minimal, no new module

Add to the existing `SceneNode` usertype at `engine/Fury/LuaBindings.cpp` lines 271–305:

- `RemoveChild`, `RemoveFromParent`, `RemoveComponent` (overloaded by `std::type_index` of each known component, mirroring `AddComponent`'s existing overload set).
- `Clone` (leaf-only — preserves the current behavior for scripts that already use it).
- `CloneTree` (deep clone with children).
- `GetName`, `SetName`.

No new Editor API surface is needed — `Editor.lua` already constructs/manipulates the scene and can now do so in response to inspector actions. The inspector itself will gain **editor-side state** (drag payload, hover timer, rename state, context menu state) but does not need a new public C API.

### 7. Generic component section in Node Properties

Replace `RenderLightSection` with a loop:

```cpp
for each registered component type in ComponentRegistry:
    if (auto c = node->GetComponent<T>(); c):
        RenderComponentHeader(type_name, c);  // collapsible + x button
        RenderComponentBody(c);                // ImReflect-driven or per-type dispatch
```

The "Add Component" button sits below the loop and opens a `BeginCombo` / popup menu listing all registry entries except those whose `GetComponent<T>()` is non-null on the selected node. Transform is excluded from the menu entirely (Transform cannot be removed, so the menu never needs to add it; nodes already have it).

`RenderComponentBody` dispatches by type: `Light` keeps the existing ImReflect-driven section (`Light` already has reflection); `MeshRender` uses existing mesh/material widgets; `Camera` reuses the existing camera widget from `Editor.cpp`'s Settings panel; unknown / unregistered types render a stub `Text("No editor registered for this component.")`.

### 8. Context menu policy

Right-click on any row (other than root) opens a popup with:

- **Add Child** — creates a new `SceneNode` with a unique name (`"Node"`, `"Node (1)"`, …) and reparents it.
- **Duplicate** — calls `CloneTree` with `"(copy)"` suffix; resolves collision to `"(copy N)"`.
- **Rename** — activates the row's `RenameState`.
- **Delete** — calls `RemoveFromParent` on the node; if the deleted node was the editor's selection, clears selection.

On the root row, only **Add Child** is offered (root cannot be deleted or renamed).

### 9. MeshRender section — three-level layout

For nodes with a `MeshRender` component, the panel renders three nested `CollapsingHeader` blocks under the component's own header:

**MeshRender block** — mesh reference (current `GetMesh()` name + asset picker popup using `Scene::Manager()->ForEach<Mesh>()`), status indicator (`GetRenderable()`: green dot if material count ≥ submesh count and all weak_ptrs are alive, red otherwise), and the per-submesh Material slot list (one row per slot, with the current material name and a picker popup; rows can be added or removed to change `m_Materials.size()`).

**Mesh block** (only when a mesh is bound) — total vertex / index / triangle counts across all submeshes, `GetSubMeshCount()`, AABB readout (`GetAABB().GetMin() / GetMax() / GetSize() / GetCenter()`), `CastShadows` checkbox (delegates to `mesh->SetCastShadows(bool)`), `IsSkinnedMesh()` + `GetJointCount()` if skinned, a **per-submesh dropdown** labelled "Inspect:" with entries "All" + one entry per submesh (the engine has no real LODs — `SubMesh` index is the closest equivalent; if real LODs land later the dropdown wording can change). When a specific submesh is selected, the dropdown's panel area shows that submesh's `Indices.Data.size() / 3`, vertex count derived from `Positions.Data.size() / 3`, and a bitmask of which vertex attributes are non-empty (positions / normals / tangents / UVs / weights / IDs).

**Material block** (one per slot, 0..N-1) — material name + `SetOpaque` checkbox, `GetTextureFlags()` rendered as a label like `DIFFUSE | NORMAL` (read-only — auto-derived by `SetTexture`), a **Textures table** with one row per standard slot (`DIFFUSE_TEXTURE` / `SPECULAR_TEXTURE` / `NORMAL_TEXTURE`) plus any additional custom keys in `m_Textures`. Each row is:

- 64×64 `ImGui::Image` thumbnail using `(ImTextureID)(intptr_t)tex->GetID()` and the UV flip `ImVec2(0, 1), ImVec2(1, 0)` already used at `EditorWindows.cpp:244-256`.
- A short text label showing the slot key (editable via picker popup that lists `Scene::Manager()->ForEach<Texture>()`).
- A meta string: `"<width> × <height> <format> <sRGB|linear>"`, with an inline `IsSRGB` toggle (calls `tex->SetFilePathAndSRGB(path, srgb)` when there's an associated file).

A **Uniforms table** with categorized rows for the well-known keys: `DIFFUSE_COLOR` / `SPECULAR_COLOR` / `AMBIENT_COLOR` / `EMISSIVE_COLOR` → `ImGui::ColorEdit4`; `SHININESS` / `TRANSPARENCY` / `AMBIENT_FACTOR` / `DIFFUSE_FACTOR` / `SPECULAR_FACTOR` / `EMISSIVE_FACTOR` → `ImGui::DragFloat`. Any other keys in `m_Uniforms` are iterated and rendered by runtime `UniformBase` subclass (1f/2f/3f/4f → `DragFloatN`, 1i/2i/3i/4i/1ui/2ui/3ui/4ui → `DragIntN`, `UniformMatrix4fv` → read-only `Text`). Requires adding a `const UniformMap &GetUniforms() const;` accessor on `Material` mirroring the existing `GetTextures()` pattern (Material.h line 104), since iteration isn't currently possible without it.

A **Shader passes** read-only list: `"Pass <i>: <shader name>"` for each `i = 0..GetShaderForPass(i) != nullptr`. Editing shader sources in-place is out of scope.

The MeshRender root block also gets the existing `x` delete control (the component itself is removable; only `Transform` is protected).

## Risks / Trade-offs

- **Pointer-keyed drag payload is unsafe across editor restarts** → acceptable: drag-drop is a transient gesture, never persisted. Risk mitigated by clearing payload state on focus loss.
- **Hover-to-expand timer map can leak entries** if a drag is cancelled abruptly → mitigated by clearing the map on `ImGui::DragDropPayload` end (`IsAccepted()` and `IsPreviewActive()` both false) and on window close.
- **Sibling name collisions** — `Entity::SetName` doesn't disambiguate, and Lua scripts today rely on this for scripted scenes. The inspector-generated names use a counter ("Node", "Node (1)", …) so they don't collide, but if a user renames a node to one that already exists, FindByName lookups in their scripts will start returning whichever they hit first. Mitigation: do not enforce uniqueness, but log a warning when the inspector detects a sibling collision.
- **Component registry only knows default-constructable components** — `Camera::Create()` doesn't take a viewport, and `MeshRender::Create(nullptr, nullptr)` takes placeholder mesh/material. Adding these through the inspector produces a stub that the user must then configure. Acceptable for v1; a future change can add "Create with…" wizards.
- **Generic component body rendering needs per-type dispatch** — ImReflect works for `Light` and `Transform` (they have `Reflect` tags); `MeshRender` and `Camera` will need small adapter functions. Adds a few hundred lines but contained to `EditorNodeProperties.cpp`.
- **No undo** — accidental Delete / Duplicate is currently unrecoverable without reloading the scene. Listed as a non-goal; out of scope for this change.

## Migration Plan

1. Land engine changes first (`CloneTree`, registry expansion, Lua bindings). These are additive and isolated; existing scripts and scenes keep working.
2. Land editor changes in `EditorWindows.cpp` and `EditorNodeProperties.cpp`. Toggle `WITH_EDITOR=OFF` builds remain unaffected (editor files don't compile).
3. Update `Editor.lua` only if any of the new bindings need explicit handling — likely not, since the new bindings are direct method mirrors.
4. Rollback is per-commit revert; no migration of saved scenes is required (the JSON format is unchanged).

## Open Questions

- Should the inspector remember which rows were expanded across selection changes? (Lean toward no — out of scope.)
- Should the Add Component menu group by category (Rendering / Lighting / Audio / etc.)? Engine currently only has 4 components, so a flat list is fine. Re-evaluate if/when more component types land.
- Should hovering-over-a-drop-target render a visual highlight (background color)? ImGui's `DragDropTarget` API supports this; defaulting to a subtle tint.