# mesh-thumbnail-disk-cache

## Purpose

Persistent PNG disk cache for mesh preview thumbnails shown in the Content
Browser, plus the async content-hash workers and in-memory hit index that back
it. Provides the shared "simple Lambert" look used by both the 128×128 mesh
thumbnail and the interactive mesh editor preview so the two stay visually
consistent.

## Requirements

### Requirement: The editor SHALL maintain a disk-backed PNG cache of mesh preview snapshots

The editor SHALL persist mesh preview thumbnails to disk so they survive across editor sessions and warm the in-memory FBO cache on startup. The cache SHALL live under `FileUtil::GetAbsPath("Resource/.thumbcache/")` (created on first use via `std::filesystem::create_directories`). Each thumbnail SHALL be written as a PNG file named `furye_<hex>.png`, where `<hex>` is the 16-character lowercase hexadecimal representation of a 64-bit FNV-1a content hash computed over the mesh's `Positions.Data` bytes followed by its top-level `Indices.Data` bytes followed by each submesh's `Indices.Data` bytes (in submesh order).

The hash SHALL be computed off the main thread via `ThreadUtil::Enqueue`. The GL render (into the in-memory FBO) and the `glReadPixels` + `stbi_write_png` file write SHALL run on the main thread (GL context is main-thread-only). The PNG encode step (`stbi_write_png` itself) MAY be offloaded to a worker thread by capturing the pixel buffer into a `std::vector<unsigned char>` on the main thread and enqueuing a second worker task that performs the encode + file write.

The disk cache SHALL be best-effort: if the cache directory cannot be created, a write fails, or `stbi_write_png` returns failure, the editor SHALL emit a single rate-limited `FURYW` log line and continue serving the thumbnail from the in-memory FBO cache for the current session. Disk-cache failure SHALL NOT break the editor or cause the Content Browser to fall back to the gray placeholder rect as long as the in-memory FBO render succeeds.

#### Scenario: Cache directory is created on first write

- **WHEN** the editor renders a mesh thumbnail for the first time in a fresh working directory
- **AND** `Resource/.thumbcache/` does not exist
- **THEN** the editor creates `Resource/.thumbcache/` via `std::filesystem::create_directories`
- **AND** the thumbnail PNG is written to `Resource/.thumbcache/furye_<hash>.png`

#### Scenario: Cache hit reuses an existing PNG

- **WHEN** the editor renders a mesh thumbnail whose content hash matches an existing `furye_<hash>.png` in the cache directory
- **THEN** the editor loads the PNG via `Texture::CreateFromImage(path, srgb=false, mipMap=false)`
- **AND** blits it into the in-memory thumbnail FBO
- **AND** does NOT invoke `RenderMeshToThumbnail` to re-render the mesh
- **AND** does NOT write a new PNG file

#### Scenario: Cache miss renders and writes

- **WHEN** the editor renders a mesh thumbnail whose content hash has no corresponding `furye_<hash>.png`
- **THEN** the editor runs `RenderMeshToThumbnail` to render the mesh into the in-memory FBO
- **AND** calls `glReadPixels` to read the FBO pixels into a CPU-side buffer
- **AND** writes the buffer to `Resource/.thumbcache/furye_<hash>.png` via `stbi_write_png`

#### Scenario: Disk write failure falls back gracefully

- **WHEN** `stbi_write_png` fails (e.g. disk full, permission denied)
- **THEN** the editor emits one `FURYW` log line with the mesh name and the error
- **AND** the in-memory FBO continues to serve the thumbnail for the current session
- **AND** the Content Browser mesh tile still displays the rendered thumbnail (not the gray placeholder)

#### Scenario: Hash is computed off the main thread

- **WHEN** a mesh's content needs to be hashed (dirty-transition detected)
- **THEN** the editor enqueues a `ThreadUtil` worker task that scans `mesh->Positions.Data` + `mesh->Indices.Data` + submesh indices
- **AND** the worker task does NOT touch any GL state
- **AND** when the worker completes, its result (the 64-bit hash) is delivered to a main-thread callback via `ThreadUtil::Update()`

#### Scenario: PNG encode is offloaded to a worker thread

- **WHEN** the main thread has captured the `glReadPixels` pixel buffer for a thumbnail
- **THEN** the editor enqueues a `ThreadUtil` worker task that calls `stbi_write_png(path, w, h, 4, pixels.data(), row_stride)` on the captured buffer
- **AND** the main thread does NOT block on PNG encoding
- **AND** the worker task does NOT touch any GL state

### Requirement: The editor SHALL re-hash and re-render a thumbnail when the mesh's content changes

The editor SHALL detect mesh content changes by observing the `Buffer::GetDirty()` transition from `true` to `false` (i.e. the mesh was edited and then re-uploaded to the GPU via `UpdateBuffer()`). On the transition, the editor SHALL enqueue an async content-hash computation. When the hash completes, the editor SHALL compare it to the cached `ThumbnailCacheEntry::contentHash` for that mesh's `BufferId`. If the hash differs, the editor SHALL invalidate the in-memory FBO and re-render + re-write the disk PNG. If the hash matches (the edit produced identical content), the editor SHALL keep the existing thumbnail.

`BufferId` change alone (without a dirty transition) SHALL NOT trigger a re-render — a mesh that was re-imported from the same source with identical content SHOULD reuse the cached thumbnail. The content hash is the single source of truth for cache validity.

The editor SHALL NOT re-hash every frame (to avoid thrashing on static meshes). The editor SHALL NOT re-hash while a mesh is continuously dirty (e.g. an animated mesh) — such meshes are documented as a known limitation; the user can right-click → Refresh in the Content Browser to force a re-render.

#### Scenario: Editing mesh vertices triggers a re-render

- **WHEN** the user edits a mesh's vertex positions (e.g. via a future mesh-editing tool or external script)
- **AND** `mesh->SetDirty()` is called and the mesh is subsequently re-uploaded via `UpdateBuffer()`
- **THEN** on the next periodic refresh poll, the editor detects the dirty-transition
- **AND** enqueues an async hash computation
- **AND** the new hash differs from the cached hash
- **AND** the thumbnail FBO is re-rendered and the disk PNG is overwritten

#### Scenario: Identical re-import reuses the cached thumbnail

- **WHEN** a mesh is re-imported from a source file that has not changed
- **AND** the new `Mesh` object has a different `BufferId` (new object identity) but identical vertex + index data
- **THEN** the editor computes the content hash for the new mesh
- **AND** the hash matches the existing `furye_<hash>.png` on disk
- **AND** the thumbnail is loaded from disk (no re-render)

#### Scenario: Static mesh does not trigger per-frame hashing

- **WHEN** a mesh has not been edited for 60 seconds
- **THEN** no hash computation is enqueued for that mesh
- **AND** no `glReadPixels` or `stbi_write_png` call is made for that mesh
- **AND** the Content Browser continues to display the cached thumbnail

#### Scenario: Always-dirty mesh does not thrash

- **WHEN** a mesh is animated every frame (e.g. a skinned character in playback)
- **AND** `mesh->GetDirty()` is true on every frame (no true→false transition)
- **THEN** the editor does NOT enqueue a hash computation
- **AND** the thumbnail may drift out of sync (known limitation)
- **AND** the user can right-click → Refresh in the Content Browser to force a re-render

### Requirement: The editor SHALL run a periodic refresh poll to re-check live mesh tiles against the cache

The editor SHALL call a `RefreshMeshThumbnailCache()` function from `Editor::Tick` every 30 frames (≈0.5s at 60fps). The poll SHALL iterate the currently-visible Content Browser mesh tiles and, for each mesh, check whether a dirty-transition has occurred since the last poll. If so, the poll SHALL enqueue the async hash + re-render pipeline.

The poll SHALL NOT block the main thread. The poll SHALL NOT re-hash meshes that are already in the `hashInFlight` state (an async hash is already running for that mesh). The poll SHALL skip meshes whose `GetMeshThumbnail` would hit the fast path (cached hash matches, dirty=false).

The eviction pass (`EvictStaleMeshThumbnails`) SHALL continue to run every frame from the Content Browser (unchanged). The periodic refresh poll is an addition to, not a replacement of, the per-frame eviction.

#### Scenario: Poll detects a dirty transition and enqueues a hash

- **WHEN** the periodic refresh poll runs at frame N
- **AND** a mesh's `GetDirty()` was true at frame N-30 and is false at frame N
- **THEN** the editor enqueues an async hash computation for that mesh
- **AND** sets `ThumbnailCacheEntry::hashInFlight = true` to prevent duplicate enqueues
- **AND** the existing (stale) thumbnail continues to display until the hash + re-render completes

#### Scenario: Poll skips meshes with an in-flight hash

- **WHEN** the periodic refresh poll runs
- **AND** a mesh's `hashInFlight == true` (an async hash is already running)
- **THEN** the poll does NOT enqueue another hash for that mesh
- **AND** the existing thumbnail continues to display

#### Scenario: Poll skips static meshes

- **WHEN** the periodic refresh poll runs
- **AND** a mesh's cached content hash is valid and `mesh->GetDirty() == false`
- **THEN** the poll does NOT enqueue a hash for that mesh
- **AND** the cached thumbnail continues to display

### Requirement: The editor SHALL build an in-memory index of disk-cached filenames on startup

On `Editor::Initialize` (or the first `Editor::Tick`), the editor SHALL perform a one-shot `std::filesystem::directory_iterator` scan of `Resource/.thumbcache/` and populate an in-memory `std::unordered_set<std::string>` of filenames present. This index is consulted on cache-lookup to avoid a `std::filesystem::exists` syscall per mesh per session.

When a thumbnail PNG is successfully written, the editor SHALL add the filename to the index. When a mesh is evicted from the in-memory FBO cache, the editor SHALL NOT delete the disk file (the disk cache may outlive the in-memory cache; the user can manually clear the folder).

#### Scenario: Startup scan populates the index

- **WHEN** the editor starts up
- **AND** `Resource/.thumbcache/` contains `furye_aaaa1111aaaa2222.png` and `furye_bbbb3333bbbb4444.png`
- **THEN** the in-memory disk-cache index contains both filenames
- **AND** subsequent cache lookups for the corresponding hashes return "exists" without a filesystem syscall

#### Scenario: Successful write adds to the index

- **WHEN** the editor writes a new `furye_cccc5555cccc6666.png` to disk
- **THEN** the filename is added to the in-memory index
- **AND** subsequent cache lookups for that hash return "exists"

#### Scenario: Eviction does NOT delete the disk file

- **WHEN** a mesh is removed from the scene and `EvictStaleMeshThumbnails` drops its in-memory FBO entry
- **THEN** the corresponding `furye_<hash>.png` file on disk is NOT deleted
- **AND** the filename remains in the in-memory disk-cache index
- **AND** if the same mesh (same content hash) is loaded again later, the disk cache hit is served

### Requirement: The thumbnail and mesh editor preview SHALL use a consistent simple Lambert look

Both the 128×128 mesh thumbnail rendered for the Content Browser and the interactive mesh editor preview SHALL use the same inline flat-shaded Lambert shader with:
- A fixed directional light direction of `(0.4, 0.8, 0.3)` (normalized).
- A half-lambert ambient floor of `0.2` (so back-faces are not pure black).
- A flat albedo of `vec3(0.7)` (mid-grey, so lit faces are clearly grey and silhouettes are readable against the black background).
- An opaque black background (`glClearColor(0.0, 0.0, 0.0, 1.0)`) matching the 3D scene viewport.

The shader SHALL be robust against missing or zero vertex normals (glTF meshes sometimes arrive without them). When the interpolated `v_normal` is degenerate (`length < 0.0001`), the shader falls back to `vec3(0, 1, 0)` so the fragment is shaded (mid-grey) instead of producing NaN/white.

The thumbnail and the mesh editor preview SHALL NOT render the mesh's first bound material. Material-faithful rendering remains the responsibility of the main 3D scene viewport.

#### Scenario: Thumbnail uses the simple Lambert shader

- **WHEN** the editor renders a thumbnail for a mesh with a textured PBR material bound
- **THEN** the thumbnail is rendered with the flat 0.7 grey albedo + fixed directional light
- **AND** the mesh's bound material is NOT sampled
- **AND** the background is opaque black (matching the 3D scene viewport)

#### Scenario: Mesh editor preview matches the thumbnail's look

- **WHEN** the user opens the mesh editor for a mesh
- **THEN** the preview pane renders the mesh with the same simple Lambert shader used for the thumbnail
- **AND** the visual look (albedo, lighting, background) is identical between the thumbnail and the editor preview

#### Scenario: Mesh with missing normals still renders

- **WHEN** the editor renders a thumbnail for a mesh whose vertices have no normals (e.g. some glTF primitives)
- **THEN** the shader falls back to a default normal vector
- **AND** the rendered fragment is mid-grey, not white
- **AND** the silhouette is visible against the black background
