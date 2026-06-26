## Context

`engine/Fury/OcTree.{h,cpp}` and `OcTreeNode.{h,cpp}` currently model a fixed-extent octree. `OcTree::Create(min, max, maxDepth = 6)` immediately allocates the root in the constructor (`OcTree.cpp:25`); there is no "empty" state. Every call site supplies `Vector4(-1000)` / `Vector4(1000)` and `maxDepth = 2`:

- `engine/Fury/Cli.cpp:422`
- `engine/Fury/Scene.cpp:37`
- `engine/Fury/GltfImporter.cpp:1090`
- `examples/Editor.lua:174`

`OcTreeNode::AddSceneNode` holds a `std::vector<std::shared_ptr<SceneNode>>` of nodes that didn't subdivide cleanly into a child (`OcTreeNode.cpp:158`). `OcTree::AddSceneNode` walks down via `IsTwiceSize` + `GetFitNode` (`OcTree.cpp:208-222`); when the inserted node's AABB is *larger* than the current tree-node's extents, or straddles the split planes, it stays at this level. Crucially, `GetFitNode` (`OcTreeNode.cpp:50-63`) returns `shared_from_this()` when the node falls outside the parent's AABB, so out-of-bounds scene nodes silently end up in the *root* node's bucket — no spatial culling, but no crash either. That's why "everything still works" with `±1000`: anything past that just becomes a slow-path linear scan inside `WalkScene`.

`maxDepth = 2` at every call site is also undersized. With a `±1000` root, depth 2 means each leaf cube is 500³ — far too coarse for a tank scene. The current code masks this because most nodes don't subdivide cleanly anyway.

The Profiler is in `engine/Fury/Editor/EditorWindows.cpp::RenderProfilerFpsTab` (line 121). Existing pattern: a row of `ImGui::Checkbox`es feeding `Pipeline::Active->SetSwitch(...)` (line 157-167). The CSM toggle uses the same surface. `Pipeline::DrawDebug` (`Pipeline.cpp:693`) is gated by `PrelightPipeline.cpp:159-161` — any of `CUSTOM_BOUNDS / LIGHT_BOUNDS / MESH_BOUNDS` enables it. The OcTree-bounds overlay slots into the same gate.

Constraints:
- Backwards-compat: existing C++ and Lua callers that pass `(min, max, depth)` must keep working unchanged.
- The Lua binding lives in `engine/Fury/LuaBindings.cpp:166-169` and currently registers `Create` as `&OcTree::Create` directly. sol2 supports overloading via `sol::overload`; this is how we'll expose the zero-arg / one-arg / three-arg shapes.
- `Scene::Active` may not be set at render time of the Profiler tab; the editor already null-checks `Pipeline::Active` for the same reason. The Spatial section must follow the same defensive pattern.
- The `OCTREE_BOUNDS` overlay must work even when `m_DebugBoxBounds` (the `CUSTOM_BOUNDS` source) is empty — it pulls geometry from the live tree, not the debug-bounds list.

## Goals / Non-Goals

**Goals:**
- `OcTree::Create()` with no arguments produces a usable tree that auto-rooms itself on first insert.
- Out-of-bounds scene nodes never end up in the root's linear-scan bucket as long as a finite number of root-wraps gets the tree to contain them.
- The existing call sites become trivial: `OcTree::Create()` everywhere; remove the `±1000, 2` cargo-cult.
- The Profiler's FPS tab gains a "Spatial" section: live root extents, total node count, depth-occupancy summary, plus a "Draw OcTree Bounds" toggle that produces a wireframe overlay of every occupied tree-node AABB, color-coded by depth.
- All of the above stays optional and opt-in — the overlay defaults off, the readout is a few `ImGui::Text` lines.

**Non-Goals:**
- Shrinking the root when nodes leave a region. Octrees don't gracefully shrink without re-inserting everything; the wins from the auto-grow case dominate.
- Loose octrees, BVHs, or any change to the partitioning algorithm beyond the Create/Reset surface.
- Serializing octree topology. The tree is rebuilt from `AddSceneNodeRecursively` at scene-load time today (`Scene.cpp:37` + `Importer::MergeInto`), and that contract is preserved.
- Per-leaf node-count limits or threshold-based subdivision. The depth cap stays the only knob.
- Multi-threaded inserts or thread-safe queries. The tree is single-threaded today; we don't add locking.
- Replacing `std::pow` in `GetFitNode` with `1 << i`. Tempting, but unrelated micro-cleanup; out of scope.

## Decisions

### Decision 1: Default to a `±1000` cube root; no lazy seeding

`OcTree`'s zero-arg and one-arg constructors eagerly create the root with a `±OcTree::kDefaultHalfExtent` cube (`kDefaultHalfExtent = 1000.0f`). The `Create()` factory is a thin shim over the constructor. Behavior:

- `OcTree::Create()` → root spans `[(-1000,-1000,-1000), (1000,1000,1000)]`, `m_MaxDepth = 8`.
- `OcTree::Create(maxDepth)` → same root extents, custom max depth.
- `OcTree::Create(min, max, maxDepth = 8)` → caller-supplied extents (the legacy form). Use this when you know your scene's bounds up front and want to avoid wrap operations during a batch load.

`OcTree::Reset()` and `OcTree::Reset(unsigned int)` likewise restore the default-cube root. `Reset(min, max, depth)` still takes caller-supplied extents.

**Why default-cube over lazy-seed-from-first-insert:**
- The first version of this change seeded lazily from the first inserted node's worldAABB. In testing across three demo scenes (a humanoid, tanks-on-grass, and an outdoor diorama) all three ended up with identical root extents `(-76,-84,-92) → (52,44,36)` because whichever scene seeded first won, and `Scene::Clear` doesn't reset the root. Sharing root extents across unrelated scenes is the wrong default.
- The wrap path is the load-bearing piece. Once that exists, "what bounds do you start with" stops being a correctness question and becomes an optimization knob. A `±1000` default covers nearly every authoring-scale scene without a single wrap; out-of-range scenes wrap as needed; batch loaders that already know their bounds pass them in and skip the wraps.
- "Lazy seed sized to the first node" is also worse for batch scene loads: each subsequent insert that drifts past the snug seeded cube triggers a wrap, and you may pay several wraps before the loop is done. Starting at `±1000` typically pays zero wraps.

**Alternative considered:** keep lazy seeding but invalidate the root on `Scene::Clear`. Rejected — "the tree's root depends on the order in which scenes happened to load" is harder to reason about than a fixed default. The lazy-seed approach also fails offline tools that construct an empty tree and query it before inserting (less common, but real).

**Alternative considered:** make `kDefaultHalfExtent` configurable globally. Rejected — global config knobs for "what's a good default" tend to drift and disagree across embedders. Either you know your bounds (pass them to `Create`) or you don't (accept the default).

### Decision 2: Lazy 2× root-wrap when a node falls outside

When `AddSceneNode`'s top-level call sees `!m_Root->GetAABB().Contains(nodeBounds)` (or the looser test "any axis of `nodeBounds` exceeds the root"), wrap the root:

1. Determine the *direction* of the out-of-bounds extent — for each axis, is the node's center on the +side or –side of the current root's center?
2. Construct a new root AABB twice the current root's size, positioned so the *opposite* corner is the current root's matching corner. (E.g., if the node is up-right-back of the root center, the new root extends up-right-back; the current root becomes the down-left-front child of the new root — child index `0` under the layout in `OcTreeNode.h:11-19`.)
3. Allocate the new root via `OcTreeNode::Create(*this, nullptr, newMin, newMax)`. Attach the *old* root as the appropriate child of the new root (one of the eight child slots gets the existing subtree by direct pointer move). The old root's `m_Parent` updates to the new root, and the new root's `m_TotalSceneNodeCount` is initialized to the old root's count.
4. Repeat until `m_Root->GetAABB().Contains(nodeBounds)`. Capped at 32 wraps as a runaway-loop guard (32 doublings = 4 billion-unit world extent, well past float precision; if we ever hit this the user has bigger problems).

The crucial property: existing `m_SceneNodes` lists at every tree-node — and the assignments stored in `SceneNode::m_OcTreeNode` weak_ptrs (`SceneNode.cpp:190-192`) — are unaffected. The new root is just one extra layer above; everything below remains valid.

`m_MaxDepth` is *not* incremented during wrap. Wrapping makes leaves coarser (a node that was at depth 4 is effectively at depth 5 of the new tree, but its `m_AABB` doesn't change). If the maximum useful depth was 8 before, it's 8 after — we just have a bigger root. If users want finer subdivision after many wraps they can re-create the tree.

**Alternative considered:** rebuild the tree from scratch when an out-of-bounds insert arrives. Rejected — that's O(n) work on insert. Lazy wrap is O(log(extent-ratio)), typically 1–3 wraps for any reasonable scene, and the work is shared by all subsequent in-bounds inserts.

**Alternative considered:** detect the direction once and grow in *all* directions equally (i.e., 3× the current extent centered on the same point). Rejected — that doesn't preserve the existing root as a child cleanly; it forces a re-bin of every existing scene node into the new structure.

### Decision 3: Default `maxDepth` becomes `8`, applied at the `Create` layer

Every existing call site passes `2`. Looking at the geometry — `±1000` world, `2` levels → leaf cubes of 500³ — that's three levels too few for the bundled tank scene; one tank's worldAABB is ~5×5×5, which already fits `IsTwiceSize` at depth 4 (root → 1000³ → 500³ → 250³ → 125³ → leaf 62.5³). Bumping to 8 gives leaves of ~7.8³ at the original `±1000` root, fine for the scenes we ship.

Why 8 and not (say) 6: 6 was the old C++ default that no one used. 8 is the balance point where leaf size is small enough to discriminate small props, large enough that the tree doesn't waste memory on near-empty subtrees. Modeling-engine convention.

**Alternative considered:** make `maxDepth` adaptive to the number of inserted nodes. Rejected — coupling the tree's structural parameter to its content count makes "where does this scene node end up" non-deterministic and harder to debug. We'd rather expose a fixed-but-good default than a clever-but-mysterious one.

### Decision 4: Spatial readout lives inside the FPS tab; tab structure stays

Per user direction during the propose call: don't add a fourth tab. Instead, add a `Separator` inside `RenderProfilerFpsTab` after the existing "Use Cascaded Shadow Map" checkbox (`EditorWindows.cpp:160`) and emit:

- `ImGui::Text("OcTree:");`
- Min / Max / extent of the current root (or `"(unrooted)"` when `IsRooted() == false`).
- Total tracked scene-node count (`m_Root->GetTotalSceneNodeCount()`).
- Total occupied tree-node count (a new `OcTree::GetOccupiedNodeCount()` that does a depth-first count of allocated `OcTreeNode`s — at most a few hundred even for large scenes; cheap once per frame).
- Max occupied depth (`OcTree::GetMaxOccupiedDepth()` — same DFS, tracks the deepest depth where `m_SceneNodes` is non-empty).
- `ImGui::Checkbox("Draw OcTree Bounds", &draw_octree_bounds);` feeding `Pipeline::Active->SetSwitch(PipelineSwitch::OCTREE_BOUNDS, draw_octree_bounds);`

The readout pulls from `Scene::Active->GetSceneManager()` cast (via `dynamic_cast` or via the existing `GetTypeIndex()` check) to `OcTree`. If the scene manager isn't an `OcTree`, render the section as `(non-octree scene manager)` — preserves the future-extension hook.

**Alternative considered:** put the readout in a new "OcTree" tab (the original recommendation). Withdrawn at the user's preference; the section in FPS tab is the chosen approach.

### Decision 5: `PipelineSwitch::OCTREE_BOUNDS` joins the existing debug-render gate

Add `OCTREE_BOUNDS` to the `PipelineSwitch` enum in `Pipeline.h:39-46`, before `LENGTH`. Update `PrelightPipeline.cpp:159-161` to include the new switch in the `IsSwitchOn({...}, true)` list that decides whether to call `DrawDebug`. In `Pipeline::DrawDebug`, after the existing `customBoundsOn` block, add:

```cpp
if (IsSwitchOn(PipelineSwitch::OCTREE_BOUNDS) && Scene::Active)
{
    if (auto tree = std::dynamic_pointer_cast<OcTree>(Scene::Active->GetSceneManager()))
        tree->DrawDebugBounds(renderUtil);  // new method
}
```

`OcTree::DrawDebugBounds(RenderUtil&)` walks every allocated `OcTreeNode` and emits one `DrawBoxBounds` per node, with color picked from a depth-indexed palette (e.g., `Color::White → Yellow → Green → Cyan → Blue → Magenta`, modulo 6). Empty nodes — `m_TotalSceneNodeCount == 0` — are skipped to keep the overlay readable.

**Alternative considered:** ship a `RenderUtil::DrawOcTree` helper instead of a method on `OcTree`. Rejected — `RenderUtil` doesn't currently know about `OcTree` (correctly, layering-wise). The walk belongs on the tree; only the per-AABB draw call belongs on `RenderUtil`.

### Decision 6: Lua `OcTree.Create` supports zero / one / three argument forms via `sol::overload`

`engine/Fury/LuaBindings.cpp:166-169` becomes:

```cpp
lua.new_usertype<OcTree>("OcTree",
    sol::no_constructor,
    "Create", sol::overload(
        []() { return OcTree::Create(); },
        [](unsigned int d) { return OcTree::Create(d); },
        &OcTree::Create  // (Vector4, Vector4, unsigned int)
    ));
```

This keeps the existing three-arg site (`Editor.lua:174-177`) valid; once that file is updated to `OcTree.Create()` it picks up the zero-arg path.

**Alternative considered:** a single sol2 callable that inspects its args. Rejected — `sol::overload` is the documented, type-checked path; manual dispatch invites typing-error regressions.

### Decision 7: `OcTree::Reset` semantics

`Reset(min, max, maxDepth)` exists today (`OcTree.cpp:197-201`). Keep it. Add `Reset()` (zero-arg) that re-puts the tree into the unbounded state (drop `m_Root`, reset `m_MaxDepth` to default). Add `Reset(maxDepth)` for symmetry. Reset never auto-seeds; the next insert does.

## Risks / Trade-offs

- **Risk: 2× wrap pathology when a long-running scene drifts continuously in one direction.** Each wrap doubles the root extents but the inserted node only has to escape by one quantum past the current root edge. A pathological loop ("insert at +1010, +2020, +4040, …") would force a wrap on every insert. → Mitigation: the wrap step *over*shoots — when wrapping in direction +X, the new root is `[old_min, old_max + (old_max - old_min)]`, doubling the extent in that direction so the next +X insert almost certainly fits. In practice scenes don't drift like this; we're not optimizing for adversarial input.

- **Risk: Float precision degradation as the root grows.** After 32 wraps the root is `2^32` units across; per-leaf coordinates lose precision. → Mitigation: 32 wraps from a starter `8³` cube is `8 * 2^32 ≈ 3.4 × 10^10` units, far beyond any reasonable scene. The 32-wrap cap exists as a guardrail; if it triggers, we log `FURYE` and stop wrapping (the node stays in the root's linear-scan bucket — same as today's behavior). Not worse than current.

- **Risk: Depth-color palette collisions on `Draw OcTree Bounds` when the tree is deeper than the palette length.** → Acceptable: 6-color palette wraps at depth 6, which means depths 0/6/12 share a color. Visually fine for the editor — the user can read depth from the readout if precision is needed.

- **Risk: Forward-compat for `OcTreeNode::Create` from Lua / external callers.** The new wrapping logic mutates `OcTreeNode::m_Parent` (which is currently set once at construction). → Mitigation: `OcTreeNode` is not bound to Lua and is internal to the `Fury` namespace. Only `OcTree` itself touches it. Document the post-construction `m_Parent` mutation in a comment in `OcTreeNode.h`.

- **Trade-off: We're not addressing the deeper "OcTreeNode `IsTwiceSize` drops large objects to the parent" issue.** A scene node larger than the smallest current tree-node still ends up in the root or a coarse parent and gets linear-scanned. The auto-size change does not make this worse; bumping the default depth to 8 makes the leaves smaller, which actually mitigates it. → Out of scope: a real fix requires loose octrees or BVHs; that's a much bigger change.

- **Trade-off: `Scene::Clear` empties scene-node membership but does not shrink a wrapped root.** A scene that wraps to `±4000` then gets cleared and re-imported keeps the `±4000` root. → Acceptable: the only cost is a wider wireframe in the debug overlay; query performance is unchanged because the wrap layers contain no scene nodes after clear and `WalkScene` skips empty subtrees. Callers that want a fresh baseline can call `OcTree::Reset()` (which restores `±1000`).

- **Trade-off: We're hardcoding the 2× wrap factor.** A 4× wrap would converge faster on degenerate cases but waste more of the tree's volume. 2× is the simplest and the standard-textbook choice; we accept the trade.

## Migration Plan

1. Land `engine/Fury/OcTree.{h,cpp}` and `OcTreeNode.{h,cpp}` changes (defaults, lazy seed, wrap, `IsRooted`, `GetOccupiedNodeCount`, `GetMaxOccupiedDepth`, `DrawDebugBounds`). Test: existing `Editor.lua` still runs because it explicitly passes `(±1000, 2)`. ⇒ All existing scenes load and render identically.
2. Land `engine/Fury/Pipeline.{h,cpp}`: new `PipelineSwitch::OCTREE_BOUNDS` + the new branch in `DrawDebug`. Land the gate update in `engine/Fury/PrelightPipeline.cpp:159-161`.
3. Land `engine/Fury/Editor/EditorWindows.cpp` Spatial section.
4. Update call sites: `engine/Fury/Scene.cpp`, `engine/Fury/GltfImporter.cpp`, `engine/Fury/Cli.cpp`, `examples/Editor.lua` — all four switch to `OcTree::Create()`.
5. Land `engine/Fury/LuaBindings.cpp` `sol::overload` change.
6. Build, run `./fury Editor.lua`, verify (a) the demo scene renders identically, (b) the Profiler FPS tab now shows the Spatial readout, (c) "Draw OcTree Bounds" toggles a wireframe overlay, (d) no `EROR` lines in `Log.txt`.
7. Commit; do not push without confirmation.

No deploy or rollback complexity — single-PR, single-commit revert if needed.

## Open Questions

- Should `OcTree::DrawDebugBounds` skip *empty subtrees* recursively (don't draw a parent that has zero `m_TotalSceneNodeCount`) or skip only individually-empty nodes? Current plan: skip when `m_TotalSceneNodeCount == 0` — that prunes both empty leaves and the parents-of-only-empty-leaves naturally.
- Should we expose `OcTree.GetMaxOccupiedDepth()` and `OcTree.GetOccupiedNodeCount()` to Lua? Listed as a follow-up; not strictly needed for this change since the editor uses the C++ surface directly.
- Should `Pipeline::DrawDebug`'s OcTree path color empties subtly (alpha 0.2) instead of skipping them, so the user can see the partitioning structure even where nothing lives? Non-blocking; defer to user feedback after first checkpoint.
