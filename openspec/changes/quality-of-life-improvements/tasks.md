## 1. Scene Inspector selection model

- [x] 1.1 Add `SceneNodeSet` struct (members + anchor) to the editor's internal state. Replace the existing single-`SceneNode*` selected-node member with the new struct.
- [x] 1.2 Keep the legacy `Editor::SetSelectedSceneNode(node)` / `Editor::GetSelectedSceneNode()` API as a wrapper that maps to the anchor (sets members = {node}, anchor = node). Existing single-node consumers (component panel, gizmo, frame-on-double-click) continue to call the wrapper.
- [x] 1.3 Expose `Editor::GetSelectionSet()` returning the full set in anchor-first order. Add a Lua binding `Editor.GetSelection()` that returns a Lua array of `SceneNode*` usertypes.
- [x] 1.4 Clear the selection on `File → New`, `File → Open`, `File → Import`, and on a scene-load that fully replaces the active scene.

## 2. Scene Inspector row hitbox

- [x] 2.1 In the Scene Inspector render loop, switch the row's `TreeNodeEx` to one whose click hitbox covers the full row (the implementation flag is recorded in code; pin it to whichever ImGui flag combination gives the row-wide click without breaking the arrow toggle).
- [x] 2.2 Wire the click handler to the multi-select rules: plain click → set selection to {node}, anchor = node; Ctrl-click → toggle node in selection, update anchor to clicked node; Shift-click → select range from current anchor to clicked node in the current visible tree order, leave anchor unchanged.
- [~] 2.3 SKIPPED per user preference: hover buttons were not wanted. The row click handler still skips the arrow-toggle zone (arrow click only toggles open state).
- [~] 2.4 SKIPPED per user preference: hover buttons (`+` / `...`) intentionally not added.

## 3. Context menu gating

- [x] 3.1 Right-click context menu disables Add Child / Duplicate / Rename when selection has >1 members; Delete always enabled.
- [x] 3.2 Delete iterates selection members, detaches each, then clears the selection.
- [x] 3.3 Selection-promotion logic lives INSIDE `BeginPopupContextItem` (only fires on a real right-click).

## 4. Scene Inspector reveal on pick + scene-clear

- [x] 4.1 `Editor::SetNodeOpen/IsNodeOpen` drive the inspector tree state from a `std::unordered_set<SceneNode*>`.
- [x] 4.2 Picker calls `Editor::SetSelectedSceneNode(picked)` then `Editor::RevealInInspector(picked)`.
- [x] 4.3 `RevealInInspector` walks the parent chain via `SceneNode::GetParent()` and force-opens every collapsed ancestor.
- [x] 4.4 Picker does NOT call `FrameSelection`; camera framing stays on the leaf-double-click path.
- [x] 4.5 Scene-clear: `TriggerNew/Open/Import` clear `g_OpenedNodes` + `g_SelectionSet` so dangling SceneNode* pointers don't leak across scene swaps.

## 5. Settings window collapse default

- [x] 5.1 Make every Settings window section default-collapsed on first open. Use `ImGui::SetNextItemOpen(false, ImGuiCond_FirstUseEver)` per section so missing ini + first appearance both start closed; ImGui's own `[Window]` persistence covers subsequent launches. No custom registry, no extra persistence keys.

## 6. Sky engine-default promotion

- [x] 6.1 Copy `cloud_noise.png` + `moon.png` from `examples/Projects/outdoor/Terrain/` to `examples/Resource/Texture/Sky/`. Verify byte-identical (`cmp`).
- [x] 6.2 Update `setup_terrain_sky_scene.lua` to use the engine-default paths.
- [x] 6.3 Confirm seven atmosphere shaders at `examples/Resource/Shader/Atmosphere/`.
- [x] 6.4 Write `examples/Resource/SKY-README.md` (ASCII-only).

## 7. Verification

- [x] 7.1 WITH_EDITOR=ON build clean.
- [x] 7.2 WITH_EDITOR=OFF build clean; legacy `Editor::Set/GetSelectedSceneNode` wrappers link, `Editor.SetSelection` is no-op stub.
