# inspector-reveal-on-pick

## Purpose

When the user picks a node in the viewport, the Scene Inspector must show the picked node without a manual expansion hunt, while preserving the user's scroll position and the camera pose.

## Requirements

### Requirement: A viewport pick SHALL auto-expand the picked node's ancestor chain

On a viewport pick that resolves to a non-null `SceneNode`, the Scene Inspector SHALL force-open every collapsed ancestor of the picked node by writing `Editor::SetNodeOpen(p, true)` for each ancestor in the chain via `Editor::RevealInInspector(picked)`.

The reveal SHALL NOT modify the open/closed state of any sibling subtree. The reveal SHALL NOT scroll the inspector or move the camera -- camera framing is owned by the Scene Inspector's leaf-double-click path via `Editor::FrameSelection`.

Implementation:
- Picker (`EditorPicking.cpp`) calls `Editor::SetSelectedSceneNode(picked)` then `Editor::RevealInInspector(picked)` after a successful pick.
- `Editor::RevealInInspector(p)` walks the parent chain via `SceneNode::GetParent()` up to the root, calling `Editor::SetNodeOpen` for each currently-collapsed ancestor.
- `RenderNodeRow` seeds `SetNextItemOpen(Editor::IsNodeOpen(node))` each frame from `g_OpenedNodes`, which the reveal updates.

#### Scenario: Pick a node inside a collapsed subtree

- **WHEN** the user clicks the viewport and the picked node is a child of one or more collapsed parents
- **THEN** every collapsed ancestor is force-opened
- **AND** the picked node is selected
- **AND** the camera pose is unchanged
- **AND** the inspector's vertical scroll position is unchanged

#### Scenario: Pick a node whose ancestors are already expanded

- **WHEN** the user picks a node whose ancestors are all expanded
- **THEN** the visible tree is unchanged
- **AND** the picked node becomes the selection
- **AND** the camera pose is unchanged

#### Scenario: Pick misses all nodes

- **WHEN** the picker resolves to null
- **THEN** no row is force-opened
- **AND** the editor's selection is cleared
- **AND** the camera pose is unchanged
