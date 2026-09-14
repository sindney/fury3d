# asset-path-identity-and-content-folders

## Why

Two linked problems. First, the engine keeps two competing identity systems for assets: UUIDs (instance identity, stable across save/load, what `EntityManager::Add` keys on) and names (human-readable handles, what every consumer — Content Browser, picker dialogs, `EntityManager::Get<T>(name)` — actually looks up by). Legacy scenes accumulate 6+ copies of the same logical terrain texture (same name, distinct UUIDs) because `LoadTerrainTexture`'s strict `GetFilePath() == path` check forced a fresh `Texture::Create` (and a new UUID) every time a legacy entry's `m_FilePath` was a broken basename like `"snow.png"` instead of `"TerrainIsland/snow.png"`. The Content Browser's `DedupeTiles` covered the ImGui ID-conflict symptom but did not address the source. Second, the Content Browser is a flat tile grid — there is no way to navigate by folder, no notion of where an asset came from, and no way to scope tiles to a subdirectory of a project's `Resource/` tree. Both problems dissolve if the canonical asset identity is the file path: structural uniqueness becomes free, and a folder tree is just a tree of paths.

## What Changes

- **Path is the canonical asset identity.** `EntityManager` stores each entity keyed by its file path (`Texture`, `Material`, `Mesh`, `AnimationClip`, `ParticleSystem`, `Heightmap`, `OceanWaves`). Two assets pointing to the same file cannot coexist by construction. UUIDs remain on each entity as a per-instance serialization handle (so existing save/load round-trips and `Material` ↔ `Texture` shared_ptr references still work), but UUID is no longer the lookup key.
- **One canonical name per asset.** The entity's `m_Name` is derived from its path (basename for display, full path for keying). Same-path collisions are caught at `Add` time and refused with a clear error. The legacy "first-registered-wins" silent-dup behavior in the name index is gone.
- **`EntityManager` API reshape.** `Add<T>(ptr)` rejects same-path entries; the bool return is now load-bearing and every caller checks it. `Get<T>(path)` is the canonical lookup; the old `Get<T>(name)` becomes a deprecated alias that maps name → path via the new map. `Remove<T>(path)` removes the path-keyed entry and (if a corresponding UUID-keyed entry existed) the UUID entry. `ForEach<T>` walks the path-keyed map — duplicates cannot appear because they cannot be inserted.
- **Path-keyed vs UUID-keyed views are unified.** Where a caller needs the per-instance handle (e.g. `Material` deserialization restores a `Texture::Ptr`), the lookup goes through a UUID-keyed map that is automatically maintained alongside the path map. `Scene::Load` and `Importer::MergeInto` use the UUID-keyed map to re-bind saved references; `Terrain::LoadTerrainTexture` and the Content Browser use the path-keyed map to find the canonical asset.
- **Content Browser two-pane layout (UE-style).** Left sidebar is a tree with two root nodes — `content/` (the active scene's containing folder, derived from the scene file's path) and `engine/` (`examples/Resource/`, the engine resource root resolved via `FileUtil::GetAbsPath() + "Resource/"`). Tree nodes are folders; expanding a node scans its children on demand. Right pane is the tile grid, now scoped to the selected folder's contents. Selecting a folder shows only the assets directly inside it; selecting a root shows everything under that root.
- **Folder-relative paths everywhere user-facing.** Tile labels show the asset's basename; hovering shows the full relative path. Drag-drop, rename, and the picker dialog all operate on relative paths under one of the two roots. Renames update the entity's path in place; the old path is freed in the EM atomically.
- **Asset moves tracked across save/load.** When a save emits a `Texture` whose `m_FilePath` differs from where it was opened, the rename is preserved (the file moves with it). On load, the path-keyed map is rebuilt from the saved paths; the UUID-keyed map re-binds the saved shared_ptrs.
- **BREAKING** — Lua bindings: any script using `EntityManager::Get<T>(name)` semantics (the old first-registered-wins name lookup) must move to `Get<T>(path)`. The legacy `examples/` Lua scripts that only used `Scene.GetTexture(path)` continue to work because they were already using the name-as-path convention.
- **BREAKING** — `Content Browser` tile ID derivation changes from `type.name() + ":" + name` to `type.name() + ":" + path`. Existing `imgui.ini` entries for the browser window still dock correctly (the window title is unchanged); the in-window selection state is reset on first launch.

## Capabilities

### New Capabilities

- `asset-path-identity`: `EntityManager` keyed by file path, UUID as per-instance property, `Add` rejects same-path, single canonical name per asset. Subsumes the deduplication half of the legacy `asset-unique-naming` requirement.
- `content-browser-folders`: two-pane UE-style Content Browser — left tree with `content/` and `engine/` roots, right pane folder-scoped tile grid, folder-relative paths in user-facing surfaces, rename-tracks-path.

### Modified Capabilities

- `asset-unique-naming`: the "first-registered-wins name index with a warning" behavior is removed — path collisions are now structural and impossible. The `UniqueName` helper still exists for *user-created* names that don't map to files (e.g. Lua-created `SceneNode`s whose names are arbitrary strings), but it is no longer the EM's primary dedupe mechanism.
- `asset-editor-windows`: per-asset editor windows open via path, not name; the "already-open asset" lookup keys on path.
- `scene-round-trip`: the round-trip count-stability requirement stays (same count before save and after reload), but the implementation no longer needs the legacy `Scene::Load` same-name dedupe guard — the EM cannot contain same-path duplicates in the first place.

## Implementation Note: Coding Style (read before writing code)

When implementing this change:

- **Comments are facts, not history.** No "// previously X did Y so we need to do Z" or "// this used to be a bug" comments. Write the new code as if it was always correct.
- **Compact one-liner comments only.** Either a short single-line note above a non-obvious line, or no comment at all. No multi-paragraph essays.
- **Drop the legacy workaround, then forget it existed.** Remove `DedupeTiles`, the legacy `Scene::Load` same-name skip guards, the strict `GetFilePath() == path` re-check in `LoadTerrainTexture` — and don't leave a comment explaining what they replaced. The replacement is self-evident from the code.
- **No "this is the new way" markers.** The new code is THE way. Don't label paths or branches as "new" or "legacy" — those words imply a past that the code shouldn't acknowledge.
- **Rename, don't annotate.** If a function name no longer reflects what it does, rename it. Don't keep the old name and add a comment.

## Impact

- **Engine core**: `EntityManager` (`engine/Fury/EntityManager.h` / `.cpp`) — add a path-keyed map alongside the UUID-keyed one; reshape `Add`, `Get`, `Remove`, `ForEach`. `Scene::Load` / `Scene::Save` (`engine/Fury/Scene.cpp`) — write/read path-keyed entries; the dedupe guard at the loader is no longer needed. `Texture` / `Material` / `Mesh` / `AnimationClip` / `ParticleSystem` / `Heightmap` / `OceanWaves` — derive `m_Name` from `m_FilePath` on construction; reject same-path in `Add`.
- **Components**: `Terrain::LoadTerrainTexture` (`engine/Fury/Terrain.cpp`) — the strict path-equality check disappears because path equality is now guaranteed by the EM. `Heightmap::ResolveHeightmapAsset` similarly simplifies.
- **Editor**: `EditorWindows.cpp` (`Content Browser` section) — replace `CollectTiles` + `DedupeTiles` with a folder-tree + folder-scoped-tile renderer; tile ID uses path; selection state keys on `(type, path)`. `EditorAssetPicker` — path-based filter / pick. `EditorNodeProperties.cpp` — texture path references update on rename.
- **Lua API**: `Scene.GetTexture(name)` and friends continue to work (name == path convention preserved); new `Scene.GetTexturesInFolder(root, folder)` and `Scene.ForEachTexture` continue but iterate the path-keyed map (always de-duplicated).
- **Save format**: existing `.json` / `.bin` files reload unchanged — the saved `path` field per `Texture` IS the canonical key. Old scenes that contained same-name-different-UUID duplicates lose the duplicates on first save (they cannot be represented in the EM anymore); the user is warned once when this happens on a legacy file.
- **Content browser UX**: existing single-pane layout retired; new two-pane layout takes its place. `imgui.ini` `Settings.ContentBrowser.*` keys are reset (one-time user-visible change).
- **Tests**: new headless tests cover (1) same-path `Add` rejected, (2) `Scene::Load` collapse of legacy same-name dups, (3) `ForEach<T>` counts match unique path counts across save+reload cycles, (4) Content Browser tree builds correct roots for a scene with a known path.
- **No new third-party dependencies.** No build-system changes beyond the existing `engine/CMakeLists.txt` source list.