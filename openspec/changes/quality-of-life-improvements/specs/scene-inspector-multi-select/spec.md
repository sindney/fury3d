## ADDED Requirements

### Requirement: The Scene Inspector row hitbox SHALL cover the entire row

Each node row in the Scene Inspector SHALL register a click on the entire row, not only on the visible text label. The implementation SHALL use `ImGui::Selectable` (or equivalent) with `ImGuiSelectableFlags_SpanAllColumns` (or a manual full-row rect) so clicking anywhere on the row to the left of the horizontal scroll boundary selects the node.

The tree-node expand/collapse arrow (the small triangle next to the name) SHALL remain the only place where a single click toggles the open/collapsed state. Single-clicking the rest of the row SHALL select; double-clicking the row SHALL continue to obey the existing parent-toggle / leaf-frame behaviour.

#### Scenario: Click on the empty area of a row selects the node

- **WHEN** the user single-clicks the empty area to the right of a node row's name but before the hover buttons
- **THEN** that node becomes the editor's selected anchor node

#### Scenario: Click on the arrow toggles expand/collapse

- **WHEN** the user single-clicks the expand/collapse arrow on a parent row
- **THEN** the row's open state toggles
- **AND** the selection is not changed

#### Scenario: Hitbox does not extend over the hover buttons

- **WHEN** the user clicks on the hover "+" or "..." buttons
- **THEN** the row click is not consumed and the button's own action runs

### Requirement: The Scene Inspector SHALL support multi-node selection

The Scene Inspector SHALL support selecting more than one node at a time. The selection SHALL be exposed to the rest of the editor as an ordered set with a designated **anchor** node. The anchor is the most-recently clicked non-toggle node; the rest are **members**. The anchor is always a member of the set. The set MAY be empty.

Inputs that modify the selection:

- A single click on a row (no modifier) SHALL set the selection to `{clicked}` and set the anchor to that node.
- A click with the Ctrl modifier SHALL toggle the clicked node in the selection. After the toggle the anchor SHALL be the clicked node (whether it was added or removed). If the resulting set has one element, the anchor is that element.
- A click with the Shift modifier SHALL select the contiguous range from the current anchor to the clicked node (in the current visible tree order, depth-first over expanded nodes), overwriting the existing selection. The anchor SHALL be left unchanged. The set SHALL include both endpoints.
- A click with no modifier on a row that is already the only selected node SHALL leave the selection unchanged (and the node stays selected).

Existing single-node consumers (component panel, gizmo, frame-on-double-click) SHALL continue to use the anchor as the single selected node. The Lua-visible `selection` accessor SHALL return a Lua array of `SceneNode*` usertypes in anchor-first order.

#### Scenario: Plain click resets to a single selection

- **WHEN** the user has three nodes selected and clicks a fourth node with no modifier
- **THEN** the selection becomes `{fourth}` and the anchor is the fourth node

#### Scenario: Ctrl-click adds a node

- **WHEN** the user has `{A, B}` selected with anchor `B` and Ctrl-clicks node `C`
- **THEN** the selection becomes `{A, B, C}` with anchor `C`

#### Scenario: Ctrl-click removes a node

- **WHEN** the user has `{A, B, C}` selected with anchor `C` and Ctrl-clicks node `B`
- **THEN** the selection becomes `{A, C}` with anchor `B`
- **AND** single-node consumers now read `C` (the sole non-anchor member) as the anchor per the rule that the anchor is updated to the clicked node

#### Scenario: Shift-click selects a contiguous range

- **WHEN** the user has anchor `B` and Shift-clicks node `E` where the visible tree order is `A, B, C, D, E, F`
- **THEN** the selection becomes `{B, C, D, E}`
- **AND** the anchor stays `B`

#### Scenario: Empty selection clears the editor highlight

- **WHEN** the user removes the last node from the selection (e.g. by Ctrl-clicking the only selected node with no anchor replacement)
- **THEN** the gizmo and the component panel show no selected node

#### Scenario: Single-node consumers use the anchor

- **WHEN** the user has `{A, B, C}` selected with anchor `B`
- **THEN** the component panel renders the components of `B`
- **AND** the gizmo is drawn at `B`'s transform

### Requirement: The right-click context menu SHALL gate entries by selection size

The Scene Inspector's right-click context menu SHALL disable entries that are not meaningful for the current selection:

- With exactly one selected node, every entry is enabled (subject to the existing Delete-on-root / Rename-on-root rules).
- With more than one selected node, only **Delete** is enabled. **Add Child**, **Duplicate**, and **Rename** are disabled and rendered with a greyed-out style. A tooltip on the disabled entry explains that the operation requires a single selection.
- The root node's row SHALL continue to offer only **Add Child** regardless of selection size.

The context menu SHALL be populated based on the **anchor** node (or the root, when the anchor is the root) so the menu title and disabled-state set are consistent.

#### Scenario: Single selection enables every entry

- **WHEN** the user right-clicks a non-root node while exactly one node is selected
- **THEN** the menu offers `Add Child`, `Duplicate`, `Rename`, `Delete` all enabled

#### Scenario: Multi selection disables per-node entries

- **WHEN** the user right-clicks a node while three nodes are selected
- **THEN** the menu shows `Add Child`, `Duplicate`, `Rename` as greyed-out / disabled
- **AND** `Delete` is enabled
- **AND** hovering a disabled entry shows the tooltip "Requires a single selection"

#### Scenario: Delete on a multi-selection removes every selected node

- **WHEN** the user picks `Delete` while three nodes are selected
- **THEN** all three nodes are detached from their parents
- **AND** the editor's selection is cleared

#### Scenario: Root row menu ignores multi-select

- **WHEN** the user right-clicks the root row while many nodes are selected
- **THEN** the menu offers only `Add Child` (Delete / Duplicate / Rename omitted, as before)

### Requirement: Multi-select SHALL be cleared by scene-level events

The Scene Inspector SHALL clear the multi-selection (returning the editor to an empty selection) when the user:

- Opens a new scene via `File → New` or `File → Open`
- Imports a new scene via `File → Import`
- Loads the default scene at startup

The selection SHALL NOT be cleared when the user adds a new child, duplicates, or renames a node — those operations update the selection as before (cleared, set to the new node, or unchanged, depending on the existing single-node semantics).

#### Scenario: File New clears the selection

- **WHEN** the user has several nodes selected and picks `File → New`
- **THEN** the Scene Inspector renders no row as selected
- **AND** the gizmo is not drawn

#### Scenario: Add Child does not clear the selection

- **WHEN** the user picks `Add Child` on the single currently-selected node
- **THEN** the new child becomes the selected anchor
- **AND** the prior selection state is replaced only by the new child
