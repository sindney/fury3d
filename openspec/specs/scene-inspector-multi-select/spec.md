# scene-inspector-multi-select

## Purpose

The Scene Inspector supports multi-node selection so batch operations (delete, future group ops) work without scripting.

## Requirements

### Requirement: The Scene Inspector row hitbox SHALL cover the entire row

Each node row in the Scene Inspector SHALL register a click on the entire row, not only on the visible text label. Implementation uses `ImGui::TreeNodeEx(...)` with `ImGuiTreeNodeFlags_SpanFullWidth` so the clickable rect covers the full row width including empty space past the label.

The tree-node expand/collapse arrow (the small triangle next to the name) is the only place where a single click toggles open/collapsed.

#### Scenario: Click on the empty area of a row selects the node

- **WHEN** the user single-clicks the empty area to the right of a node row's name
- **THEN** that node enters the selection state per the multi-select rule

### Requirement: Multi-node selection

The Scene Inspector SHALL support selecting more than one node at a time. The selection is a struct of ordered members plus an **anchor** -- the most-recently clicked non-toggle node. The anchor is always one of the members. The set MAY be empty.

Storage: `Editor::SceneNodeSet { std::vector<SceneNode*> members; SceneNode* anchor; }`. The single-node accessor `Editor::GetSelectedSceneNode()` returns the anchor and remains the entry point for existing single-node consumers (component panel, gizmo, frame-on-double-click).

Public APIs:
- `Editor::GetSelectionSet() const SceneNodeSet&`
- `Editor::SetSelectionSet(const SceneNodeSet&)`
- `Editor::ClearSelection()`
- `Editor::SetSelectedSceneNode(node)` is a single-node wrapper that resets the set to `{node}` with `node` as the anchor.
- Lua: `Editor.GetSelection()` returns a Lua array of `SceneNode*` usertypes in anchor-first order; `Editor.ClearSelection()` clears.

Click rules:
- Plain click on a row: selection = `{clicked}`, anchor = clicked. No-op if the row is already the only-selected node.
- Ctrl-click on a row: toggle `clicked` in the set; anchor = clicked (whether added or removed).
- Shift-click on a row: select the contiguous visible range from the current anchor to the clicked row (depth-first over expanded nodes); anchor unchanged.

#### Scenario: Plain click resets to a single selection

- **WHEN** the user has three nodes selected and clicks a fourth node with no modifier
- **THEN** the selection becomes `{fourth}` and the anchor is the fourth node

#### Scenario: Ctrl-click adds a node

- **WHEN** the user has `{A, B}` selected with anchor `B` and Ctrl-clicks node `C`
- **THEN** the selection becomes `{A, B, C}` with anchor `C`

#### Scenario: Ctrl-click removes a node

- **WHEN** the user has `{A, B, C}` selected with anchor `C` and Ctrl-clicks node `B`
- **THEN** the selection becomes `{A, C}` with anchor `B`

#### Scenario: Shift-click selects a contiguous range

- **WHEN** the user has anchor `B` and Shift-clicks node `E` where the visible tree order is `A, B, C, D, E, F`
- **THEN** the selection becomes `{B, C, D, E}`
- **AND** the anchor stays `B`

#### Scenario: Single-node consumers use the anchor

- **WHEN** the user has `{A, B, C}` selected with anchor `B`
- **THEN** the component panel renders the components of `B`
- **AND** the gizmo is drawn at `B`'s transform

### Requirement: Right-click context menu SHALL gate entries by selection size

The Scene Inspector's right-click context menu SHALL disable entries that are not meaningful for the current selection:

- With exactly one selected node, every entry is enabled (subject to the existing Delete-on-root / Rename-on-root rules).
- With more than one selected node, only `Delete` is enabled. `Add Child`, `Duplicate`, and `Rename` are disabled and rendered greyed-out with the tooltip "Requires a single selection".
- The root node's row SHALL continue to offer only `Add Child` regardless of selection size.

The menu is populated based on the **anchor** node. When the user right-clicks a row that is not in the current selection, the row is promoted to anchor inside `BeginPopupContextItem` (selection-promotion lives in the popup block so it only fires on a real right-click, not on every frame the inspector renders).

`Delete` iterates each member of the selection set, detaches every one from its parent, and then clears the selection.

#### Scenario: Single selection enables every entry

- **WHEN** the user right-clicks a non-root node while exactly one node is selected
- **THEN** the menu offers `Add Child`, `Duplicate`, `Rename`, `Delete` all enabled

#### Scenario: Multi-selection disables per-node entries

- **WHEN** the user right-clicks while three nodes are selected
- **THEN** the menu shows `Add Child`, `Duplicate`, `Rename` as greyed-out / disabled
- **AND** `Delete` is enabled
- **AND** hovering a disabled entry shows the tooltip "Requires a single selection"

#### Scenario: Delete removes every selected node

- **WHEN** the user picks `Delete` while three nodes are selected
- **THEN** all three nodes are detached
- **AND** the editor's selection is cleared

#### Scenario: Root row menu ignores multi-select

- **WHEN** the user right-clicks the root row while many nodes are selected
- **THEN** the menu offers only `Add Child`

### Requirement: Multi-select SHALL be cleared by scene-level events

The Scene Inspector SHALL clear the multi-selection when the user opens a new scene (`File -> New` / `File -> Open`) or imports one (`File -> Import`). Per-scene editor state (selection, opened tree rows) is reset in `Editor::ResetForNewScene()` called from each trigger.

#### Scenario: File New clears the selection

- **WHEN** the user has several nodes selected and picks `File -> New`
- **THEN** the Scene Inspector renders no row as selected
- **AND** the gizmo is not drawn
