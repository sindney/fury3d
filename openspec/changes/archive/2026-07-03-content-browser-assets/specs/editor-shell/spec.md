## MODIFIED Requirements

### Requirement: The editor SHALL ship a Content Browser mirroring the active scene's directory

The Content Browser window SHALL render the in-scene assets of the active `Scene`'s `EntityManager` as an icon grid (Unity/UE4-style rectangle tiles in a wrapping `ImGui` layout), NOT a single-column `Selectable` file list. The window SHALL enumerate two asset categories from `Scene::Active->GetEntityManager()`: every registered `Mesh` (`ForEach<Mesh>`) and every registered `Material` (`ForEach<Material>`). When no scene is active or the `EntityManager` has no Meshes or Materials, the window SHALL display the placeholder text `"(no assets in active scene)"`.

Each tile SHALL render at a fixed icon size (default 64×64 thumbnail area + label row below) and the grid SHALL wrap to fill the available content region width. Tiles SHALL be selectable (single selection); clicking a tile SHALL set an internal `selected_asset` pair of `(type_index, name)` and replace the previous selection. Selection state SHALL be per-frame (no persistence across editor sessions).

Each tile's thumbnail SHALL be:

- **For a Material**: a 64×64 `ImGui::Image` of the material's diffuse texture (or first non-null texture if no diffuse), or a flat color swatch derived from the material's `DIFFUSE_COLOR` uniform if no textures are bound, or a checkerboard placeholder if neither is available. The texture GL handle SHALL be cast via `(ImTextureID)(intptr_t)tex->GetID()` with UVs `ImVec2(0,1), ImVec2(1,0)` (matching the existing `EditorNodeProperties.cpp:304` pattern).
- **For a Mesh**: a 64×64 mini-3D render of the mesh, rendered off-screen into a cached FBO per mesh (keyed on the mesh's `BufferId`), drawn with the mesh's first material or a fallback flat shader, with the camera framed by the mesh's AABB. The FBO SHALL be regenerated only when the mesh's `BufferId` changes (i.e. the mesh was re-uploaded to the GPU).

Each tile's label row SHALL display the asset's name truncated to fit the tile width (ellipsis on overflow). The tile SHALL additionally render a small type badge (`M` for Mesh, `Mat` for Material) in the top-left corner of the thumbnail.

The Content Browser SHALL support the following interactions:

- **Double-click** on a tile SHALL open the per-asset editor window (see `asset-editor-windows` capability). If the editor window for that asset is already open, double-click SHALL focus the existing window.
- **Right-click** on a tile SHALL open a context menu with the items: `Duplicate`, `Rename`, `Delete`. Right-clicking empty space in the grid SHALL open a context menu with the items: `Refresh` (re-runs `ForEach` next frame).
- **F2** on a selected tile SHALL activate inline rename (same as `Rename` menu item).

The window SHALL be hidden by default and toggled from `Window → Content Browser`.

The Content Browser SHALL expose a `Editor::SelectAssetInBrowser(type_index, name)` API (C++ side, called by the Node Properties inspector) that sets `selected_asset` and scrolls the grid so the matching tile is visible on the next frame. This is the "jump to asset" mechanism used by the inspector's mesh/material rows.

The legacy file-listing behavior (enumerating `Resource/Scene/` via `FileUtil::ListDirectory` with `[J]`/`[B]`/`[G]`/`[F]` extension prefixes) SHALL be removed from this window. File open/import/save remain reachable via the File menu and the Open/Import modals.

#### Scenario: Browser shows in-scene assets as a wrapping icon grid

- **WHEN** Editor.lua opens `Resource/Scene/scene.bin` containing 3 meshes and 5 materials
- **AND** the Content Browser window is visible
- **THEN** the window renders 8 tiles in a wrapping grid
- **AND** each tile shows a thumbnail, a name label, and a type badge (`M` or `Mat`)

#### Scenario: Empty scene shows placeholder

- **WHEN** the engine starts with no startup scene
- **AND** the Content Browser window is visible
- **THEN** the window displays the placeholder text `"(no assets in active scene)"`

#### Scenario: Selecting a tile updates selected_asset

- **WHEN** the user clicks the "Cube" Mesh tile
- **THEN** `selected_asset` becomes `(typeid(Mesh), "Cube")`
- **AND** the previously selected tile (if any) is deselected

#### Scenario: Material tile shows diffuse thumbnail

- **WHEN** a material named "Material_Ground" has a diffuse_texture bound to `grass.jpg`
- **THEN** the "Material_Ground" tile renders a 64×64 thumbnail of `grass.jpg`

#### Scenario: Material tile falls back to color swatch

- **WHEN** a material has no textures but has a `DIFFUSE_COLOR` uniform of `(0.8, 0.2, 0.2, 1.0)`
- **THEN** the tile's thumbnail area is filled with a flat `(0.8, 0.2, 0.2)` swatch

#### Scenario: Double-click opens the asset editor

- **WHEN** the user double-clicks the "Cube" Mesh tile
- **THEN** a `Mesh: Cube` editor window opens (per the `asset-editor-windows` capability)

#### Scenario: Right-click opens the Duplicate/Rename/Delete context menu

- **WHEN** the user right-clicks the "Cube" Mesh tile
- **THEN** a context menu opens with the items `Duplicate`, `Rename`, `Delete` enabled

#### Scenario: Refresh re-runs ForEach next frame

- **WHEN** the user right-clicks empty grid space and selects `Refresh`
- **THEN** on the next frame the grid re-enumerates `Scene::Active->GetEntityManager()->ForEach<Mesh>()` and `ForEach<Material>()`

#### Scenario: External SelectAssetInBrowser focuses a tile

- **WHEN** the Node Properties inspector calls `Editor::SelectAssetInBrowser(typeid(Mesh), "Cube")`
- **THEN** on the next frame `selected_asset` is `(typeid(Mesh), "Cube")`
- **AND** the grid scrolls so the "Cube" tile is visible

#### Scenario: Mesh thumbnail FBO is cached on BufferId

- **WHEN** the Content Browser first renders a tile for "Cube" with `BufferId == 42`
- **AND** the "Cube" mesh is not modified between frames
- **THEN** the FBO is rendered once and reused on subsequent frames
- **AND** no new FBO is allocated until `Cube->GetBufferId()` changes
