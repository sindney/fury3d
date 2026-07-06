## Context

Today `Mesh` is a single-resolution asset: one vertex buffer + one set of submeshes, no concept of "this mesh also has lower-detail versions". `MeshRender` binds exactly one `Mesh` and the renderer draws that one mesh every frame (`Shader::BindMesh` / `BindSubMesh` is called per-frame from the per-instance draw path). `GltfImporter` translates one glTF `Mesh` into one engine `Mesh`; if the glTF uses `MSFT_lod` to chain a list of resolution variants under a single node, the importer currently rejects the file because `extensionsRequired` is non-empty.

The editor's mesh window (`EditorAssetWindows.cpp::RenderMeshEditorWindow`) shows the metadata for a single `Mesh` and the mesh window's 3D preview pane currently renders nothing (it is a `PLACEHOLDER` that draws a gray rect). The scene-inspector MeshRender body (`EditorNodeProperties.cpp::RenderMeshRenderBody`) shows the bound mesh name + per-slot materials but has no LOD concept.

The glTF sample-assets repo at `/Users/sindney/Documents/git/furyengine/glTF-Sample-Assets/` provides ~159 model folders. None of them currently uses the `MSFT_lod` extension (verified by grep), so the importer will need a name-prefix heuristic (e.g. `Box_LOD0`, `Box_LOD1`, `Box_LOD2`) to demonstrate the feature against the available test data.

## Goals / Non-Goals

**Goals:**

- Define a first-class `LodGroup` that owns an ordered list of `Mesh::Ptr`s + per-LOD transition thresholds, serializable through `Mesh::Save`/`Load` via a new `lod_group` key.
- Teach `MeshRender` to bind a `LodGroup` and to pick the active LOD per-frame from the camera's screen-space coverage of the model's AABB.
- Teach the glTF importer to build `LodGroup`s, both from the `MSFT_lod` extension (spec'd path) and from a name-prefix fallback (e.g. `Foo_LOD0/1/2`).
- Add a dropdown to the editor's mesh window so the user can preview each LOD; add an active-LOD readout + threshold list to the MeshRender body.

**Non-Goals:**

- True cross-fade / dithered LOD transitions (we pick one LOD per frame and snap).
- GPU-side mesh decimation (a higher LOD is one of the pre-baked glTF `Mesh` entries, not a runtime-generated proxy).
- A round-trippable authoring workflow for hand-tweaking LOD thresholds in the editor (we read thresholds from glTF and expose them read-only in v1).
- An auto-decimation tool (out of scope; users are expected to author LODs in DCC tools and re-export).

## Decisions

### `LodGroup` is owned by `MeshRender`, not by `Mesh`

- **Why**: a `Mesh` is a shared asset (multiple tanks can reference the same `Tank_LOD0`); the LOD chain is per-instance authoring — the same `Tank_LOD0` might be combined with a hand-edited `Tank_LOD1` for one scene and the DCC-exported one for another. Storing the chain on `Mesh` would either force a fork of the mesh asset per scene or smuggle authoring data into the asset. The user's request says "engine's meshrender doesn't have LODs" — making the LOD a `MeshRender` concept matches the existing `materials[]` precedent.
- **Alternative considered**: store `LodGroup` on `Mesh` and reference it from `MeshRender`. Rejected: makes the asset file carry scene-specific data; the mesh file format is intended to be DCC-tool-compatible.

### LOD selection uses screen-space AABB coverage

- **Why**: the glTF `MSFT_lod` spec keys thresholds on "screen coverage" — the fraction of the viewport the model occupies. The engine has the camera projection + the mesh's world-space AABB on hand; computing the AABB's projected radius each frame is O(1) and avoids a per-instance distance-from-camera arithmetic. A pre-allocated `m_ActiveLod` is updated at the top of the per-frame draw loop.
- **Alternative considered**: distance-from-camera with per-LOD world-distance thresholds. Rejected: doesn't match the spec'd `MSFT_lod` semantics and would be a second code path to maintain for users who mix imported and hand-authored LODs.

### Thresholds default to `screen-relative transition height` (glTF `MSFT_lod`)

- **Why**: matches the spec exactly. Each LOD entry is `(mesh, screen_coverage_threshold)`. The active LOD is the highest index `i` whose threshold is `>= current_screen_coverage`. If the highest-detail LOD's threshold is 1.0, the lowest's is 0.0, and we snap to the deepest LOD when the model is sub-pixel.
- **Alternative considered**: store LODs as `min_distance, max_distance` pairs in world units. Rejected: not what the spec says, not what the importer source data says, and requires a unit-conversion story for imported assets.

### Name-prefix heuristic as a fallback for the sample assets

- **Why**: the glTF sample-assets repo (the one the user cloned for testing) has no MSFT_lod examples, so the spec'd path produces no demoable result. To validate the feature end-to-end, the importer also accepts a name pattern `(<base>_LOD<n>|LOD<n>_<base>|<base>.LOD<n>)` per `tinygltf::Mesh` and groups the matches into one `LodGroup` ordered by `n`. Thresholds are synthesized as `1.0, 0.66, 0.33, ...` to roughly mimic the spec's `screen_coverage` ladder.
- **Trade-off**: this is heuristic and may mis-group. We mitigate by (a) requiring at least 2 matches with the same base name before forming a group (a single `Foo_LOD0` mesh still loads as a plain `Mesh`); (b) one-shot `FURYW` per grouping; (c) the threshold ladder is a default, and the user can override the `LodGroup` thresholds post-import by editing the scene file.

### `LodGroup` persists as a `Mesh` JSON sub-key, not a first-class entity in the scene file

- **Why**: `MeshRender` already round-trips via `mesh` (a name) + `materials[]`. Adding `lod_group` as a parallel name reference would double the registered-entity count for no real win. Instead, `LodGroup` is a value type serialized as a sub-object on the `MeshRender` JSON record: `{"lod_group": {"thresholds": [...], "meshes": ["LOD0_name", "LOD1_name", ...]}}`. The entity manager registers each individual LOD `Mesh` and the `LodGroup` lives as a transient value on the `MeshRender`.
- **Alternative considered**: register `LodGroup` as a first-class `Entity`. Rejected: a `LodGroup` is 1:1 with its owning `MeshRender`; making it addressable independently would invite shared mutations ("which LOD is the active one for *this* MeshRender?") for no authoring benefit.

### The LOD dropdown in the mesh window is a preview-only override, not a saved state

- **Why**: the runtime LOD is camera-driven; the dropdown lets the artist inspect each LOD without having to dolly the camera into the model's bounding sphere. The dropdown state is held on the mesh window's `RenderMeshEditorWindow` (per-window, not on the `Mesh`), so it is naturally discarded on close.
- **Alternative considered**: persist the dropdown selection onto the `Mesh` itself. Rejected: would conflate preview state with authored data, and the runtime LOD is *always* the screen-coverage-driven one.

## Risks / Trade-offs

- **[Risk] `MSFT_lod` is an extension the importer previously rejected.** → We expand the importer's `HasUnsupportedFeatures` whitelist to permit `MSFT_lod`; any *other* extension in `extensionsRequired` still rejects. Document the change in the importer's class comment.
- **[Risk] Name-prefix heuristic mis-groups meshes that happen to share a `_LOD<n>` suffix for unrelated reasons.** → Require ≥2 matches with the same base name to form a group. A single match (e.g. `Foo_LOD0` with no `Foo_LOD1`) is treated as a plain `Mesh` (no `LodGroup`) and logs `FURYD` so it's visible in the import log. Users who hit a false positive can rename the mesh in the glTF source and re-import.
- **[Risk] Active-LOD selection uses the model's *untransformed* AABB, which over-estimates screen coverage for scaled nodes.** → Acceptable in v1; the worst case is that we render a slightly higher LOD than needed. A future change can fold in the SceneNode's world scale.
- **[Risk] Adding a `lod_group` key to `MeshRender::Save` could break older loaders if a future version removes the key.** → Save is forward-compatible (additive keys are read-if-present). Older builds that don't know `lod_group` will simply ignore it. New builds must read `lod_group` as optional — no parse error when absent.
- **[Risk] Per-frame screen-coverage computation adds a per-MeshRender math op even when the model is far off-screen.** → Cheap (mat4 * vec4 + a length); frustum-culled renderables don't reach the draw path so the cost only applies to visible MeshRenders. Acceptable.

## Migration Plan

- No on-disk migration: pre-LOD scene files load unchanged (`lod_group` is read as optional). The engine binary gains the new code path; scene files written by the new engine are forward-compatible (older engines ignore the new keys).
- Backwards-compat: a `MeshRender` with no `lod_group` keeps its current behavior — bind a single `Mesh`, draw it every frame.
- Rollback: revert the new `MeshRender::SetLodGroup` / `MeshRender::GetActiveLod` and the importer changes; no scene file state is lost.

## Open Questions

- Should the LOD transition cross-fade (alpha-blend the two LODs for a frame) be supported in v1? — _Deferred. v1 snaps. v1.1 may add a small lerp window if the shader supports alpha-blended submeshes._
- Should `LodGroup` thresholds be editable in the editor's MeshRender body, or read-only? — _v1 ships read-only (matches "thresholds come from glTF" intent). Editable sliders are a v1.1 candidate._
- Does the per-frame screen-coverage check need to be skipped for static-batched renderables? — _No: LOD selection is per-instance, batched or not; the check is O(1) so it's not the bottleneck._
