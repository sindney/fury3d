## Context

The fury3d editor currently has three pieces of "asset UI" that do not connect to each other:

1. **Content Browser** (`engine/Fury/Editor/EditorWindows.cpp:963-1006`) — a flat `ImGui::Selectable` list of files in `Resource/Scene/` with one-char icons. Selection sets `g_SelectedFile` and nothing reads it back.
2. **MeshRender inspector block** (`engine/Fury/Editor/EditorNodeProperties.cpp:360-404`) — read-only display of mesh name + per-slot material textures.
3. **Mesh/Material assets** — `Entity` subclasses stored as `shared_ptr` in the active `Scene`'s `EntityManager` (`EntityManager.h:13`), referenced by name (no UUID) from `MeshRender`'s `weak_ptr<Mesh>` / `vector<weak_ptr<Material>>`. Identity is `hash(name)`.

The engine already has:

- `EntityManager::ForEach<T>(fn)` (`EntityManager.h:147`) — the iteration primitive for asset pickers.
- `ImGui::Image((ImTextureID)(intptr_t)tex->GetID(), ImVec2(48,48), ImVec2(0,1), ImVec2(1,0))` (`EditorNodeProperties.cpp:304`) — the texture thumbnail pattern.
- `Texture::GetTemporary(...)` + raw GL FBO blit (`EditorWindows.cpp:284-397` Profiler Shadows tab) — the offscreen-render pattern.
- A flag-then-`OpenPopup` modal pattern (`Editor.cpp:316-369` Save As modal).
- ImGuizmo vendored at `engine/ThirdParty/ImGuizmo/` including `ViewManipulate` (the upstream "ImViewGizmo" widget), but never called.
- `UniqueChildName(parent, base)` (`EditorWindows.cpp:496-509`) — editor-only, `SceneNode*`-typed.

Constraints:
- No UUID/GUID system; identity stays name-based.
- No separate `Asset`/`AssetManager` class; `Mesh`/`Material` ARE the runtime objects.
- Scene file format (`scene.json`/`.bin`) cannot change shape — meshes/materials stay inlined, referenced by name.
- All editor code gated by `WITH_EDITOR`.

## Goals / Non-Goals

**Goals:**
- Content Browser becomes an icon grid of in-scene Mesh/Material assets with thumbnails, context menu, double-click-to-edit, and external selection API.
- MeshRender inspector becomes a two-way binding surface: picker for rebinding mesh/material, jump-to-asset button.
- Asset editor windows (Mesh + Material) walk serializable properties and render typed editors.
- Mesh editor renders a centered 3D preview with orbit/zoom + `ImGuizmo::ViewManipulate` cube.
- Unique-name logic is lifted out of the editor's anonymous namespace into an engine-level helper shared with assets.
- Delete-in-use asset triggers a reusable Yes/No confirm dialog.

**Non-Goals:**
- UUID/GUID system.
- External `.mesh`/`.mat` files on disk (assets stay in scene).
- Drag-drop from Content Browser into viewport.
- Material/shader graph editing.
- Asset import pipeline into the Content Browser (still via File → Import).
- Refactor of other inspector sections (Light, Camera, Transform) — only MeshRender changes.

## Decisions

### D1: Unique-name helper as a free function in a new `EntityUtil.h`

`fury::UniqueName(const std::string& base, const std::function<bool(const std::string&)>& exists)` lives in a new non-editor header `engine/Fury/EntityUtil.h`. The editor's `UniqueChildName` becomes a one-line wrapper.

**Why:** keeps the helper usable by engine-side code (e.g. future `Scene::Merge` dedup), avoids pulling `WITH_EDITOR` into engine code, and is the smallest possible surface. Alternative considered: method on `Entity` or `EntityManager` — rejected because it would require the caller to know the asset type at the call site, and the predicate-style API is more general (works for `SceneNode::FindChild`, `EntityManager::Get<T>`, and any future container).

**Alternative considered:** template `<typename T> std::string EntityManager::UniqueName<T>(base)` — rejected: requires EntityManager to know about `SceneNode::FindChild` for the node case, breaking layering.

### D2: Content Browser data source — direct `EntityManager::ForEach` per frame

The grid re-runs `Scene::Active->GetEntityManager()->ForEach<Mesh>(...)` and `ForEach<Material>(...)` every frame the window is visible. No caching layer.

**Why:** matches the existing pattern (the file-list version called `directory_iterator` every frame, and `ForEach<Mesh>` is a single `unordered_map` iteration — far cheaper than `directory_iterator`). Asset counts in real scenes are dozens, not thousands. Avoids cache-invalidation bugs when the inspector duplicates/renames/deletes an asset.

**Trade-off:** if a scene ever ships with thousands of meshes, per-frame ForEach will show up in the profiler. At that point we add a dirty-flag on `EntityManager::Add/Remove` and re-snapshot only when dirty. Out of scope for now.

### D3: Tile selection state — `std::optional<std::pair<std::type_index, std::string>> g_SelectedAsset`

Replaces `static std::string g_SelectedFile`. Type-index lets the inspector's jump-to-asset disambiguate Mesh vs Material of the same name.

**Why:** `type_index` is the same key `EntityManager` uses internally, so the inspector can call `Editor::SelectAssetInBrowser(typeid(Mesh), mesh->GetName())` directly. Alternative considered: a `Entity*` raw pointer — rejected because assets can be deleted while the inspector still holds the selection, and the `(type, name)` pair survives deletion gracefully (just deselects).

### D4: Mesh thumbnail rendering — per-mesh FBO cached on `BufferId`

A `std::unordered_map<size_t /*BufferId*/, ThumbnailCacheEntry>` lives file-local in `EditorAssetWindows.cpp` (or a small `EditorMeshThumbnails` helper). Each entry holds a `Texture::GetTemporary(128,128,...)` color RT + a raw GL FBO name (mirroring `Pass::m_FrameBuffer`). When the grid needs a thumbnail:

1. Lookup by `mesh->GetBufferId()`. If present, render that texture into the tile.
2. If missing (or `BufferId` changed since last render), allocate/reallocate FBO+RT, render the mesh offscreen into it once, cache, and reuse.

**Render:** a dedicated `EditorThumbnailShader` (flat-shaded, depth-test on, single directional light) draws the mesh's VAO. Camera: orbit angles default to `(azimuth=30°, elev=20°)`, distance computed from `mesh->GetAABB()` radius and a 0.6 fill factor. Rendered with raw `glViewport`/`glBindFramebuffer`/`glClear` — no Pipeline, no SceneManager, no OcTree.

**Why:** matches the existing Shadows-tab pattern. `BufferId` is the existing dirty-flag (`Buffer.h:10`, bumped whenever a mesh is re-uploaded to the GPU). `Texture::GetTemporary` is the engine's existing RT pool.

**Trade-off:** first frame a tile appears, the FBO is allocated and rendered (one draw call). Subsequent frames are zero-cost. Memory cost: 128×128 RGBA8 = 64KB per visible mesh, dropped from the cache when the mesh's `BufferId` disappears from the EntityManager (eviction on next ForEach pass).

**Alternative considered:** render all thumbnails into one atlas texture — rejected as premature complexity; per-mesh FBO is simpler and 64KB×100 meshes = 6.4MB, well within budget.

### D5: Material thumbnail — diffuse texture or color swatch

For each material tile:

- If `Material::GetTextures()["diffuse_texture"]` is non-null, render that texture (reusing the `EditorNodeProperties.cpp:304` `ImGui::Image` pattern at 64×64).
- Else if `Material::GetUniforms()["DIFFUSE_COLOR"]` exists, fill the tile area with `ImGui::GetWindowDrawList()->AddRectFilled(...)` using the color.
- Else, a checkerboard placeholder (drawn via `AddRectFilled` quads).

**Why:** no FBO render needed for materials — they're already textures/colors. Zero per-frame cost.

### D6: Asset editor windows — modal `BeginPopupModal` with per-asset popup ID

A `std::unordered_map<std::string /*popupID*/, bool /*open*/> g_AssetEditorWindows` lives file-local. The popup ID is `("MeshEditor:" + name)` or `("MaterialEditor:" + name)`. Double-click on a tile calls `ImGui::OpenPopup(popupID.c_str())` next frame. Only one open per asset (keyed by name).

**Why:** ImGui modal popups are the existing pattern (Save As, Open/Import modals). Modal prevents interaction with the rest of the editor while editing, which keeps the "edit a single asset" mental model clean.

**Trade-off:** modal means you can't, e.g., drag from the Content Browser into the editor. That's fine for the non-goal of drag-drop. If a future task needs non-modal asset editors, switch to `Begin` windows instead.

### D7: Mesh editor 3D preview — orbit camera math + raw GL render

The preview pane is an `ImGui::BeginChild("preview", size)` with a captured content region. State per open mesh editor:

```cpp
struct MeshEditorState {
    float azimuth = 30.0f * DEG2RAD;
    float elevation = 20.0f * DEG2RAD;
    float distance = 0.0f; // computed on open from AABB
    BoxBounds initialAABB;
};
```

- **Open**: `distance = (aabb.Radius() * 0.6) / sin(fov * 0.5)` (so mesh fills ~60% of shorter axis).
- **Drag**: `azimuth -= dx * 0.01f; elevation -= dy * 0.01f;` (clamp elevation to `[-89°, 89°]`).
- **Wheel**: `distance *= (1.0f - wheel * 0.1f);` (clamp `[0.1 × initial, 10 × initial]`).
- **View matrix**: `lookAt(center + spherical(azimuth, elevation, distance), center, up)`.

**Render:** offscreen FBO (size = preview child's content region), `EditorThumbnailShader` (same as D4) but at full preview resolution. The result texture is drawn via `ImGui::Image((ImTextureID)(intptr_t)previewRT->GetID(), size, ImVec2(0,1), ImVec2(1,0))`.

**ImGuizmo::ViewManipulate integration:** after rendering, call `ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList())`, `ImGuizmo::SetRect(...)`, then `ImGuizmo::ViewManipulate(&view.Raw[0], 60.0f, corner, size, 0)` inside the preview child's screen-space. If the function modifies `view` (user clicked a face), decompose back into `azimuth`/`elevation`/`distance` for next frame's state.

**Why:** raw GL render avoids entangling the preview with the scene's `Pipeline` (which has post-processing, GBuffer, shadow maps that we don't want in a preview). The orbit math is 20 lines and self-contained.

**Trade-off:** orbit/zoom is hand-rolled, not from ImGuizmo. ImGuizmo's `Manipulate` is for object transforms, not camera orbit. `ViewManipulate` is the axis-cube snap UI. This split (hand-rolled orbit + ImGuizmo axis-cube) matches how upstream ImGuizmo examples use it.

### D8: Modal asset pickers — reuse the Open/Import modal pattern

`RenderAssetPickerModal(title, type_index, onPick)` is a small helper mirroring `RenderOpenImportModal` (`Editor.cpp:373-438`): `BeginPopupModal` + a `Selectable(AllowDoubleClick)` list fed by `EntityManager::ForEach<T>` + `OK`/`Cancel` buttons. Single callback `onPick(shared_ptr<void>)` on confirm.

**Why:** the existing pattern is proven (it's used for Open and Import). One generic helper serves mesh/material/texture pickers.

### D9: Confirm dialog — single pending-request queue

`Editor::RequestConfirmDialog(title, message, onResult)` pushes onto `static std::queue<ConfirmRequest> g_PendingConfirms;`. On each tick, if no confirm is currently open and the queue is non-empty, pop one and `ImGui::OpenPopup("ConfirmDialog")`. `RenderConfirmDialog()` (called from `Editor::Tick`) renders the modal: `BeginPopupModal`, body text, `Yes`/`No` buttons. On click, `CloseCurrentPopup()` and invoke `onResult(bool)`. Esc / click-outside is treated as `No`.

**Why:** queue serializes confirms so multiple in-flight deletes don't open stacked modals. Mirrors the `g_SaveAsModalOpen` flag pattern but generalized. `onResult` callback decouples caller from modal lifecycle.

**Trade-off:** callback-based API means callers must capture state in lambdas. Acceptable — same style as the rest of the editor.

### D10: Delete-in-use counting — single `ForEach<MeshRender>` pass

When the user picks `Delete` on an asset tile, before opening the confirm dialog the editor counts references: iterate `Scene::Active->GetSceneManager()`'s nodes (or just `Scene::Active`'s root subtree recursively) and count `MeshRender` components where `mesh.lock() == target` (for mesh delete) or `materials[i].lock() == target` (for material delete).

**Why:** there's no existing ref-count on assets (they're `shared_ptr` in the EntityManager, `weak_ptr` in MeshRender). A one-shot traversal is O(N) per delete attempt, which is fine for a user-initiated action on scenes of any reasonable size.

**Alternative considered:** maintain a reverse-index `asset → set<MeshRender*>` updated on `SetMesh`/`SetMaterial` — rejected as premature complexity; delete is rare.

### D11: Inline rename — `ImGui::InputText` with Enter/Esc handling

Each tile, when selected and rename-active (triggered by F2 or context menu Rename), replaces its label row with an `ImGui::InputText` of the asset's name. On Enter (or focus loss with content), validate:

1. If empty, fall back to the original name (no-op).
2. If `EntityManager::Get<T>(newName) != nullptr` (collision with another asset), call `UniqueName(newName, predicate)` to get a unique variant and apply that.
3. Else apply `newName`.

On Esc, cancel (no rename).

Rename is implemented as: `oldAsset = EntityManager::Get<T>(oldName)`; clone is not needed — `Entity::SetName` recomputes `m_HashCode`; but `EntityManager` keys by hash, so we must `Remove<T>(oldName)` then `Add<T>(asset)` (after `SetName`) to re-key the map. All `MeshRender` weak_ptrs survive (they point at the `shared_ptr`'s control block, not the map entry).

**Why:** `EntityManager` is a name→ptr map, not name→data, so re-keying is the only way. Existing `MeshRender` weak_ptrs don't break because they reference the `shared_ptr` control block, not the map. On next `Scene::Save`, MeshRender writes the new name. On next `Scene::Load`, the new name resolves.

**Trade-off:** in-memory, between rename and next save/load, MeshRender's `m_Mesh` weak_ptr still points at the right asset, but if someone saves the scene without reloading, MeshRender's serialization writes the new name (because `MeshRender::Save` reads `m_Mesh.lock()->GetName()`). Confirmed by re-reading `MeshRender::Save` at `MeshRender.cpp:114-115`.

### D12: Duplicate — deep copy via serialize round-trip

`Duplicate asset` uses `FileUtil::SerializeToString` → `DeserializeFromString` on the asset (the same trick `SceneNode::CloneTree` uses at `SceneNode.cpp:182-214`). After deserialization, call `SetName(UniqueName(origName + " (copy)", predicate))` and `EntityManager::Add<T>(copy)`. For Material, the inlined textures come along for the ride (Texture::Save serializes them by path). For Mesh, all vertex streams + submeshes + joints are deep-copied.

**Why:** the serialize round-trip is the existing deep-copy mechanism, already proven by `SceneNode::CloneTree`. Avoids writing per-field copy constructors for Mesh/Material/Texture/SubMesh/Joint.

**Trade-off:** more allocations than a hand-rolled copy. Acceptable — duplicate is user-initiated and rare.

### D13: Editor CMake additions

Add to `engine/CMakeLists.txt` under the `WITH_EDITOR` block (next to `EditorWindows.cpp` etc.):

- `EditorAssetWindows.cpp` — mesh/material editor windows + mesh thumbnail cache + mesh preview renderer.
- `EditorConfirmDialog.cpp` — confirm dialog request queue + render.
- `EditorAssetPicker.cpp` — generic asset picker modal helper (used by inspector + asset editors).

New engine header (not editor-gated): `EntityUtil.h` for `UniqueName`.

## Risks / Trade-offs

- **[Risk] Per-frame `ForEach<Mesh>` + `ForEach<Material>` in the Content Browser shows up in profiling on huge scenes.** → Mitigation: measure; if it's >1% of frame time, add a dirty-flag on `EntityManager::Add/Remove` and snapshot only when dirty. Tracked as a follow-up task in `tasks.md`.
- **[Risk] Mesh thumbnail FBO leak if `EntityManager` drops a mesh but the cache retains its entry.** → Mitigation: on each ForEach pass, build a set of currently-visible `BufferId`s and erase cache entries not in the set. O(visible_meshes) per frame.
- **[Risk] Inline rename of an asset that's referenced by a MeshRender currently selected in the inspector** — the inspector's "→" button holds the old name. → Mitigation: the inspector re-reads `mesh->GetName()` every frame, so it picks up the new name on the next frame. The `g_SelectedAsset` in the Content Browser also needs to track by `weak_ptr` or refresh on rename. Decided: `g_SelectedAsset` is `(type_index, name)`; on rename, we update `g_SelectedAsset.second` to the new name in the same operation so selection follows the rename.
- **[Risk] Offscreen render of mesh thumbnail uses a depth buffer — needs `glTexImage2D(GL_DEPTH_COMPONENT)` + `glFramebufferTexture2D(GL_DEPTH_ATTACHMENT)`.** → Mitigation: follow `Pass.cpp`'s existing depth RT setup pattern; if it's brittle, fall back to no depth test (sorted draw) — meshes are convex enough that back-face culling alone is acceptable for a 64×64 thumbnail.
- **[Risk] ImGuizmo::ViewManipulate requires a prior `ImGuizmo::BeginFrame()` + `SetRect()` to set up its state.** → Mitigation: call `BeginFrame` + `SetRect` to the preview child's screen rect before `ViewManipulate`. The viewport's existing `EditorGizmo.cpp` already does this; we mirror it.
- **[Trade-off] Asset editor is modal (D6), not a dockable window.** → Acceptable: simpler state model, fewer bugs. If users want non-modal later, swap `BeginPopupModal` for `Begin`.
- **[Trade-off] Orbit camera is hand-rolled (D7), not a library.** → Acceptable: 20 lines, well-understood math. A library would pull in a dependency for negligible gain.
- **[Trade-off] Delete-in-use counting (D10) is a full node traversal per delete attempt.** → Acceptable: user-initiated, rare. Not on the hot path.
