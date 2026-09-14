# Tasks: asset-path-identity-and-content-folders

## 1. EntityManager: path-keyed map alongside UUID-keyed map

- [x] 1.1 Add a `m_PathMap<T>` template alias (`std::unordered_map<std::string, std::shared_ptr<T>>`) to `EntityManager` (engine/Fury/EntityManager.h), keyed by file path string.
- [x] 1.2 Refactor `EntityManager::Add<T>(ptr)` to insert into both `m_EntityMap<T>` (UUID-keyed, current behavior) and `m_PathMap<T>` (new, path-keyed) for path-keyed types. Reject silently when `m_PathMap<T>` already contains the same path (return false). For non-asset types (`SceneNode`, `Component`, `Scene`), keep the existing path-by-UUID behavior plus the `m_NameIndex` warning.
- [x] 1.3 Refactor `EntityManager::Get<T>(path)` to look up `m_PathMap<T>` (the canonical lookup). Make it the primary overload.
- [x] 1.4 Add `EntityManager::Get<T>(uuid_hashcode)` lookup that reads `m_EntityMap<T>` (the existing `Get<T>(size_t)` already does this; verify it's still in place after the refactor).
- [x] 1.5 Refactor `EntityManager::Remove<T>(path)` to remove from both `m_EntityMap<T>` and `m_PathMap<T>` (path-keyed entry first, then match by UUID hashcode).
- [x] 1.6 Refactor `EntityManager::ForEach<T>(fn)` to walk `m_PathMap<T>` instead of `m_EntityMap<T>`. This makes duplicate iteration structurally impossible.
- [x] 1.7 Refactor `EntityManager::Count<T>()` to return `m_PathMap<T>.size()`.
- [x] 1.8 Keep `EntityManager::IndexName` for non-path-keyed types only (drop the warning for path-keyed types). The `m_NameIndex` lookup for path-keyed types is now a name → path parse + `Get<T>(path)` alias.

## 2. Asset types: name derived from path

- [x] 2.1 Add `Texture::SetPath(const std::string&)` that updates `m_FilePath` and `m_Name` (basename) atomically. Existing `GetFilePath()` continues to return `m_FilePath`. (engine/Fury/Texture.h / .cpp)
- [x] 2.2 Add `Material::SetPath(const std::string&)` analogously. (engine/Fury/Material.h / .cpp)
- [x] 2.3 Add `Mesh::SetPath(const std::string&)` analogously. (engine/Fury/Mesh.h / .cpp)
- [x] 2.4 Add `AnimationClip::SetPath(const std::string&)` analogously. (engine/Fury/AnimationClip.h / .cpp)
- [x] 2.5 Add `ParticleSystem::SetPath(const std::string&)` analogously. (engine/Fury/ParticleSystem.h / .cpp)
- [x] 2.6 Add `Heightmap::SetPath(const std::string&)` analogously. (engine/Fury/Heightmap.h / .cpp)
- [x] 2.7 Add `OceanWaves::SetPath(const std::string&)` analogously. (engine/Fury/OceanWaves.h / .cpp)
- [x] 2.8 On each entity's `Load` (the `Entity::Load` chain already restores `m_Name` and `m_UUID`), after the type-specific load reads the `path` field, set `m_Name` to `basename(m_FilePath)` to normalize legacy broken-name data.

## 3. EntityManager callers: handle `Add`'s return value

- [x] 3.1 `engine/Fury/Scene.cpp` `Scene::Load`: drop the existing same-name skip guards in the top-level `textures` / `materials` / `meshes` / `animations` / `particleSystems` / `heightmaps` array loaders and the material-texture registration pass. Each `m_EntityManager->Add` call now goes through the new path-keyed `Add`; check the bool return and log a one-time `FURYW` for the duplicate path (one log per path per load).
- [x] 3.2 `engine/Fury/LuaBindings.cpp` `Importer::MergeInto`: verify the `target_em->Add(t)` calls handle the new bool return. Same-path entries in the source are absorbed by the target's existing canonical entry; no log needed.
- [x] 3.3 `engine/Fury/Terrain.cpp` `LoadTerrainTexture`: drop the strict `existing->GetFilePath() == path` re-check. `Get<T>(path)` now returns an entry that *is* at that path. If the entry's content is invalid, call `SetPath(path)` then `CreateFromImage(path, ...)` to heal it in place. If the entry doesn't exist, create a new Texture and `Add` it (the new `Add` may reject it if a concurrent caller just added the same path — log + return nullptr).
- [x] 3.4 `engine/Fury/Terrain.cpp` `ResolveHeightmapAsset`: analogous drop of the strict re-check.
- [x] 3.5 `engine/Fury/Pipeline.cpp` `Pipeline::Load` (textures array loader): check the `m_EntityManager->Add` return and log a `FURYW` on path collision.
- [x] 3.6 `engine/Fury/GltfImporter.cpp` `GltfImporter` (texture registration paths): verify the `em->Add(tex)` calls handle the bool return. GltfImporter creates fresh UUIDs per import run, so collisions are unlikely; on collision, log and continue.
- [x] 3.7 Audit any remaining `em->Add(` callsites via grep (`grep -rn 'EntityManager.*Add\|->Add(' engine/Fury/`). For each, verify the bool return is consumed.

## 4. Material texture rebinding on load

- [x] 4.1 `engine/Fury/Material.cpp` `Material::Load` (the textures array loader inside `Material::Load`): after `Texture::Create("temp")` + `texture->Load(node)`, the texture's `m_Name` and `m_FilePath` are restored from disk. Call `texture->SetPath(texture->GetFilePath())` to normalize `m_Name` to the basename. The texture's `Add` call may be rejected if the path is already in the EM (canonical entry from the top-level textures array). The material's `SetTexture(key, ptr)` call should bind to the canonical entry via `em->Get<Texture>(path)` after the `Add` returns.

## 5. Content Browser: two-pane layout

- [x] 5.1 `engine/Fury/Editor/EditorWindows.cpp`: refactor `RenderContentBrowserWindow` to use two `ImGui::BeginChild` panes side-by-side, with a drag-handle splitter between them. Left pane ("tree", default 220 px) hosts the folder tree; right pane ("tiles") hosts the existing tile grid logic.
- [x] 5.2 Implement `RenderContentTree(Scene&)` (or extract to a helper file) that draws the folder tree. Two root nodes: `content/` (from `dirname(Editor::GetCurrentScenePath())`) and `engine/` (from `FileUtil::GetAbsPath() + "Resource/"`).
- [x] 5.3 Implement folder expansion state persisted via `imgui.ini` Settings (`ContentBrowser.TreeExpanded.<path>` = bool). Persist the currently selected folder (`ContentBrowser.SelectedFolder` = string).
- [x] 5.4 Refactor tile ID derivation from `type.name() + ":" + name` to `type.name() + ":" + path`. Drop the `DedupeTiles` walk (path-keyed uniqueness makes it unnecessary). Update `g_SelectedAsset`'s comparison to match.
- [x] 5.5 Filter the tile grid to assets whose `m_FilePath` is under the currently selected folder (using a path-prefix check with a separator boundary: `path.substr(0, folder.size()) == folder && path[folder.size()] == '/'`).
- [x] 5.6 Update the rename action: opening `InputText` accepts a new basename; `Texture::SetPath(dir + new_basename)` updates the path-keyed entry atomically. Drop the legacy rename's "first-match by name" semantics.
- [x] 5.7 Update the delete action: `em->Remove<T>(path)` removes the canonical entry.
- [x] 5.8 Update `EditorAssetPicker` (engine/Fury/Editor/EditorAssetPicker.cpp) to use the same two-pane layout, returning the picked asset's path.

## 6. Persistence: imgui.ini keys

- [x] 6.1 Add the persistence keys in the existing `FuryEditor` Settings handler (the same handler used by the QoL-improvements change). Keys: `ContentBrowser.TreeExpanded.<path>` (per folder), `ContentBrowser.SelectedFolder` (single string).
- [x] 6.2 Reset `g_SelectedAsset` to `std::nullopt` on first launch under this change (one-time UX; subsequent launches read it from imgui.ini).

## 7. Lua API

- [x] 7.1 Verify `Scene.GetTexture(path)` continues to work (the existing `Scene.GetTexture` binding already passes the name through to `em->Get<Texture>(name)`; under the new map, `Get<T>(name)` becomes an alias for `Get<T>(path)` after a name-as-path parse).
- [x] 7.2 Verify `Scene.ForEachTexture` continues to iterate the EM's textures (now from the path-keyed map; structurally de-duplicated).
- [x] 7.3 Verify `Scene.ForEachMaterial`, `Scene.ForEachMesh`, `Scene.ForEachAnimationClip`, etc. continue to work for the corresponding types.

## 8. Tests

- [x] 8.1 Create `tests/lua/asset_path_identity.lua`: load a scene with two same-path textures in the top-level array, assert the EM holds exactly one; run a save+reload cycle and assert the count stays at one. Run via `furye exec Projects/ocean/ocean_island.bin tests/lua/asset_path_identity.lua` — the legacy `ocean_island.bin` should collapse 30 → 14 entries on first load, and 14 → 14 across 5 reload cycles.
- [x] 8.2 Create `tests/lua/content_browser_folders.lua`: build a synthetic scene with a Terrain whose textures live under a known folder, load it, and verify `Scene.ForEachTexture` iterates the expected count and `Scene.GetTexture(path)` returns the canonical entry for each unique path.
- [x] 8.3 Verify the existing `tests/lua/scene_roundtrip.lua` (or whatever the canonical round-trip test is) still passes (structural counts stable across save+reload).
- [x] 8.4 Verify the existing `tests/lua/terrain_physics.lua` still passes (Terrain's heightmap + physics flow unchanged).

## 9. ASAN verification

- [x] 9.1 Build with `-fsanitize=address` (the existing `build-asan/` configuration) and run `tests/lua/asset_path_identity.lua` 5+ times to flush out any use-after-free in the new path-keyed map (entity destructors, remove-from-map paths).
- [x] 9.2 Run the editor (furye) with the bundled `Projects/ocean/ocean_island.bin` and verify it loads cleanly under ASAN. Save the file once and confirm the new file has only 14 texture entries (vs. 30+ in the legacy file).

## 10. Documentation

- [x] 10.1 Update `docs/CLAUDE.md` (or the appropriate docs page) with a short note about path-as-identity: "Asset identity = file path. The EM's `Add` is idempotent on path. `Get<T>(name)` is now a thin alias for `Get<T>(path)`."
- [x] 10.2 Update `docs/LUA_API.md` (regenerated by `FURY_DOCGEN` if it's auto-generated) with the new behavior if it differs from the documented contract.