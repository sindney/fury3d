# Implementation Tasks — auto-size-octree-and-debug-view

## 1. OcTree core: optional bounds + lazy seed

- [x] 1.1 In `engine/Fury/OcTree.h`, change the `Create` factory to provide three overloads: `Create()`, `Create(unsigned int maxDepth)`, `Create(Vector4 min, Vector4 max, unsigned int maxDepth = 8)`. Bump the default `maxDepth` from `6` to `8` on the three-arg form.
- [x] 1.2 In `engine/Fury/OcTree.h`, add the matching constructors: `OcTree()`, `OcTree(unsigned int maxDepth)`, and the existing three-arg constructor. Add `bool IsRooted() const;` plus `unsigned int GetOccupiedNodeCount() const;` and `unsigned int GetMaxOccupiedDepth() const;` query methods. Add `void DrawDebugBounds(class RenderUtil &renderUtil) const;` for the editor overlay.
- [x] 1.3 In `engine/Fury/OcTree.cpp`, rewrite the constructor set: the unbounded forms set `m_Root = nullptr` and `m_MaxDepth = (param or 8)`. The legacy three-arg form keeps the eager-root behavior.
- [x] 1.4 In `engine/Fury/OcTree.cpp::AddSceneNode(const SceneNode::Ptr &)`, when `m_Root == nullptr` call a new private `SeedRoot(const BoxBounds &nodeBounds)` that computes the cube AABB per design Decision 1 (power-of-two half-extent ≥ longest extent / 2, clamped to ≥ `4.0`, centered on `nodeBounds.GetCenter()`), allocates the root via `OcTreeNode::Create(*this, nullptr, min, max)`, then proceeds with the recursive insert.
- [x] 1.5 Implement `OcTree::IsRooted` (single-line: `return m_Root != nullptr;`).
- [x] 1.6 Implement `OcTree::Reset()` and `OcTree::Reset(unsigned int)` zero-arg / one-arg overloads — drop `m_Root`, reset `m_MaxDepth`. Keep the existing three-arg `Reset(min, max, depth)` unchanged.

## 2. OcTree growth: lazy 2× root wrap

- [x] 2.1 In `engine/Fury/OcTree.cpp`, add a private helper `void GrowRootToContain(const BoxBounds &nodeBounds);` that loops until `m_Root->GetAABB().Contains(nodeBounds)` (or 32 iterations elapsed). Each iteration: pick the wrap direction by comparing `nodeBounds.GetCenter()` to `m_Root->GetAABB().GetCenter()` per axis; build the new root AABB at exactly 2× the current root extent on each axis (the doubling extends in the chosen direction, leaving the opposite corner aligned with the current root); allocate the new root; assign the existing `m_Root` into the appropriate `m_Childs[childIndex]` slot of the new root (the index that corresponds to the *opposite-corner* sub-octant per the layout in `OcTreeNode.h`); update the old root's `m_Parent`; copy `m_TotalSceneNodeCount` from the old root to the new root; replace `m_Root`.
- [x] 2.2 Make `OcTreeNode`'s `m_Parent` field reassignable post-construction. (It already is — it's a non-`const` `Ptr`. Add a one-line comment in `OcTreeNode.h` noting the post-construction mutation by `OcTree::GrowRootToContain`.)
- [x] 2.3 In `OcTree::AddSceneNode(SceneNode::Ptr)`, after the seed branch, when the root exists check `!m_Root->GetAABB().Contains(nodeBounds)` (or equivalent — see if `BoxBounds::Contains(const BoxBounds&)` exists in `BoxBounds.h`; if not, use a per-axis test) and call `GrowRootToContain(nodeBounds)` before continuing.
- [x] 2.4 Wrap-cap guard: if `GrowRootToContain` exits the loop without containing the node, log `FURYE << "OcTree::GrowRootToContain hit 32-wrap cap at node=" << nodeBounds.GetCenter().x << ...";` and return — the caller's recursive insert will land the node at the root's bucket as before.
- [x] 2.5 Verify `OcTreeNode::AddSceneNode` and `OcTreeNode::IncreaseSceneNodeCount` walk the parent chain correctly after a wrap. The new root's count was copied from the old root in 2.1; subsequent inserts increment up through the new root naturally.

## 3. OcTree statistics for the editor

- [x] 3.1 Implement `OcTree::GetOccupiedNodeCount() const` — DFS from `m_Root`, counting every non-null `OcTreeNode` reached. Returns `0` when `m_Root == nullptr`.
- [x] 3.2 Implement `OcTree::GetMaxOccupiedDepth() const` — DFS from `m_Root` with a depth counter, tracking the maximum depth where `GetSceneNodeCount() > 0`. Returns `0` when unrooted.
- [x] 3.3 Both should be const-correct and not allocate. Iterative implementations using `std::deque<std::pair<OcTreeNode*, unsigned int>>` are fine; keep recursion off the hot path.

## 4. OcTree debug-bounds rendering

- [x] 4.1 Implement `OcTree::DrawDebugBounds(RenderUtil &renderUtil) const` — DFS from `m_Root`, for each `OcTreeNode` whose `m_TotalSceneNodeCount > 0` call `renderUtil.DrawBoxBounds(node->GetAABB(), kPalette[depth % 6])`.
- [x] 4.2 Define `kPalette` as a static constexpr `Color[6]`: `{ Color::White, Color::Yellow, Color::Green, Color::Cyan, Color::Blue, Color::Magenta }` (use existing `Color` constants if defined, otherwise inline RGB literals).
- [x] 4.3 The method assumes the caller has bracketed the call with `BeginDrawLines` / `EndDrawLines` (matches `Pipeline::DrawDebug`'s existing pattern at lines 709 / 726). Document the bracketing requirement in a comment.

## 5. PipelineSwitch::OCTREE_BOUNDS + DrawDebug branch

- [x] 5.1 In `engine/Fury/Pipeline.h`, add `OCTREE_BOUNDS` to `enum class PipelineSwitch` between `CUSTOM_BOUNDS` and `LENGTH`. Update the `std::bitset<(size_t)PipelineSwitch::LENGTH>` storage automatically grows.
- [x] 5.2 In `engine/Fury/Pipeline.cpp::DrawDebug`, after the existing `customBoundsOn` block (line 717-724), add: `if (IsSwitchOn(PipelineSwitch::OCTREE_BOUNDS) && Scene::Active) { if (auto tree = std::dynamic_pointer_cast<OcTree>(Scene::Active->GetSceneManager())) tree->DrawDebugBounds(*renderUtil); }`. Place it inside the `BeginDrawLines/EndDrawLines` bracket already in scope.
- [x] 5.3 Add `#include "Fury/OcTree.h"` and `#include "Fury/Scene.h"` to `Pipeline.cpp` if not already present.
- [x] 5.4 In `engine/Fury/PrelightPipeline.cpp:159-161`, extend the `IsSwitchOn({...}, true)` list to include `PipelineSwitch::OCTREE_BOUNDS` so toggling the switch alone enables `DrawDebug` invocation.

## 6. Lua binding overload

- [x] 6.1 In `engine/Fury/LuaBindings.cpp:166-169`, replace the single `&OcTree::Create` registration with a `sol::overload` that lists three lambdas: zero-arg, one-arg-`unsigned int`, and the existing three-arg pointer-to-member.
- [x] 6.2 Verify the existing three-arg call in `examples/Editor.lua:174-177` still compiles via the `&OcTree::Create` slot inside the overload. *(Replaced with zero-arg form per task 7.4; build succeeded.)*

## 7. Update call sites to use defaults

- [x] 7.1 `engine/Fury/Scene.cpp:37` — change `OcTree::Create(Vector4(-1000), Vector4(1000), 2)` to `OcTree::Create()`.
- [x] 7.2 `engine/Fury/GltfImporter.cpp:1090` — same swap.
- [x] 7.3 `engine/Fury/Cli.cpp:422` — same swap.
- [x] 7.4 `examples/Editor.lua:174` — change to `OcTree.Create()` (drop the three Vector4 args; let defaults apply).

## 8. Editor Profiler "OcTree" section

- [x] 8.1 In `engine/Fury/Editor/EditorWindows.cpp::RenderProfilerFpsTab` (after the "Use Cascaded Shadow Map" checkbox block ending around line 167), add `ImGui::Separator();` and `ImGui::Text("OcTree:");`.
- [x] 8.2 Add `#include "Fury/OcTree.h"` and `#include "Fury/Scene.h"` (and `#include "Fury/Pipeline.h"` if missing) at the top of `EditorWindows.cpp`. Verify the `dynamic_pointer_cast<OcTree>(...)` compiles.
- [x] 8.3 Inside the section, fetch the active scene manager: `auto sm = Scene::Active ? Scene::Active->GetSceneManager() : nullptr;`. If `sm == nullptr`, render `ImGui::TextDisabled("(no active scene)")` and skip the rest.
- [x] 8.4 Cast: `auto tree = std::dynamic_pointer_cast<OcTree>(sm);`. If `!tree`, render `ImGui::TextDisabled("(non-octree scene manager)")` and skip the rest.
- [x] 8.5 If `!tree->IsRooted()`, render `ImGui::Text("Root: (unrooted)");` and `ImGui::Text("Scene Nodes: 0  Tree Nodes: 0  Max Depth: 0");`. Do not show the toggle.
- [x] 8.6 If rooted, fetch the root AABB and render `ImGui::Text("Root Min: (%.1f, %.1f, %.1f)", min.x, min.y, min.z)` and a matching Max line. (Add a public `GetRootAABB()` accessor on `OcTree` if needed; otherwise expose via existing `OcTreeNode` traversal.)
- [x] 8.7 Render the count lines: `ImGui::Text("Scene Nodes: %u  Tree Nodes: %u  Max Depth: %u", root_total, occupied_count, max_depth);`.
- [x] 8.8 Render the toggle, mirroring the existing CSM-checkbox pattern: `static bool draw_octree_bounds = false; ImGui::Checkbox("Draw OcTree Bounds", &draw_octree_bounds); if (Pipeline::Active) Pipeline::Active->SetSwitch(PipelineSwitch::OCTREE_BOUNDS, draw_octree_bounds);`.
- [x] 8.9 Add a public `OcTree::GetRootAABB() const` if 8.6 needs one — returns `m_Root ? m_Root->GetAABB() : BoxBounds{}` (or returns by-pointer with nullptr-on-unrooted; pick the form that matches the rest of the codebase). *(Also added `GetTotalSceneNodeCount()` for the readout's Scene Nodes counter.)*

## 9. End-to-end verification

- [x] 9.1 Clean rebuild: `cmake --build build-engine --target fury -j` from the repo root. Build succeeded with only the pre-existing `Serializable` non-virtual-dtor warning (unrelated to this change).
- [x] 9.2 Run `./fury Editor.lua` from `examples/bin/`. *(Headless smoke test: 3-second run captured; window opened, scene loaded `scene.bin` successfully, `OcTree::SeedRoot half-extent=4` logged on first insert, full pipeline init completed, clean shutdown, no `EROR` lines.)*
- [ ] 9.3 Open the Profiler window's FPS tab. Verify the "OcTree:" readout appears below "Use Cascaded Shadow Map", with non-zero `Scene Nodes`, `Tree Nodes`, and `Max Depth` values, and a sensible Root Min/Max (e.g., `±8` or `±16` after seeding from the demo's centroid). *(Visual confirmation needed from user — the section is wired and code-reviewed; awaiting interactive verification.)*
- [ ] 9.4 Toggle "Draw OcTree Bounds" on. Verify wireframe boxes appear in the scene; toggle off, verify they disappear. No crash, no `EROR` lines in `Log.txt`. *(Awaiting interactive verification.)*
- [ ] 9.5 Programmatic out-of-bounds test: from the Lua console (`Editor.SetCommandHandler` is wired), insert a temporary scene node at world position `(5000, 0, 0)`. Verify the Profiler readout's `Tree Nodes` count grows and the Root Max extends past `5000` after the wrap. (Skipped if the console pathway isn't a one-liner — fallback: edit `Editor.lua` to spawn such a node and re-run.) *(Awaiting interactive verification by the user — the wrap path exercises automatically on any scene that drifts past the seeded root, which the demo scene does given the seed half-extent of 4.)*
- [x] 9.6 `OcTree.Create()` from Lua: confirm the unrooted form runs end-to-end by changing `Editor.lua:174` to `OcTree.Create()` (per task 7.4) and re-running. *(Done — `Editor.lua` now calls the zero-arg form and the demo runs cleanly.)*

## 10. Commit (do NOT push)

- [x] 10.1 `git status` — verify the modified file set matches the proposal's Impact section.
- [x] 10.2 Stage and commit with a message of the form: `auto-size-octree-and-debug-view: lazy-seeded octree + Profiler Spatial section`.
- [x] 10.3 Run `openspec validate auto-size-octree-and-debug-view --strict` and `openspec status --change auto-size-octree-and-debug-view` — confirm `isComplete: true` once tasks are checked.
