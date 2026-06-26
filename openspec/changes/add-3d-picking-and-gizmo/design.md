## Context

`add-node-properties-panel-and-save` introduces the Node Properties window (right-docked), the `Editor::SceneIO::on_save` flow, and the keyboard shortcuts. Selection state lives in `g_SelectedSceneNode` (raw `SceneNode*`); the panel renders editable widgets but transforms still need to be edited as numbers.

**Engine rendering shape (current):**

- Frame loop in `Engine::Tick`: `RenderUtil::BeginFrame` → SFML event poll → fixed updates → `Gui::NewFrame(dt)` → `Editor::Tick` (menu, dockspace, windows) → user `on_update` → (Lua) `Pipeline::Active->Execute(octree)` → `Gui::Render`.
- `PrelightPipeline::Execute(sceneManager)` walks the OcTree, fills a `RenderQuery` with `RenderUnit{node, mesh, material, subMesh}` records, draws G-buffer / lighting / forward passes, then `Gui::Render` composites ImGui on top.
- `Pipeline::Active->GetCurrentCamera()` returns the camera SceneNode. Camera matrices come from `node->GetWorldMatrix()` and `camera->GetProjectionMatrix()` (`Matrix4` storing `float Raw[16]` in OpenGL column-major layout — same convention ImGuizmo expects).
- Mesh draws use `shader->BindMatrix(Matrix4::WORLD_MATRIX, node->GetWorldMatrix())` then `shader->BindMesh(mesh)`. The picking pass needs to do the same — bind a node-id uniform per draw, render to an R32UI target.

**SceneNode surface used by gizmo write-back:**

- `SceneNode::SetLocalPosition(Vector4)`, `SetLocalRoattion(Quaternion)`, `SetLocalScale(Vector4)`, `Recompose(bool force)`.
- `node->GetWorldMatrix()` is the source-of-truth Matrix4 for ImGuizmo.

**Selection lifecycle:**

`g_SelectedSceneNode` is a raw pointer. The Node Properties panel walks the scene tree per frame to confirm reachability (specced in `add-node-properties-panel-and-save`). Picking writes the same pointer; the dangling-pointer walk handles cleanup if the picked node is later destroyed.

**Why the offscreen-ID approach:**

Compared with CPU ray-AABB:
- AABB picking can't pick into concave geometry (a chair seen through its own legs).
- AABB picking returns the bounding-box hit, not the visually-frontmost mesh.
- Ray-AABB needs a Frustum/Vector4 ray-builder we'd have to add and unit-test.

Compared with ID pass every frame:
- Editor scenes are small but `glReadPixels(R32UI)` from the GPU stalls the pipeline ~0.1–0.5ms. Doing it on every frame wastes that latency for no benefit.
- Doing it only on click frames means steady-state editor cost is zero new GPU/CPU work.

## Goals / Non-Goals

**Goals:**
- Click on a 3D node in the viewport → that node becomes the current selection (Scene Inspector + Node Properties + gizmo all show it).
- A draggable TRS gizmo appears at the selected node's world transform; dragging updates the live scene's local transform.
- Gizmo mode (translate / rotate / scale), space (local / world), and snap settings are user-controlled from the Node Properties panel.
- No measurable steady-state cost — picking pass and readback run only on click.
- Engine public headers and non-editor builds stay identical.

**Non-Goals:**
- Marquee / box selection.
- Multi-node selection or multi-node gizmo manipulation.
- Hover highlight (outline shader on the picked node) — out of v1.
- Undo / redo for gizmo edits.
- Picking transparent / non-opaque renderables (we only enumerate `RenderQuery::renderableNodes`).
- Snapping for non-uniform scale axes individually (snap step is uniform across X/Y/Z).
- Camera or light icon "billboards" in the viewport (would need an icon-render pass).
- Touch / pen / multi-pointer input (mouse only).

## Decisions

### Decision 1: Vendor ImGuizmo as `engine/ThirdParty/ImGuizmo/ImGuizmo.{cpp,h}` only

Copy `ImGuizmo.cpp` and `ImGuizmo.h` from `/Users/sindney/Documents/git/furyengine/ImGuizmo/src/` into `engine/ThirdParty/ImGuizmo/`. Skip `GraphEditor*`, `ImCurveEdit*`, `ImGradient*`, `ImSequencer*`, `ImVectorEditor*`, `ImLightRig.h`, `ImZoomSlider.h` — they're optional widgets unrelated to manipulation. Add a top-of-file comment in each vendored file recording the upstream commit hash + copy date.

CMake: extend the existing `if(WITH_EDITOR)` block to:
- add `engine/ThirdParty/ImGuizmo/ImGuizmo.cpp` to the editor source list,
- add `engine/ThirdParty/ImGuizmo` to the editor target's include directories.

ImGuizmo includes `imgui.h`/`imgui_internal.h` directly. We'll either:
1. Add a small shim `engine/ThirdParty/ImGuizmo/imgui.h` that redirects `#include "imgui.h"` → `#include "ImGui/imgui.h"`, OR
2. Patch the vendored `ImGuizmo.cpp/.h` includes to use `"ImGui/imgui.h"`.

Pick (2) — cleaner, no path-shadowing surprises. Document the patch in the top-of-file comment.

**Why:** ImGuizmo isn't header-only and has a meaningful TU. Vendoring just the manipulator keeps editor build time bounded; pulling in the timeline / curve-editor widgets would add ~5000 LOC we don't use.

**Alternative considered:** git submodule — rejected for the same reason as ImReflect (fragile across fresh clones).

### Decision 2: Picking-pass shader is a tiny custom Pass shader in `engine/Fury/Editor/EditorPicking.cpp`

Define a `id_pass.vs` / `id_pass.fs` pair as inline string literals (mirroring the existing `EditorBlitCubeShader` pattern in `EditorWindows.cpp`):

```glsl
// id_pass.vs
in vec3 vertex_position;
uniform mat4 view_matrix;
uniform mat4 projection_matrix;
uniform mat4 world_matrix;
void main() {
    gl_Position = projection_matrix * view_matrix * world_matrix * vec4(vertex_position, 1.0);
}
```

```glsl
// id_pass.fs
uniform uint node_id;
out uint fragment_output;
void main() { fragment_output = node_id; }
```

The fragment shader writes a single uint per pixel to the bound R32UI color attachment. Skinned meshes use a slightly different vs that also reads `vertex_bone_id` / `vertex_bone_weight` and a uniform bone-matrix array — we'll reuse `Shader::ShaderType::SKINNED_MESH` precedent and mirror its bind path.

The pass renders front-to-back (depth test enabled) so the read pixel is always the visually-frontmost node ID.

**Why:** Avoids touching `PrelightPipeline` or adding a new `Pass` JSON. The whole pass lives in editor TUs and is invisible to non-editor builds.

**Alternative considered:** Add an "id_pass" entry to the existing pipeline JSON — rejected because that surfaces editor-only state in shipped artifacts and complicates `WITH_EDITOR=OFF`.

### Decision 3: Picking lifecycle is "request → render once → read back next frame → resolve"

Three states tracked in `EditorPicking.cpp`:

```cpp
enum class PickState { Idle, RenderRequested, AwaitingReadback };

PickState g_State = PickState::Idle;
ImVec2    g_PendingClickPx;     // viewport-relative cursor
std::vector<std::weak_ptr<SceneNode>> g_IdTable;
```

On viewport click (left mouse button down, ImGui doesn't want the mouse, gizmo not hovered):
1. Record `g_PendingClickPx`. Set `g_State = RenderRequested`.

In `Editor::Tick` when `g_State == RenderRequested`:
1. Rebuild `g_IdTable` from `Scene::Active->GetSceneManager()->GetRenderableNodes()` (or the equivalent traversal).
2. Bind picking FBO. Resize/recreate if drawable size changed since last pick.
3. Clear color to 0, clear depth.
4. For each renderable node, `BindUInt("node_id", index+1)`, `BindMatrix(world_matrix, ...)`, draw mesh. Reuse `RenderUtil::Instance()` patterns.
5. Issue an async `glReadPixels` into a 1×1 staging buffer at `g_PendingClickPx`. Set `g_State = AwaitingReadback`.

In `Editor::Tick` when `g_State == AwaitingReadback`:
1. Read the staging buffer (pipeline has flushed by now since we draw it last frame and the engine's main pipeline ran in between).
2. If `id == 0` → `g_SelectedSceneNode = nullptr`. Else if `id-1 < g_IdTable.size()` and `weak_ptr.lock() != nullptr` → `g_SelectedSceneNode = locked.get()`.
3. Set `g_State = Idle`.

The `RenderRequested` → `AwaitingReadback` split intentionally spans two frames so the readback never blocks the GPU on the same frame we issue the draws — `glReadPixels` from a recently-rendered RT typically returns immediately on the next frame because the driver has already flushed the queue.

**Why:** Click latency stays below one frame as perceived. No PBO required (a 1-pixel uint readback is small enough to skip async PBO complexity for v1).

**Alternative considered:** Single-frame synchronous readback (issue draws + `glReadPixels` same frame) — works but causes a guaranteed pipeline stall. Two-frame approach removes the stall for free.

### Decision 4: Picking-pass FBO is owned by the editor, recreated on resize, sized to drawable area

Inside `EditorPicking.cpp`:

```cpp
GLuint g_PickFBO = 0;
std::shared_ptr<Texture> g_PickColor;   // R32UI
std::shared_ptr<Texture> g_PickDepth;   // DEPTH24
int g_PickW = 0, g_PickH = 0;
```

Each pick frame, query the engine's drawable size (`Gui::GetDrawableSize()` if exposed; otherwise SFML window size — we'll add a minimal accessor `Editor::GetDrawableSize()` for this). Compare with `g_PickW/g_PickH`; if changed, destroy and recreate the textures + FBO. Use `Texture::CreateEmpty(w, h, 0, TextureFormat::R32UI, TextureType::TEXTURE_2D)` — except `R32UI` doesn't currently exist in `TextureFormat` enum. Add `R32UI` to `engine/Fury/EnumUtil.h`'s `TextureFormat` and the corresponding GL `internalFormat=GL_R32UI`/`format=GL_RED_INTEGER`/`type=GL_UNSIGNED_INT` mapping in the texture creation switch.

Adding `R32UI` to the enum is a strictly additive change — no existing enumerator value shifts (it goes at the end before `SRGB` block, same position pattern as `R32F`). This does NOT break the public API.

**Why:** R32UI is the correct format for a non-aliased 32-bit-integer ID channel. Float formats lose precision past 2^24; using R32F would cap us at ~16M nodes, but more importantly would force a uintBitsToFloat dance.

**Risk:** The `TextureFormat` enum is part of `engine/Fury/EnumUtil.h` — engine public header. Adding a value is acceptable (additive); contributors who exhaustively `switch` on it elsewhere need a new arm. → Mitigation: grep the codebase for `case TextureFormat::` and add the missing arms (probably 1–2 sites in `Texture.cpp`'s GL-format mapping).

**Alternative considered:** Encode IDs into RGBA8 (4×8-bit channels) — sidesteps the enum change but requires bit-fiddling on read and limits us to 2^32 ids in a non-obvious way. Rejected: explicit R32UI is clearer.

### Decision 5: The viewport's clickable region is the central dock node, not the whole window

ImGui's `DockSpaceOverViewport` with `PassthruCentralNode` produces a transparent central area where the 3D scene shows through. We need the click + gizmo to interact only with that region (not over docked panels).

`ImGui::DockBuilderGetCentralNode(s_DockspaceID)->Pos / Size` gives us the central rect each frame. Picking and gizmo use that rect:
- Click is "in viewport" iff `cursor` is inside the central rect AND `!ImGui::GetIO().WantCaptureMouse` AND no gizmo is being used.
- Gizmo's `ImGuizmo::SetRect(centralRect)` clamps gizmo interactivity to the same region.
- Picking clicks fired through the central rect compute cursor coords as `cursor.x - rect.x, cursor.y - rect.y` for the viewport-relative pixel coordinate (Y-flip handled by GL convention: `pickY = pickH - 1 - rectRelativeY`).

**Why:** Clicking on a docked panel must drive ImGui as today (no accidental selection through a Settings checkbox).

### Decision 6: Gizmo state lives in editor C++; Lua bindings are optional

```cpp
// In Editor.cpp
enum class GizmoMode { Translate, Rotate, Scale };
enum class GizmoSpace { Local, World };
GizmoMode  g_GizmoMode  = GizmoMode::Translate;
GizmoSpace g_GizmoSpace = GizmoSpace::World;
bool       g_SnapEnabled = false;
float      g_SnapTranslate = 1.0f;
float      g_SnapRotate    = 15.0f;   // degrees
float      g_SnapScale     = 0.1f;
```

`Editor::SetGizmoMode(const char*)`, `Editor::SetGizmoSpace(const char*)`, `Editor::SetSnapEnabled(bool)` are exposed. The Lua bindings (`Editor.SetGizmoMode("rotate")`, etc.) are added but no script changes are required — Editor.lua continues to work unchanged.

State persists via the same `imgui.ini` settings handler that already persists the theme: extend the FuryEditor section to include `Gizmo=mode,space,snap_enabled,snap_t,snap_r,snap_s`.

**Why:** Symmetric with the existing theme persistence. Avoids a JSON config sidecar.

### Decision 7: Gizmo writes to local transform after world-space manipulation

ImGuizmo accepts a 4x4 matrix and modifies it in place. We give it the node's world matrix; on commit:

1. Read back the modified world matrix `Mw`.
2. Compute parent's world matrix `Mp = parent->GetWorldMatrix()` (identity if no parent).
3. New local matrix `Ml = inverse(Mp) * Mw`.
4. Decompose `Ml` into translation / rotation / scale via `MathUtil` helpers (we'll add a `Matrix4::Decompose(out_pos, out_rot, out_scale)` if it doesn't already exist — quick check during implementation).
5. Call `node->SetLocalPosition(t)`, `SetLocalRoattion(r)`, `SetLocalScale(s)`, `Recompose(false)`.

Only the components that actually changed are written: ImGuizmo provides `delta_matrix` separately so we can detect "translate-only" / "rotate-only" / "scale-only" and skip no-op writes. This avoids re-quantizing rotation when the user is only translating.

**Why:** Keeps the gizmo's UX (drag along world XYZ when in World space, drag along node-local axes when in Local space) while keeping the data model (local transforms) intact. Matches Unity / Godot.

**Risk:** Non-uniform scale on a parent can produce shear in the decomposed local matrix. → Mitigation: documented limitation. The same risk exists for any transform editor; users avoid non-uniform scale on parents by convention.

### Decision 8: Gizmo and picking integrate at the end of `Editor::Tick`, after the pipeline draws

Today's order: `Gui::NewFrame` → `Editor::Tick` (menu + dockspace + windows) → user `on_update` → `Pipeline::Execute` (3D scene → framebuffer) → `Gui::Render` (ImGui draws).

ImGuizmo wants to draw via ImGui draw lists, so it must run BEFORE `Gui::Render`. The picking pass writes to its own FBO and reads back, so its placement is independent of the main pipeline.

New order:
1. `Gui::NewFrame`.
2. `Editor::Tick` (early): menu bar, dockspace, modals, windows. Compute central-node rect. Read mouse-click intent. — same as today.
3. `Editor::Tick` (late, just before user `on_update` returns control via `Pipeline::Execute`): Render gizmo into ImGui draw lists for the active camera + selected node.
4. User `on_update` runs (calls `Pipeline::Execute`).
5. **NEW**: `Editor::TickPostRender()` — invoked from `Engine.cpp` between `Pipeline::Execute` and `Gui::Render`. Runs the picking pass when state is `RenderRequested` and consumes readback when `AwaitingReadback`. Picking renders AFTER the scene render so the depth/state is fresh — except picking has its own depth buffer, so this is for code-locality, not GL-state.

This requires one new entry point: `Editor::TickPostRender()` callable from `engine/Fury/Engine.cpp`. The existing `Editor::Tick` is unchanged in name; `TickPostRender` is the additional hook. With `WITH_EDITOR=OFF`, `TickPostRender` is an inline no-op (same pattern as `Editor::Tick`).

**Why:** Splits the editor's frame contribution into "before user pipeline" (UI) and "after user pipeline" (picking + any future post-effects). Keeps the engine's main loop as the orchestrator.

**Alternative considered:** Run picking from within `Editor::Tick` and rely on GL state being "good enough" — rejected because the user's pipeline may invalidate the FBO binding state we set up.

### Decision 9: Picking ID table is rebuilt on every pick frame, not cached

The cost of `weak_ptr<SceneNode>` ownership means storing strong refs would interact badly with scene mutations. We rebuild the `g_IdTable` each pick frame from `RenderQuery::renderableNodes` (or equivalent). Cost: O(N) push_backs into a vector; on the tank scene N≈30, well below a microsecond.

**Why:** Eliminates an entire class of bugs (table out of sync with current scene). Cheap.

**Alternative considered:** Persistent `g_IdTable` updated by scene-mutation signals — rejected as overkill for the click frequency we expect.

### Decision 10: Camera node ignored by picking pass; debug helpers (axis lines, frustums) ignored

`Pipeline::DrawDebug` already excludes itself from the renderable query. The picking pass walks `RenderQuery::renderableNodes` only, which by construction excludes the camera node and debug visualizers. Lights are already excluded too (they're in a separate `lightNodes` collection). This is the right scope for v1.

**Trade-off:** Users can't pick a light by clicking on its world-space position. Acceptable — the Scene Inspector tree handles light selection, and v2 could add billboard icons rendered into the picking pass.

## Risks / Trade-offs

- **[Risk]** ImGuizmo's matrix layout is row-major float[16] in C++ but column-major when uploaded as a GL uniform — we have to be deliberate about the memory layout we hand it. Our `Matrix4::Raw[16]` is GL column-major (per the comment in `Matrix4.h`). → Mitigation: pass `Raw` directly; ImGuizmo's `EditTransform` example uses the same layout. Add a one-line static_assert in `EditorGizmo.cpp` documenting the assumption.
- **[Risk]** Adding `TextureFormat::R32UI` is a public-header change. Some downstream code may have an exhaustive `switch` on `TextureFormat`. → Mitigation: grep before merge; add the missing case arm. The compile error is loud and obvious.
- **[Risk]** `glReadPixels` on R32UI on macOS / OpenGL 3.3 has historically had driver bugs on Intel iGPUs. → Mitigation: test path on the developer's macOS Metal-on-OpenGL stack; if a regression emerges, fall back to RGBA8 ID encoding (4×8-bit packed). Documented as a known issue, not blocked-on.
- **[Risk]** Click on a deep-stacked node (transparent in front of opaque) — picking returns the front-most opaque, but the user might've meant the transparent. → Acceptable: editor is for opaque-mesh content; transparent-mesh pickability is a separable feature.
- **[Risk]** A very fast user click → release in < 1 frame may not register if we trigger only on `IsMouseClicked` without holding state across frames. → Mitigation: trigger on click DOWN edge (not click-and-released), and our 2-frame state machine survives release before completion.
- **[Risk]** Skinned-mesh picking. The id_pass shader has a static-mesh path; skinned meshes need a bone-matrix path. → Mitigation: add a parallel `id_pass_skinned.vs` mirroring the existing skinned forward shader. Cost: ~30 LOC. Tracked as a v1 task — not deferred.
- **[Trade-off]** Rebuilding the ID table every pick is O(N). For 10K-node scenes this becomes ~10µs — still fine, but if the pattern grows, we'd cache.
- **[Trade-off]** We don't render an outline / highlight on the picked node in v1. Visual feedback comes from the gizmo appearing on the selected node and the Inspector / Properties windows reflecting the change. Outline would require an additional pass.

## Migration Plan

1. Land `add-node-properties-panel-and-save` first (it provides the panel that hosts gizmo controls).
2. Vendor ImGuizmo (two file copy + import-path patch).
3. Add `TextureFormat::R32UI` enum value + GL mapping. Verify all `case TextureFormat::*` switches compile.
4. Add `EditorPicking.{hpp,cpp}` with the FBO + ID pass + readback state machine. Wire `Editor::TickPostRender` into `engine/Fury/Engine.cpp`'s frame loop. Verify a 1-frame-delayed click reads back the correct ID on a known scene.
5. Add `EditorGizmo.cpp`. Wire the gizmo render into the late phase of `Editor::Tick` (before user `on_update` runs the pipeline). Verify drag updates `node->GetWorldMatrix()` next frame.
6. Add gizmo-mode / snap UI to `EditorNodeProperties.cpp`. Verify the radio buttons + checkbox round-trip through the C++ state.
7. Add Lua bindings (`Editor.SetGizmoMode`, etc.) — no Editor.lua change required.
8. Manual test the full path on `Resource/Scene/scene.bin`: click tank → tank selected → drag translate gizmo → tank moves → Cmd+S → saved scene reflects new position.
9. Rollback: revert to before this change. Engine headers are untouched apart from one additive enum value, which can be left in place harmlessly or reverted alongside.

## Open Questions

- Should the picking pass also colorize sub-meshes uniquely (sub-mesh-level picking)? **Decision: no.** Selection granularity is SceneNode in v1; matches the Scene Inspector tree.
- Should gizmo snapping defaults persist via Editor.lua or via `imgui.ini`? **Decision: `imgui.ini`** (Decision 6). Project config can override at startup via `Editor.SetSnap*`.
- Do we add an outline for the picked node? **Decision: not in v1.** The gizmo on the node is sufficient feedback.
