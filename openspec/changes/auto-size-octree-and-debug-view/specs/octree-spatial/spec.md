## ADDED Requirements

### Requirement: OcTree::Create SHALL support default-cube construction

`OcTree::Create()` SHALL accept zero, one, or three arguments. The supported overloads are:

- `Create()` — root spans the default cube `[-OcTree::kDefaultHalfExtent, +OcTree::kDefaultHalfExtent]` on each axis (currently `1000.0f`); max-depth defaults to `8`.
- `Create(unsigned int maxDepth)` — default-cube root with explicit max depth.
- `Create(Vector4 min, Vector4 max, unsigned int maxDepth = 8)` — caller-supplied root extents; preserves the legacy three-argument behavior.

The same overload set SHALL be exposed on the Lua `OcTree.Create` binding via `sol::overload`. Existing call sites that pass `(min, max, depth)` SHALL continue to compile and run unchanged.

`OcTree::IsRooted()` SHALL be added as a public `const` query that returns `true` whenever `m_Root` is allocated. With the default-cube `Create()` and `Reset()`, this is always `true`; the predicate exists so editor and tooling code can defensively guard against future code paths that explicitly null the root.

`OcTree::Reset()` SHALL be added (zero-arg) and SHALL restore the default-cube root with `kDefaultMaxDepth`. `OcTree::Reset(unsigned int maxDepth)` SHALL similarly restore the default-cube root with the supplied max depth. The existing `Reset(min, max, maxDepth)` SHALL retain its caller-supplied-extents behavior.

#### Scenario: Zero-argument Create produces a default-cube root

- **WHEN** C++ or Lua calls `OcTree::Create()` (or `OcTree.Create()`)
- **THEN** the returned tree's `IsRooted()` is `true`
- **AND** the root AABB is `[(-1000, -1000, -1000), (1000, 1000, 1000)]`
- **AND** the tree's `m_MaxDepth` is `8`

#### Scenario: Three-argument Create preserves legacy behavior

- **WHEN** code calls `OcTree::Create(Vector4(-100), Vector4(100), 4)`
- **THEN** the returned tree's `IsRooted()` is `true`
- **AND** the root's AABB is `[(-100, -100, -100), (100, 100, 100)]`
- **AND** the tree's `m_MaxDepth` is `4`

#### Scenario: Lua exposes all three overloads

- **WHEN** a Lua script calls `OcTree.Create()`, `OcTree.Create(6)`, or `OcTree.Create(Vector4(-100), Vector4(100), 4)`
- **THEN** each call returns a usable `OcTree` usertype
- **AND** unrecognized argument shapes raise a sol2 type error rather than silently miscalling

#### Scenario: Reset restores the default-cube root

- **GIVEN** a tree whose root has been wrapped multiple times so the AABB is no longer `±1000`
- **WHEN** code calls `OcTree::Reset()`
- **THEN** the root's AABB is `[(-1000, -1000, -1000), (1000, 1000, 1000)]` again
- **AND** `m_MaxDepth` is `8`

### Requirement: OcTree SHALL grow its root on out-of-bounds inserts

When `AddSceneNode` is called and the inserted node's world AABB is not fully contained by `m_Root->GetAABB()` (`IsInside` returns anything other than `Side::IN`), the tree SHALL wrap the existing root inside a new root whose extents are exactly twice the existing root's, positioned so the existing root becomes one of the eight children of the new root in the direction *away* from the out-of-bounds node.

The wrap operation SHALL preserve all existing tree state — every previously-allocated `OcTreeNode`, every `m_SceneNodes` list, every `SceneNode::m_OcTreeNode` weak pointer remains valid. Only `m_Root` (and the old root's `m_Parent`) is updated.

The wrap SHALL repeat until either the new root contains the inserted node's AABB or 32 wraps have occurred. If the 32-wrap cap is hit, the tree SHALL log `FURYE` and proceed with the legacy fallback (insert lands at the root's `m_SceneNodes` bucket).

`m_MaxDepth` SHALL NOT be incremented during wrap.

#### Scenario: Single wrap when a node falls just outside

- **GIVEN** a tree with root AABB `[(-10, -10, -10), (10, 10, 10)]` (constructed via the explicit three-arg `Create`)
- **WHEN** code inserts a node with world AABB `[(15, 0, 0), (16, 1, 1)]`
- **THEN** after the wrap the new root's AABB is exactly 2x the volume of the old root and contains the inserted node
- **AND** the old root is now a child of the new root, accessible via the appropriate index in the new root's `m_Childs[8]` array
- **AND** the inserted node ends up inside the new tree's structure (not in any node's linear-scan bucket at the new-root level beyond the existing per-tree-node placement rules)

#### Scenario: Multiple wraps on a far-out insert

- **GIVEN** a tree with root AABB `[(-10, -10, -10), (10, 10, 10)]`
- **WHEN** code inserts a node at world position `(1000, 0, 0)`
- **THEN** the tree wraps multiple times until the new root contains `(1000, 0, 0)`
- **AND** the wrap count is bounded by `ceil(log2(1010 / 10)) ≈ 7` operations
- **AND** every previously-tracked scene node remains queryable via `WalkScene` (i.e., the wrap did not lose any state)

#### Scenario: Default-cube tree handles in-range scenes without wrapping

- **GIVEN** a tree created via the zero-argument `OcTree::Create()` (root spans `±1000`)
- **WHEN** code inserts a scene whose nodes all fit inside `±1000`
- **THEN** no wrap operations are performed
- **AND** `GetRootAABB()` still returns `[(-1000, -1000, -1000), (1000, 1000, 1000)]` after the load

#### Scenario: Wrap cap engages on pathological input

- **GIVEN** a tree created via the zero-argument `OcTree::Create()`
- **WHEN** code inserts a node positioned so far from the root that 32 wraps would not contain it (e.g., near `FLT_MAX`)
- **THEN** the tree logs an `EROR`-level message identifying the wrap-cap hit
- **AND** the tree does not crash; the inserted node falls back to the root's `m_SceneNodes` bucket
- **AND** subsequent in-bounds inserts continue to function normally

### Requirement: OcTree SHALL expose statistics for the editor

`OcTree::GetTotalSceneNodeCount() const` SHALL return the count of scene nodes tracked by the tree (root's `GetTotalSceneNodeCount()`).

`OcTree::GetOccupiedNodeCount() const` SHALL return the count of allocated `OcTreeNode` instances reachable from the root (including the root itself).

`OcTree::GetMaxOccupiedDepth() const` SHALL return the deepest depth at which any tree-node's `m_SceneNodes` is non-empty, with the root at depth `0`. Returns `0` when only the root holds scene nodes (or the tree is empty).

These accessors SHALL be `const` and safe to call from the editor's per-frame Profiler tab.

#### Scenario: Empty tree reports zero scene-node and depth

- **GIVEN** a tree just constructed via `OcTree::Create()` with no scene nodes inserted
- **WHEN** code calls `GetTotalSceneNodeCount()` and `GetMaxOccupiedDepth()`
- **THEN** both return `0`
- **AND** `GetOccupiedNodeCount()` returns `1` (just the root)

#### Scenario: Single-node tree reports the expected counts

- **GIVEN** a tree created via `OcTree::Create()` (default-cube) with one inserted scene node whose world AABB sits inside the root
- **WHEN** code calls `GetTotalSceneNodeCount()`
- **THEN** the result is `1`
- **AND** `GetOccupiedNodeCount() >= 1` (root plus any subdivisions traversed by the insert)
- **AND** `GetMaxOccupiedDepth()` returns the depth at which the node landed (≥ 0)

### Requirement: Pipeline SHALL expose an OCTREE_BOUNDS debug switch

`PipelineSwitch` SHALL gain a new value `OCTREE_BOUNDS` before `LENGTH`. When `IsSwitchOn(PipelineSwitch::OCTREE_BOUNDS)` is `true` AND `Scene::Active->GetSceneManager()` is an `OcTree`, `Pipeline::DrawDebug` SHALL render one wireframe AABB per occupied tree-node in the active octree, with color picked from a depth-indexed palette.

`PrelightPipeline.cpp`'s `DrawDebug` gate (the `IsSwitchOn({...}, true)` call at line 159–161) SHALL include `PipelineSwitch::OCTREE_BOUNDS` so that toggling the switch on its own enables the debug draw.

The OcTree-bounds overlay SHALL skip tree-nodes whose `m_TotalSceneNodeCount == 0` to avoid cluttering the view with empty subtrees.

#### Scenario: Toggle on draws the octree wireframe

- **GIVEN** the demo running with the bundled scene loaded
- **WHEN** code calls `Pipeline::Active->SetSwitch(PipelineSwitch::OCTREE_BOUNDS, true)` and the next frame renders
- **THEN** wireframe boxes appear corresponding to the occupied tree-nodes of the active octree
- **AND** boxes at deeper levels are colored differently from the root's box

#### Scenario: Toggle off removes the overlay

- **GIVEN** the OcTree-bounds overlay is enabled and visible
- **WHEN** code calls `Pipeline::Active->SetSwitch(PipelineSwitch::OCTREE_BOUNDS, false)` and the next frame renders
- **THEN** the wireframe boxes are gone
- **AND** the rest of the rendered scene is unchanged

#### Scenario: Empty subtrees are not drawn

- **GIVEN** the overlay is enabled on a tree with several allocated `OcTreeNode`s, half of which have `m_TotalSceneNodeCount == 0`
- **WHEN** the frame renders
- **THEN** only nodes with `m_TotalSceneNodeCount > 0` produce a wireframe box

### Requirement: Editor Profiler FPS tab SHALL include a Spatial readout

The Profiler window's FPS tab (`engine/Fury/Editor/EditorWindows.cpp::RenderProfilerFpsTab`) SHALL display, after the existing "Use Cascaded Shadow Map" checkbox, a section labeled "OcTree:" that surfaces the active scene's octree state.

When `Scene::Active->GetSceneManager()` is an `OcTree`, the section SHALL display:
- The current root AABB's `min` and `max`.
- The total tracked scene-node count (`GetTotalSceneNodeCount()`).
- The total occupied tree-node count (`GetOccupiedNodeCount()`).
- The maximum occupied depth (`GetMaxOccupiedDepth()`).
- An `ImGui::Checkbox("Draw OcTree Bounds", ...)` whose state is mirrored to `Pipeline::Active->SetSwitch(PipelineSwitch::OCTREE_BOUNDS, ...)` each frame.

When `Scene::Active->GetSceneManager()` is not an `OcTree`, the section SHALL display "(non-octree scene manager)" and SHALL NOT show the toggle.

The section MUST NOT crash when `Scene::Active`, `Pipeline::Active`, or the scene manager is `nullptr`; in those cases the section SHALL be omitted (or render `(no active scene)` text).

#### Scenario: Section appears once per frame in the FPS tab

- **GIVEN** the editor is running with the demo scene loaded and the Profiler window is open on the FPS tab
- **WHEN** the Profiler renders for a frame
- **THEN** a Separator and "OcTree:" header appear after the "Use Cascaded Shadow Map" checkbox
- **AND** numeric readouts of root extents, total scene-node count, occupied tree-node count, and max occupied depth are visible

#### Scenario: Toggle drives the pipeline switch

- **GIVEN** the Spatial section is rendered and the "Draw OcTree Bounds" checkbox is unchecked
- **WHEN** the user clicks the checkbox
- **THEN** `Pipeline::Active->IsSwitchOn(PipelineSwitch::OCTREE_BOUNDS)` returns `true` afterwards
- **AND** the next-frame render shows the wireframe overlay
