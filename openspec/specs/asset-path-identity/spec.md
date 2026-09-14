# asset-path-identity

## Purpose

Make file path the canonical asset identity in `EntityManager`. Two assets that point to the same file cannot coexist. UUID becomes a per-instance property kept for serialization stability and `Material` ↔ `Texture` reference rebinding across save/load — but it is no longer the lookup key. This eliminates the legacy "6x same-name terrain texture" pathology by construction (the EM cannot hold same-path duplicates) and removes the brittle dual-identity system where name-based lookups and UUID-based identity could disagree.

## Requirements

### Requirement: `EntityManager` SHALL key every asset by its file path

`EntityManager` (engine/Fury/EntityManager.h) SHALL maintain, for every entity type registered via `Add<T>`, a primary lookup map keyed by the entity's file path (`m_FilePath` for `Texture`/`Material`/`Mesh`/`Heightmap`/`OceanWaves`/`ParticleSystem`/`AnimationClip`; the path-equivalent stable identifier for the type). The path is the canonical identity. The UUID (per-entity `Entity::m_UUID`) remains on the entity as a property for serialization round-tripping, but UUID is NOT the lookup key.

`Add<T>(ptr)` SHALL:
- Reject the insertion if the path-keyed map already contains `ptr->GetPath()` and return `false`. The first registered entry for a given path wins.
- Insert into the path-keyed map when the path is fresh, and into a parallel UUID-keyed map keyed by `ptr->GetHashCode()`. Return `true`.
- Both insertions must succeed for the call to return `true`; if either insertion fails the call SHALL roll back (no half-inserted state).

`Get<T>(path)` SHALL return the path-keyed entry (the canonical asset). `Get<T>(uuid)` SHALL return the UUID-keyed entry (the specific instance handle). The legacy `Get<T>(name)` SHALL remain available as a thin alias that calls `Get<T>(path)` after deriving the path from the name via the same name-as-path convention the engine has used since day one (asset `m_Name` is the file path). For names that don't parse as paths (e.g. a Lua-created SceneNode named "RootNode"), the alias SHALL fall back to the legacy UUID-keyed lookup so non-asset entities keep working.

`Remove<T>(path)` SHALL erase the path-keyed entry and the corresponding UUID-keyed entry (matching the same path → matching the same UUID → remove both).

`ForEach<T>(fn)` SHALL walk the path-keyed map. Because the path-keyed map cannot contain duplicates, `ForEach` cannot return duplicates — the legacy `DedupeTiles` presentation-layer collapse in the Content Browser becomes unnecessary.

`Count<T>()` SHALL return the path-keyed map's size.

The legacy `m_NameIndex` first-registered-wins name index SHALL remain for non-asset entities (SceneNodes, etc.) where names are arbitrary strings and the engine has no concept of "the canonical node named X" beyond first-write-wins. The "first-registered-wins with one-shot warning" behavior SHALL be retained as-is for non-path-keyed entity types only.

#### Scenario: First Add wins; second same-path Add is rejected

- **WHEN** `em->Add<Texture>(texA)` succeeds where `texA->GetPath() == "TerrainIsland/snow.png"`
- **AND** `em->Add<Texture>(texB)` is called with `texB->GetPath() == "TerrainIsland/snow.png"` and a fresh UUID
- **THEN** the second `Add` returns `false`
- **AND** `em->Get<Texture>("TerrainIsland/snow.png")` still returns `texA`
- **AND** `em->ForEach<Texture>` does not iterate `texB` (it was never inserted)
- **AND** `texB`'s shared_ptr still goes out of scope at the call site (the rejected texture is freed)

#### Scenario: Distinct UUIDs for the same path still produce one canonical entry

- **WHEN** a legacy scene file contains six `Texture` entries all named `"TerrainIsland/snow.png"` with distinct UUIDs and identical `m_FilePath`
- **AND** `Scene::Load` iterates the top-level `textures` array and calls `m_EntityManager->Add<Texture>(entry)` for each
- **THEN** the first entry is inserted; the next five `Add` calls return `false`
- **AND** the EM holds exactly one Texture for `"TerrainIsland/snow.png"`
- **AND** the EM's UUID-keyed `Texture` map holds six entries (one per legacy UUID — they are kept alive so the rest of `Scene::Load` can still rebind Material shared_ptr references that point at the legacy UUIDs)
- **AND** `Scene::Save` iterates the path-keyed map, so the next save writes one entry for `"TerrainIsland/snow.png"`

#### Scenario: ForEach count equals unique-path count after Save+Load round-trip

- **WHEN** a scene is saved and reloaded
- **AND** the saved file contains N entries in the top-level `textures` array (some of which may be legacy same-name duplicates)
- **THEN** after `Scene::Load`, `em->ForEach<Texture>` iterates exactly U entries (the unique paths)
- **AND** a second `Save` writes exactly U entries (not N)

#### Scenario: Get by path returns the same canonical entry across save+load

- **WHEN** a `Texture` named `"TerrainIsland/snow.png"` is registered in the EM with UUID-X
- **AND** the scene is saved and reloaded
- **THEN** `em->Get<Texture>("TerrainIsland/snow.png")` returns a `Texture::Ptr` whose `GetPath()` is `"TerrainIsland/snow.png"`
- **AND** its UUID MAY differ from UUID-X (because `Texture::Create("temp")` mints a fresh UUID that `Texture::Load` overwrites with the saved one — the UUID is stable on disk, but the in-process UUID after a reload roundtrip equals the saved one)

#### Scenario: Material shared_ptr reference rebinds to the canonical texture on load

- **WHEN** a `Material` is saved with a `Texture` slot pointing at a Texture whose UUID was U-saved
- **AND** the scene is reloaded
- **THEN** the legacy same-name duplicates in the textures array each register with their saved UUID
- **AND** `Material::Load` walks its textures and creates a fresh `Texture` per slot with the saved UUID
- **THEN** the fresh `Texture` is added to the path-keyed map; if a same-path entry already exists, the Add is rejected and the existing canonical texture wins
- **AND** the Material rebinds its `Texture::Ptr` slot to the canonical entry via `Get<Texture>(path)`

### Requirement: Every asset's `m_Name` SHALL be derived from its file path

`Texture`, `Material`, `Mesh`, `AnimationClip`, `ParticleSystem`, `Heightmap`, and `OceanWaves` SHALL each expose a `SetPath(const std::string&)` method (and `GetPath()` already exists) that updates `m_FilePath` (or path-equivalent). On construction, the entity's `m_Name` SHALL be set to the basename of `m_FilePath` (the file name without the directory). Renaming the path SHALL atomically update `m_Name`, the path-keyed map entry, and any reverse indexes. The Content Browser SHALL show the basename as the tile label and the full path on hover.

#### Scenario: Texture name matches basename after load

- **WHEN** a `Texture` is loaded with `m_FilePath = "TerrainIsland/snow.png"`
- **THEN** `texture->GetName() == "snow.png"`

#### Scenario: Rename updates both name and path map atomically

- **WHEN** a `Texture` in the EM has `m_FilePath = "old/snow.png"` and `m_Name = "snow.png"`
- **AND** `texture->SetPath("new/snow.png")` is called
- **THEN** `texture->GetName() == "snow.png"` (basename unchanged)
- **AND** `em->Get<Texture>("old/snow.png")` returns `nullptr`
- **AND** `em->Get<Texture>("new/snow.png")` returns the texture
- **AND** the path-keyed map is consistent (no entries under the old path; exactly one under the new)

### Requirement: `Terrain::LoadTerrainTexture` SHALL reuse the path-keyed entry without re-creation

`engine/Fury/Terrain.cpp` `LoadTerrainTexture(path, srgb)` (the file-local helper used by `Terrain::ReloadTextures`) SHALL call `em->Get<Texture>(path)` (path lookup) to find the existing entry. The strict `existing->GetFilePath() == path` re-validation that existed previously SHALL be removed because path equality is now guaranteed by the EM's path-keyed invariant: an entry returned by `Get<Texture>(path)` always has `m_FilePath == path`. The legacy broken-path scenario (`m_FilePath == "snow.png"` while the Terrain asks for `"TerrainIsland/snow.png"`) cannot occur once `m_Name` is derived from `m_FilePath` on every construction, because the legacy data's name-vs-path drift is fixed at load time by `Entity::SetName(path)` semantics on the top-level textures array.

#### Scenario: LoadTerrainTexture reuses the canonical entry

- **WHEN** the EM holds a `Texture` at `"TerrainIsland/snow.png"`
- **AND** `Terrain::Rebuild` calls `LoadTerrainTexture("TerrainIsland/snow.png", true)` for layer 3
- **THEN** the existing entry is returned (no new UUID minted, no `em->Add` call)
- **AND** the EM holds the same Texture::Ptr before and after the call

#### Scenario: LoadTerrainTexture creates a fresh entry for a path not in the EM

- **WHEN** the EM does not hold a `Texture` at `"new/grass.png"`
- **AND** `Terrain::Rebuild` calls `LoadTerrainTexture("new/grass.png", true)`
- **THEN** a new `Texture` is created with `m_FilePath == "new/grass.png"` and a fresh UUID
- **AND** the new entry is added to the EM via `Add<Texture>`
- **AND** the new entry is returned to the caller

### Requirement: `Scene::Load` SHALL NOT need a same-name skip guard at the top-level array loaders

`engine/Fury/Scene.cpp` `Scene::Load` (the top-level `textures` / `materials` / `meshes` / `animations` / `particleSystems` / `heightmaps` array loaders) currently uses a same-name skip pattern to absorb legacy same-name duplicates. Under path-as-identity, this guard is no longer needed because the EM's `Add` rejects same-path at the source. The skip guards SHALL be removed; the loaders SHALL call `m_EntityManager->Add<T>(entity)` directly and check the bool return to log a one-time FURYW message naming the duplicate path (useful for diagnosing leftover legacy data; not a hard error).

The material-texture registration pass (`m_EntityManager->ForEach<Material>` walking `mat->GetTextures()` and adding each texture to the EM) SHALL likewise drop its same-name skip guard; the new `Add` returns `false` for path collisions, which is the correct behavior (no silent duplicates, no warning spam).

#### Scenario: Scene::Load loader logs and skips same-path duplicates

- **WHEN** the top-level `textures` array in a saved scene contains six entries all with `m_FilePath == "TerrainIsland/snow.png"` and distinct UUIDs
- **AND** `Scene::Load` processes the array
- **THEN** the first entry's `Add` returns `true`
- **AND** entries 2 through 6 each return `false`
- **AND** a single `FURYW` message names the duplicate path once (not six times)
- **AND** the EM holds exactly one `Texture` for `"TerrainIsland/snow.png"` after the loop

#### Scenario: Round-trip count is stable

- **WHEN** a scene is saved and reloaded five times in succession
- **THEN** `em->ForEach<Texture>` returns the same count on every reload
- **AND** `em->ForEach<Material>` returns the same count on every reload
- **AND** the same count equals the unique-path count in the active scene

### Requirement: The legacy "first-registered-wins name index with one-shot warning" behavior SHALL be retained for non-asset entity types

`EntityManager`'s `IndexName` (engine/Fury/EntityManager.h) historically served two purposes: as a lookup for non-asset entities (`SceneNode`, `Component`, etc., where names are arbitrary strings), and as a backup dedupe mechanism for assets (the old broken "same name with different UUID is fine" behavior). With path-as-identity, the second purpose is gone. The first purpose remains: a `SceneNode` named `"RootNode"` is looked up by name across the scene graph; `EntityManager::Get<T>(name)` must continue to work for non-asset types.

The legacy `m_DupWarned` one-shot warning SHALL continue firing for non-asset entity types where two same-named entries are inserted. It SHALL NOT fire for path-keyed asset types — `Add`'s same-path rejection is silent because it's the documented contract, not a warning case.

#### Scenario: Non-asset entity name lookup still works

- **WHEN** a `SceneNode` named `"RootNode"` is added with UUID-X
- **AND** `em->Get<SceneNode>("RootNode")` is called
- **THEN** the `SceneNode` is returned
- **AND** the lookup uses the `m_NameIndex` (which still maps name → hashcode for non-asset types)

#### Scenario: Asset Add is silent on path collision

- **WHEN** `em->Add<Texture>(texA)` succeeds with path `"x.png"`
- **AND** `em->Add<Texture>(texB)` is rejected with path `"x.png"`
- **THEN** the rejection is silent (no FURYW / FURYE log)
- **AND** the rejection is reflected in the bool return value (callers must check)

#### Scenario: SceneNode same-name insert still warns

- **WHEN** `em->Add<SceneNode>(nodeA)` succeeds with name `"RootNode"`
- **AND** `em->Add<SceneNode>(nodeB)` is called with name `"RootNode"` and a fresh UUID
- **THEN** the second `Add` succeeds (non-asset types do not reject on name)
- **AND** `IndexName` emits the one-shot `FURYW` duplicate warning for `("SceneNode", "RootNode")`

### Requirement: All `Add` call sites in the engine MUST check the bool return

Every engine caller of `EntityManager::Add<T>` (including `Scene::Load`, `Scene::Save`'s reverse-lookup paths, `Importer::MergeInto`, `Pipeline::Load`, `Terrain::LoadTerrainTexture`, `Heightmap::ResolveHeightmapAsset`, and any Lua bindings that wrap Add) MUST inspect the bool return and treat `false` as "the entry was already present — reuse the existing canonical one via `Get<T>(path)`." Legacy callers that ignored the return value (which was safe when Add was idempotent on UUID) MUST be updated to handle the rejection explicitly.

#### Scenario: Scene::Load rejects same-path and recovers via Get

- **WHEN** `Scene::Load`'s top-level textures loader processes a legacy file containing two same-path textures
- **THEN** the second `Add` returns `false`
- **AND** the loader logs a single `FURYW` line naming the duplicate path
- **AND** the loader continues to the next entry (no exception, no abort)

#### Scenario: MergeInto rejects same-path and recovers

- **WHEN** `Importer::MergeInto(target, source)` is called and both scenes hold a `Texture` at the same path
- **THEN** the `Add` for the source's texture returns `false`
- **AND** the merge continues — the target scene's canonical texture is used
- **AND** `target->Get<Texture>(path)` returns the canonical entry (not the source's)

#### Scenario: Lua bindings expose Add's rejection to callers

- **WHEN** a Lua script calls `scene:GetEntityManager():Add<Mesh>(mesh)` and the mesh's path is already registered
- **THEN** the Lua-side boolean return is `false`
- **AND** the script can call `scene:GetEntityManager():Get<Mesh>(path)` to recover the canonical entry