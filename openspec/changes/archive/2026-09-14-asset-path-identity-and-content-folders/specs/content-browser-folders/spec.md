# content-browser-folders

## Purpose

The Content Browser is the editor's asset navigator. It currently renders one flat grid of every asset in the active scene's `EntityManager`, with no notion of folder hierarchy and no way to scope tiles to a subdirectory. This change upgrades the browser to a UE-style two-pane layout: a left sidebar tree whose two roots are `content/` (the active scene's containing folder) and `engine/` (`examples/Resource/`, the engine resource root), and a right pane that shows the assets in the selected folder as a tile grid. Paths become the user-facing identity for assets — drag/drop, rename, and the picker dialog all operate on relative paths under one of the two roots.

## Requirements

### Requirement: The Content Browser SHALL render a two-pane layout with a folder tree on the left and a tile grid on the right

`engine/Fury/Editor/EditorWindows.cpp` `RenderContentBrowserWindow` SHALL be reorganized into two `ImGui::BeginChild` regions side-by-side:

- **Left pane** ("tree", default width 220 px, user-resizable via the splitter that already exists for asset windows): the folder tree (see "folder tree" requirement below).
- **Right pane** ("tiles"): the existing tile grid, now scoped to the folder selected in the tree (see "folder-scoped tile grid" requirement below).

The layout SHALL use `ImGui::BeginChild` + `ImGui::SameLine` (consistent with the Mesh editor's two-pane layout per the `asset-editor-windows` spec). A `ImGui::Splitter`-style drag handle between the panes resizes the tree pane; the tile pane fills the remainder.

The window title remains `"Content Browser"`. Existing `imgui.ini` settings for the window position/size/dock continue to load; the in-window selection state (`g_SelectedAsset`) is reset to `std::nullopt` on first launch under this change because the legacy key was name-based and the new key is path-based.

#### Scenario: Two panes render side-by-side after the change

- **WHEN** the user opens the Content Browser after the change is deployed
- **THEN** the window shows a left pane containing the folder tree (with `content/` and `engine/` roots)
- **AND** a right pane containing the tile grid for the currently selected folder
- **AND** the two panes are separated horizontally (not stacked vertically)

#### Scenario: Splitter drag resizes the tree pane

- **WHEN** the user drags the splitter between the panes
- **THEN** the tree pane's width changes
- **AND** the tile pane fills the remaining width
- **AND** the new width persists across restarts via `imgui.ini`

#### Scenario: First launch under the change resets selection

- **WHEN** the user opens the Content Browser for the first time after the change is deployed
- **THEN** no asset is selected (`g_SelectedAsset == std::nullopt`)
- **AND** the tree expands `content/` and `engine/` by default
- **AND** the tile pane shows the contents of `content/` (the scene's containing folder)

### Requirement: The folder tree SHALL expose two roots: `content/` (scene's containing folder) and `engine/` (engine resource root)

The tree's two root nodes SHALL be:

- **`content/`**: the folder containing the active scene file. Derived by taking the directory part of `Editor::GetCurrentScenePath()` and stripping any trailing path-separator. When no scene is open, the `content/` root shows an empty folder with a status label "no scene open"; its children are populated once a scene is loaded.
- **`engine/`**: `examples/Resource/` resolved via `FileUtil::GetAbsPath() + "Resource/"`. This is the same path the engine's existing `Engine/` prefix assets (e.g. `Engine/Texture/DefaultSky.png`) resolve to. `engine/` is always present regardless of whether a scene is open.

Expanding any folder node scans that folder's immediate children (one level deep, on demand, no recursive pre-scan). Children are derived from the engine's `EntityManager`:

- The tree shows ONLY folders that contain at least one asset in the EM. Empty folders on disk that have no asset in the EM are not shown.
- A folder's children include both sub-folders (which themselves contain assets in the EM) and individual asset entries (the actual `Texture` / `Material` / `Mesh` / etc. whose paths are under this folder).

The tree SHALL persist expansion state per-folder-path across restarts via `imgui.ini` (using `ImGui::TreeNode` with a stable `ImGuiID` derived from the folder path).

The currently selected folder's path SHALL be persisted as `ContentBrowser.SelectedFolder` in `imgui.ini`'s `Settings` handler (added by this change if not already present).

#### Scenario: Two roots are visible

- **when** the Content Browser renders the tree
- **then** the tree shows `content/` and `engine/` as the two top-level nodes
- **and** both roots are expandable

#### Scenario: content/ reflects the active scene's folder

- **when** the active scene is `Projects/ocean/ocean_island.bin`
- **then** the `content/` root's tooltip / status shows `Projects/ocean/`
- **and** expanding `content/` shows the folders and assets under `Projects/ocean/` (e.g. `content/TerrainIsland/` containing the four layer textures + splatmap)

#### Scenario: engine/ always shows the engine resource root

- **when** the engine starts and no scene is open
- **then** the `content/` root shows "no scene open"
- **and** `engine/` is still expandable and shows the contents of `examples/Resource/`

#### Scenario: Folders with no assets are hidden

- **when** the user expands `engine/Texture/Sky/`
- **then** the tree shows the textures in that folder
- **and** does not show empty subfolders on disk that have no EM-registered assets inside them

#### Scenario: Expansion state persists across restarts

- **WHEN** the user expands `content/TerrainIsland/` and selects it
- **AND** the editor is restarted
- **THEN** `content/TerrainIsland/` is still expanded
- **AND** the tile pane still shows the contents of `content/TerrainIsland/`

### Requirement: The tile pane SHALL show only assets in the currently selected folder

When a folder is selected in the tree, the right pane SHALL show a tile grid of every asset in the EM whose path is under that folder. The path-prefix check SHALL be: `asset->GetPath().substr(0, selected_folder.size()) == selected_folder` AND `asset->GetPath()[selected_folder.size()]` is a path separator (so `content/Terrain` does not match `content/TerrainIsland/...`). Assets outside the selected folder are NOT shown.

The tile grid SHALL preserve the existing tile thumbnail / metadata behavior per the `asset-editor-windows` spec:
- Tiles are 96x96 px thumbnails with a 16-px gutter (`kTileThumbnail`, `kTilePitch` already defined in `EditorWindows.cpp`).
- `Texture` tiles show the texture's first uploaded GL ID via `ImGui::Image`.
- `Material` tiles show the diffuse texture if bound, else the diffuse color, else a checkerboard placeholder (the existing `RenderMaterialThumbnail` helper).
- `Mesh` tiles use the disk-cached thumbnail (per the `mesh-thumbnail-disk-cache` spec).
- `AnimationClip`, `ParticleSystem`, `Heightmap`, `OceanWaves` show generic placeholder thumbnails.

The existing type-filter combo (`All` / `Mesh` / `Material` / `Texture` / etc.) and the text fuzzy filter (the `g_FilterText` subsequence match) continue to scope the tile grid. Both filters apply AFTER the folder scope: a folder's assets are listed, then filtered by type and name. The existing `DedupeTiles` collapse is no longer needed because the EM cannot contain path-keyed duplicates.

The tile's ImGui ID SHALL be `type.name() + ":" + path` (was: `type.name() + ":" + name`). Because paths are unique by construction, ID collisions are structurally impossible.

#### Scenario: Selecting a folder scopes the tiles

- **WHEN** the user clicks `content/TerrainIsland/` in the tree
- **THEN** the tile pane shows the assets whose `m_FilePath` is under `content/TerrainIsland/`
- **AND** does not show assets under other folders
- **AND** the asset count in the tile pane matches the number of EM-registered assets whose path is under that folder

#### Scenario: Type filter scopes tiles within a folder

- **WHEN** `content/TerrainIsland/` is selected
- **AND** the user picks `Texture` from the type filter combo
- **THEN** the tile pane shows only `Texture` assets under `content/TerrainIsland/`
- **AND** the asset count in the tile pane matches the number of `Texture` assets under that folder

#### Scenario: Text filter scopes tiles within a folder

- **WHEN** `content/TerrainIsland/` is selected
- **AND** the user types `grass` in the text filter
- **THEN** the tile pane shows only assets under `content/TerrainIsland/` whose name (or path) contains `grass` as a subsequence

#### Scenario: Path-based ImGui IDs avoid collisions

- **WHEN** the engine holds multiple assets under different folders
- **AND** all of them are registered in the EM
- **THEN** every tile's ImGui ID is unique (path-keyed, structurally guaranteed)
- **AND** no "X visible items with conflicting ID" warning is ever logged from the Content Browser

### Requirement: Asset operations (rename, delete, picker) SHALL operate on paths

The Content Browser's right-click context menu and the asset picker modal (`EditorAssetPicker`) SHALL treat the asset's `m_FilePath` as the identity:

- **Rename** opens an inline `InputText` whose accepted value becomes the new `m_FilePath`. The basename is extracted for display; the directory stays fixed. After rename, `em->Get<T>(old_path)` returns `nullptr` and `em->Get<T>(new_path)` returns the renamed asset.
- **Delete** removes the asset from the EM via `em->Remove<T>(path)`. The "asset is in use" guard continues to work: a Texture cannot be deleted if a Material references it.
- **Picker dialog** (`EditorAssetPicker`) opens a similar two-pane layout, scoped to one the roots, and returns the picked asset's `m_FilePath` to the caller. Callers that previously received a name (e.g. `TerrainEditorHmPicker`) continue to use the returned path identically because path == name == the asset's identifier in their existing code.

#### Scenario: Rename updates the path-keyed EM entry

- **WHEN** the user clicks Rename on a Texture tile whose path is `"content/TerrainIsland/snow.png"`
- **AND** types `ice.png` as the new basename
- **THEN** `texture->GetPath() == "content/TerrainIsland/ice.png"`
- **AND** `texture->GetName() == "ice.png"`
- **AND** `em->Get<Texture>("content/TerrainIsland/snow.png")` returns `nullptr`
- **AND** `em->Get<Texture>("content/TerrainIsland/ice.png")` returns the texture

#### Scenario: Delete removes the path-keyed EM entry

- **WHEN** the user clicks Delete on a Texture tile whose path is `"content/TerrainIsland/snow.png"`
- **AND** no Material references this texture
- **THEN** `em->Get<Texture>("content/TerrainIsland/snow.png")` returns `nullptr`
- **AND** the tile no longer appears in the tile pane

#### Scenario: Picker dialog returns the asset's path

- **WHEN** the user opens `TerrainEditorHmPicker` (or any asset picker modal)
- **AND** picks a heightmap from the picker
- **THEN** the picker returns the asset's path
- **AND** the caller (the Terrain editor) sets `terrain->SetHeightmapName(path)` (continuing to use the name slot, which is identical to the path)