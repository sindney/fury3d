## Why

`OcTree::Create` requires the caller to declare the world's `min`/`max` bounds up front. Every current call site (`Editor.lua:174`, `Scene.cpp:37`, `GltfImporter.cpp:1090`, `Cli.cpp:422`) hardcodes `±1000`, which is a guess: scenes that fit inside it work, scenes that drift past it silently fall through to the root-level `m_SceneNodes` bucket and lose all spatial speedup. There is no visibility into where nodes actually land — the editor's Profiler shows draw counts and shadow maps but nothing about the octree partitioning that drives them. Two fixes belong together: make the tree resize itself instead of forcing a guess, and give the editor a debug view so the partitioning is observable.

## What Changes

- Make `OcTree::Create`'s `min` / `max` parameters optional. When omitted, the tree starts with a default `±1000` cube root (`OcTree::kDefaultHalfExtent`). Callers that know their scene bounds up front can pass an explicit `min`/`max` to `Create` to avoid runtime wraps during batch loads.
- Add lazy root growth. When `AddSceneNode` (or `UpdateSceneNode`) receives a node whose `WorldAABB` is not contained by the current root's AABB, the tree wraps the existing root in a new 2× root in the direction of the out-of-bounds extent — repeating until the new root contains the node. The wrap reuses the existing subtree (one of the eight children of the new root *is* the old root); no rebuild, existing tracked nodes keep their octree-node assignments.
- Bump the default `maxDepth` from the existing per-call default of `6` (and the current `2` used by every call site) to a single sane default of `8`, exposed via the same optional-parameter mechanism. Justification in design.md.
- Update existing call sites (`Editor.lua:174`, `Scene.cpp:37`, `GltfImporter.cpp:1090`, `Cli.cpp:422`) to drop the `±1000` / `depth=2` arguments and let the defaults apply.
- `OcTree::Reset()` (and `Reset(unsigned int)`) restore the default-cube root rather than leaving the tree unrooted, so a `Scene::Clear` followed by an import of a new scene starts from a clean baseline rather than inheriting whatever the previous scene's wraps grew to.
- Add a "Spatial" section inside the **Profiler → FPS** tab in `engine/Fury/Editor/EditorWindows.cpp::RenderProfilerFpsTab`: live readout of the octree's current root min/max, total tracked scene nodes (root's `GetTotalSceneNodeCount`), how many subdivisions exist (count of allocated `OcTreeNode` instances), the maximum depth actually populated this frame, and a "Draw OcTree Bounds" checkbox.
- Add `PipelineSwitch::OCTREE_BOUNDS` and a corresponding wireframe-render path in `Pipeline::DrawDebug` that walks the active scene's `SceneManager` (when it's an `OcTree`) and emits each occupied tree-node's AABB as `RenderUtil::DrawBoxBounds`. Color-code by depth.

## Capabilities

### New Capabilities
- `octree-spatial`: The octree's runtime contract — auto-seeding the root from the first inserted node, lazy 2× root-wrapping when an out-of-bounds node arrives, and the editor-visible debug view (Profiler readout + `PipelineSwitch::OCTREE_BOUNDS` wireframe overlay). Covers the new optional `Create` arguments and the documented growth invariants that future scripts and tools depend on.

### Modified Capabilities
<!-- None. The existing `engine-presentation` spec covers Profiler tab structure but not its contents per-tab; the Spatial section is additive. The OcTree itself has never had a published spec — this change creates the first one. -->

## Impact

- Code: `engine/Fury/OcTree.{h,cpp}`, `engine/Fury/OcTreeNode.{h,cpp}`, `engine/Fury/Pipeline.{h,cpp}` (new `OCTREE_BOUNDS` switch + DrawDebug branch), `engine/Fury/PrelightPipeline.cpp` (include the new switch in the `DrawDebug` gate at line 159), `engine/Fury/Editor/EditorWindows.cpp` (Spatial section in the FPS tab), `engine/Fury/LuaBindings.cpp` (loosen `OcTree.Create` to accept zero-arg / one-arg / three-arg forms), `engine/Fury/Scene.cpp`, `engine/Fury/GltfImporter.cpp`, `engine/Fury/Cli.cpp`, `examples/Editor.lua`.
- Public C++ API: `OcTree::Create` gains default values for `min` / `max` / `maxDepth`; the existing three-arg overload remains valid. `OcTree::Reset()` (zero-arg) and `Reset(unsigned int)` are added; both restore the default-cube root. New `OcTree::IsRooted()` / `GetRootAABB()` / `GetTotalSceneNodeCount()` / `GetOccupiedNodeCount()` / `GetMaxOccupiedDepth()` / `DrawDebugBounds()` accessors for the editor.
- Public Lua API: `OcTree.Create()` (no args), `OcTree.Create(maxDepth)`, and the existing `OcTree.Create(min, max, maxDepth)` all dispatch correctly. Backwards-compatible.
- Behavior: Scenes that fit inside `±1000` continue to work identically. Scenes that drift past `±1000` — currently invisible because they end up at the root bucket — now keep their spatial structure as the root grows. Batch scene loads that know their bounds up front can call `OcTree::Create(min, max)` to avoid wrap operations during the load. The Profiler's FPS tab gets a new readable section; nothing else moves.
- No change to scene serialization, asset formats, shader paths, or pipeline JSON. The new `OCTREE_BOUNDS` switch is runtime-only and defaults `false`.
- Out of scope: re-balancing the tree when nodes shrink back inside a smaller volume (the wrapping is one-way — the root never shrinks); switching the underlying structure to a loose octree or a BVH; serializing octree state (it's reconstructed from `AddSceneNodeRecursively` on load, as it always has been).
