# Design: asset-path-identity-and-content-folders

## Context

The engine has two competing identity systems for `EntityManager` assets. `EntityManager::Add` keys on `Entity::GetHashCode()`, which is `hash(m_UUID)` — so each insertion is identified by an instance UUID. Every consumer-facing lookup (`EntityManager::Get<T>(name)`, the Content Browser tile ID derivation `type.name() + ":" + name`, `Terrain::LoadTerrainTexture`, `EditorAssetPicker`) treats names as the identity, where names are typically the file path string. The two systems drift apart the moment the legacy data's `m_FilePath` field diverges from the entity's `m_Name` field — and they did drift, because the legacy `LoadTerrainTexture` had a strict `existing->GetFilePath() == path` check that forced a fresh `Texture::Create` (minting a new UUID) every time the path check failed. The result was `ocean_island.bin` carrying 30 same-named terrain textures (5 unique paths × 6 copies each).

The Content Browser's `DedupeTiles` masks the ImGui ID-conflict symptom but doesn't address the source. A more durable fix is to make the file path the canonical lookup key — structurally, two assets at the same path cannot coexist, and the legacy "6x same-name" pathology becomes unrepresentable.

Once paths are the identity, the Content Browser naturally grows from a flat tile grid into a two-pane UE-style layout: a folder tree on the left (two roots: `content/` = scene's containing folder, `engine/` = `examples/Resource/`), and a tile grid on the right scoped to the selected folder. Paths become user-facing identifiers in tile labels (basename), hover tooltips (full path), rename (path moves with the rename), and the picker dialog.

## Goals / Non-Goals

**Goals:**
- Make `EntityManager` keyed by file path for every asset type (`Texture`, `Material`, `Mesh`, `AnimationClip`, `ParticleSystem`, `Heightmap`, `OceanWaves`).
- Make same-path `Add` rejections structural; remove the legacy "first-registered-wins" silent-dup behavior for assets.
- Keep UUID as a per-instance property for serialization round-trips and `Material` ↔ `Texture` shared_ptr rebinding.
- Drop the same-name dedupe guards from `Scene::Load` (the EM now rejects at insertion).
- Add a folder-tree sidebar to the Content Browser with two roots (`content/` and `engine/`).
- Scope tile grids to the selected folder.
- Keep all `imgui.ini` settings except the now-meaningless selection key.

**Non-Goals:**
- Changing the on-disk file format. Existing `.json`/`.bin` files load unchanged because they already carry `m_FilePath` per asset.
- Touching non-asset entity types (`SceneNode`, `Component`). Their name index keeps "first-registered-wins with one-shot warning."
- A real filesystem-backed browser (read/write through OS dialogs). The tree is a derived view from the EM's registered assets; folders with no assets are hidden.
- Per-folder thumbnails or caching strategies. The existing per-asset thumbnail pipeline (mesh disk cache, material checkerboard, texture direct sample) is reused.
- Drag-and-drop between folders. The rename flow updates paths in place; no separate drag handler.
- Asset deletion with disk-side file removal. Delete removes from the EM only; the file on disk is untouched.

## Decisions

### Decision 1: Path-keyed map alongside UUID-keyed map, not a replacement

Two maps coexist in `EntityManager`:

- **`m_PathMap<T>`**: keyed by `string` (the file path). Insertion order matters; `Add<T>(ptr)` rejects same-path. `Get<T>(path)` returns from this map. `ForEach<T>` walks this map. `Count<T>` returns its size.
- **`m_EntityMap<T>`** (the existing one): keyed by `size_t` (UUID hash). `Get<T>(uuid)` continues to work for deserialization rebinding. `Material::Load` walks its texture slots, creates a fresh `Texture` per slot with the saved UUID, and `Add`s it. The same-path rejection ensures only the canonical entry survives; the fresh instance is discarded (its shared_ptr drops at the lambda exit). The material then `Get<T>(path)`s the canonical entry and rebinds its `Texture::Ptr` slot to it.

**Rationale**: Replacing the UUID-keyed map outright would break `Material::Load`'s in-place texture construction (it relies on `Texture::Create("temp")` + `Entity::Load` to restore UUIDs from disk). Keeping both maps lets the deserialization path use UUID identity for instance rebinding while the user-facing lookup uses path identity for canonicalization.

**Alternatives considered**:
- Single map keyed by path; UUIDs become a secondary index. Discarded: UUID-keyed deserialization would need to do two lookups (path then uuid-by-hash), and `Material::Load`'s same-slot-temporary pattern wouldn't fit.
- Single map keyed by UUID; require the caller to look up by path through a separate `m_PathToUUID` index. Rejected: the EM's primary map type would still be UUID-keyed, leaving the "two identity systems" footgun in place.

### Decision 2: `Entity::m_Name` derived from path on construction, with a setter that keeps them in sync

`Texture`, `Material`, `Mesh`, `AnimationClip`, `ParticleSystem`, `Heightmap`, `OceanWaves` each expose `SetPath(const std::string&)` (with `GetPath()` already existing). On construction, `m_Name` is set to the basename of `m_FilePath`. `SetPath` updates both `m_Name` (basename) and the path-keyed map atomically. The `EntityManager::m_NameIndex` for these types is removed — it served the legacy "find by name" lookup, but `Get<T>(name)` becomes a thin alias that parses the name as a path and dispatches to `Get<T>(path)`.

**Rationale**: The legacy data's name-vs-path drift (`name = "TerrainIsland/snow.png"`, `path = "snow.png"`) was the proximate cause of `LoadTerrainTexture`'s strict check failure. By deriving `m_Name` from `m_FilePath` at construction, the drift cannot occur: name and path are the same string at every point in the asset's lifetime.

**Alternatives considered**:
- Keep `m_Name` independent from `m_FilePath`; force callers to set both. Rejected: doesn't actually fix the drift, just pushes it to every callsite.
- Make `m_FilePath` derived from `m_Name`. Rejected: `m_FilePath` is the canonical identity; deriving it from a display string would invert the relationship.

### Decision 3: `EntityManager::Add<T>` returns `false` silently for path-keyed types on same-path; non-asset types keep their `IndexName` warning

The contract:
- Path-keyed types (`Texture`, `Material`, Mesh, AnimationClip, ParticleSystem, Heightmap, OceanWaves): `Add<T>(ptr)` rejects same-path silently and returns `false`. The first registered entry for a path wins. No log message — this is the documented contract.
- Non-path-keyed types (`SceneNode`, `Component`, `Scene`, future arbitrary-named entities): `Add<T>(ptr)` keeps the existing `m_NameIndex` first-registered-wins behavior, including the one-shot `FURYW` duplicate warning via `m_DupWarned`.

**Rationale**: Warning spam from "duplicate path" would be a regression for users who save+load scenes with legacy same-path data. Asset paths being canonical means duplicates shouldn't log; they're a contract violation to be detected by the caller's `if (!em->Add(...))` check, not surfaced to logs.

**Alternatives considered**:
- One-shot warning for path-keyed duplicates (like non-asset types). Rejected: the contract is structural; warnings would imply "you can recover," but the recovery is always `Get<T>(path)` to find the canonical entry, which is the normal API path.
- No first-registered-wins for non-asset types either. Rejected: `SceneNode` name uniqueness is genuinely soft (users can have multiple "Light" nodes under different parents); a hard reject would break existing scenes.

### Decision 4: Folder tree is a derived view from the EM, not a disk scan

The tree's two roots are:
- `content/`: the directory containing the active scene file (`dirname(Editor::GetCurrentScenePath())`).
- `engine/`: `examples/Resource/` (resolved via `FileUtil::GetAbsPath() + "Resource/"`).

Folders in the tree are constructed by walking every asset in the EM, computing the path's directory segments, and grouping. A folder is shown iff at least one EM-registered asset lives under it. Empty folders on disk that have no assets are hidden.

**Rationale**: The tree is for *navigating assets*, not for browsing the filesystem. Showing empty folders would confuse users (a folder with no assets is unreachable through the UI). And because path-as-identity makes the EM the canonical source of "what assets exist," deriving the tree from the EM keeps the two views in sync automatically — no separate filesystem watcher needed.

**Alternatives considered**:
- Disk-scan via `FileUtil::ListDirectory` and match EM assets to disk entries. Rejected: requires a filesystem watcher for the "asset added/removed" case; doesn't handle the case where a file exists on disk but isn't in the EM (show? don't show? — neither is obviously right).
- Show all folders regardless of asset count. Rejected: the user can't act on empty folders in this UI, so showing them is noise.

### Decision 5: Tile ImGui IDs use `type.name() + ":" + path`

Tile ID derivation changes from `type.name() + ":" + name` to `type.name() + ":" + path`. Because paths are unique by construction, ID collisions are structurally impossible — no `DedupeTiles` walk needed. Selection state (`g_SelectedAsset`) keys on `(type, path)`.

**Rationale**: The legacy ID derivation used names, which collided when legacy data had same-named different-UUID duplicates (the original bug). Paths collide only when assets collide, which can't happen.

**Alternatives considered**:
- Keep IDs as `type.name() + ":" + name` and rely on `DedupeTiles` for safety. Rejected: keeps the brittle workaround that masks the symptom.
- Use a hash of the path. Rejected: humans sometimes need to read IDs in logs and ImGui debug output; path strings are more informative.

### Decision 6: `Scene::Save` iterates the path-keyed map; legacy same-name skip guards in `Scene::Load` are removed

`Scene::Save` walks `m_EntityManager->ForEach<Texture>()` etc. Under path-as-identity, this iteration produces exactly the unique-path set. Legacy scenes that contained 6 same-name textures at one path will, on first save, write 1 entry for that path. The legacy `Scene::Load` "skip if already in EM" guards (in the top-level array loaders and the material-texture registration pass) are removed because `Add<T>` rejecting same-path is the single source of truth for dedupe.

**Rationale**: With path-keyed `Add` rejecting at insertion, the loaders don't need their own guards. Keeping them would be dead code AND would emit redundant `FURYW` messages (the guards already log on the second copy).

**Alternatives considered**:
- Keep the loaders' guards as defensive programming. Rejected: dead code; `Add`'s rejection is the canonical check.
- Make the loaders' guards the only dedupe and remove `Add`'s rejection. Rejected: callers in `Importer::MergeInto`, `Pipeline::Load`, Lua bindings, and elsewhere need the `Add` rejection too.

### Decision 7: Tree expansion state and selection persist via `imgui.ini` Settings

The Content Browser stores:
- `ContentBrowser.TreeExpanded.<path>` = `bool` (one entry per expanded folder path).
- `ContentBrowser.SelectedFolder` = `string` (the currently selected folder's full path).

Both go through the existing `Editor`'s `FuryEditor` Settings handler (the `imgui.ini` `[Window][FuryEditor]` section is already used for the QoL-improvements persistence; extending it is a no-op).

**Rationale**: The QoL improvements change introduced this pattern. Reusing it keeps the persistence style consistent.

**Alternatives considered**:
- New `[Window][Content Browser]` keys via ImGui's built-in window settings. Rejected: those persist window position/size, not application state.
- Global Lua state keyed on folder paths. Rejected: editor-only state shouldn't live in Lua scripts.

## Risks / Trade-offs

- **Risk: Refactoring `EntityManager::Add` breaks every caller that ignored its return value.**
  → Mitigation: All engine `Add` callsites (`Scene::Load`, `Scene::Save`'s reverse paths, `Importer::MergeInto`, `Pipeline::Load`, `Terrain::LoadTerrainTexture`, `Heightmap::ResolveHeightmapAsset`, Lua bindings) are explicitly listed in the spec and audited in tasks. The audit pass walks each callsite and adds an `if (!em->Add(...))` check that logs a one-time `FURYW` for path-keyed duplicates (still useful for diagnosing leftover legacy data) and recovers via `Get<T>(path)` for the canonical entry.

- **Risk: Legacy scenes with broken `m_Name` (e.g. bare basename `"snow.png"` while `m_FilePath` is `"TerrainIsland/snow.png"`) silently lose their display label after a round-trip.**
  → Mitigation: At `Scene::Load` time, after the texture entry is loaded, the loader normalizes `m_Name` to `basename(m_FilePath)` for path-keyed types. Legacy broken-name data heals on first save. The Content Browser shows the basename, which becomes consistent with the new identity.

- **Risk: `Editor::GetCurrentScenePath()` returns an absolute path; converting to a relative path for the `content/` tree requires consistent CWD.**
  → Mitigation: Use the directory part of the absolute path and strip the `FileUtil::GetAbsPath()` prefix when present, falling back to the absolute path otherwise. The CWD is fixed at `examples/` for both `fury` and `furye` launches, so the prefix is stable.

- **Risk: Two assets at different paths with the same basename (e.g. `content/A/cube.asset` and `content/B/cube.asset`) have the same display label.**
  → Mitigation: The asset editor's "already-open" lookup keys on path (per `asset-editor-windows` spec). The Content Browser's tile shows the basename with the full path on hover; clicking expands the folder containing the asset, so the user can navigate by path, not just basename.

- **Risk: `MergeInto` between two scenes that hold the same asset path loses source-side edits.**
  → Mitigation: `MergeInto`'s target EM already wins for path-keyed entries (first-registered-wins). The spec already calls this out; users editing the same asset in two scenes and merging need to resolve the conflict manually (e.g., copy the asset to a new path in one scene before merging).

- **Risk: `Material::Load`'s texture rebinding is order-dependent when the material's texture slot shares a path with another material's slot.**
  → Mitigation: Each `Material::Load` runs after the top-level textures array has been processed. By the time materials load, the path-keyed map already has all the canonical texture entries. The material's `Get<T>(path)` always finds the canonical entry regardless of load order.

- **Risk: The Lua binding `Scene.GetTexture(name)` still uses name lookup semantics, but the underlying map is path-keyed.**
  → Mitigation: The `name` argument is treated as a path. Existing scripts that pass file paths continue to work. Scripts that pass arbitrary display names (none exist in the current codebase) would need updating.

## Migration Plan

1. **Land `asset-path-identity` first**: rewrite `Add`/`Get`/`Remove`/`ForEach`/`Count` to use the path-keyed map. Audit every `Add` callsite in the engine. Drop the same-name skip guards in `Scene::Load`. Run existing tests; the test for `Scene::Load` count stability (already proposed in `tests/lua/scene_load_dedupe.lua`) becomes the canonical regression test, repurposed for the path-keyed invariant.
2. **Land `content-browser-folders`**: split `RenderContentBrowserWindow` into tree + grid panes. Add the path-derived folder tree. Update tile IDs to `type.name() + ":" + path`. Reset selection state on first launch (one-time UX change).
3. **No data migration is required.** Existing `.json`/`.bin` files load unchanged because the saved `m_FilePath` field is already the canonical identity. Legacy scenes with same-name duplicates silently lose the duplicates on first save (logged once via `FURYW` if the audit path emits a message).
4. **Rollback strategy**: if the change fails, revert by reverting the EM keying change. The Content Browser layout falls back to the flat grid (the tree rendering is a new addition; reverting it removes the panes).

## Open Questions

- Should the `content/` root support multiple folders (e.g. a scene loaded from a different project than the current `Projects/`)? The current implementation derives `content/` from the active scene's path; if the user has multiple scenes open, only one is "active." Decision: single `content/` root, derived from `Scene::Active`'s path. If multi-scene workflows become a need, the `content/` root can become a list.
- Should the tree auto-refresh when assets are added/removed (e.g., after `Import::MergeInto`)? The current implementation walks the EM each frame, so refresh is automatic. Decision: keep frame-by-frame walk; the EM is small enough that this is cheap.
- Should the rename action also rename the underlying file on disk, or only the EM's path? Decision: only EM's path. Renaming the disk file would require touching the file system mid-edit; out of scope for this change.