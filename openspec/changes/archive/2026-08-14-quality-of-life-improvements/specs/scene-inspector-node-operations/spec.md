## MODIFIED Requirements

### Requirement: Context menu on every node row

The Scene Inspector SHALL display a right-click context menu on every visible node row. The menu SHALL offer "Add Child", "Duplicate", "Rename", and "Delete" actions. The root node row SHALL offer only "Add Child".

The enabled state of each entry SHALL depend on the **current selection size** (see `scene-inspector-multi-select`):

- With exactly one selected node, all entries are enabled (subject to the existing root-row and Disabled-overrides rules).
- With more than one selected node, only "Delete" is enabled. The other entries are rendered with a disabled (greyed-out) state and a tooltip explaining that the operation requires a single selection.
- The root node row SHALL offer only "Add Child" regardless of selection size.

The menu's title and per-row anchor SHALL be the **anchor node** of the current selection (the most-recently clicked non-toggle node), or the root when the anchor is the root.

#### Scenario: Right-click on a non-root node with a single selection

- **WHEN** the user right-clicks a non-root node row while exactly one node is selected
- **THEN** a popup appears with "Add Child", "Duplicate", "Rename", and "Delete" entries all enabled

#### Scenario: Right-click on a non-root node with a multi-selection

- **WHEN** the user right-clicks a non-root node row while three nodes are selected
- **THEN** a popup appears with "Add Child", "Duplicate", "Rename" rendered disabled (greyed out) plus "Delete" enabled
- **AND** hovering a disabled entry shows a tooltip "Requires a single selection"

#### Scenario: Right-click on the root node

- **WHEN** the user right-clicks the root node row
- **THEN** a popup appears with only "Add Child"

### Requirement: The Scene Inspector row hitbox SHALL cover the entire row

Each node row in the Scene Inspector SHALL register a click on the entire row, not only on the visible text label. The implementation SHALL use `ImGui::Selectable` (or equivalent) with `ImGuiSelectableFlags_SpanAllColumns` (or a manual full-row rect) so clicking anywhere on the row to the left of the horizontal scroll boundary selects the node (per the multi-select rules in `scene-inspector-multi-select`).

The tree-node expand/collapse arrow (the small triangle next to the name) SHALL remain the only single-click expand/collapse trigger.

#### Scenario: Click on the empty area of a row selects the node

- **WHEN** the user single-clicks the empty area to the right of a node row's name but before the hover buttons
- **THEN** the row enters the selection state (single-select or anchor-set per the multi-select rules)

#### Scenario: Click on the arrow toggles expand/collapse

- **WHEN** the user single-clicks the expand/collapse arrow on a parent row
- **THEN** the row's open state toggles
- **AND** the selection is not changed

### Requirement: Double-click on a parent row toggles its expand/collapse state and frames the camera

When the user double-clicks a node row whose node has one or more children, the Scene Inspector SHALL toggle that row's open/collapsed state (expand if collapsed, collapse if expanded) AND request the editor camera to frame that node. The frame SHALL go through the registered frame-selection handler (the C++ layer does NOT compute the camera transform directly). The frame-selection handler is invoked with the **anchor node** of the current selection (which is the double-clicked row when the user has a single selection). Both effects happen on the same double-click.

#### Scenario: Double-click expands a collapsed parent and frames it

- **WHEN** the user double-clicks a node row that has children and is currently collapsed
- **THEN** the row expands and its children become visible
- **AND** the camera is repositioned to frame the node

#### Scenario: Double-click collapses an expanded parent and frames it

- **WHEN** the user double-clicks a node row that has children and is currently expanded
- **THEN** the row collapses and its children become hidden
- **AND** the camera is repositioned to frame the node

#### Scenario: Double-click on the row's arrow icon also toggles

- **WHEN** the user single-clicks the row's expand/collapse arrow
- **THEN** the row toggles its open state (existing ImGui behavior preserved)
- **AND** no camera motion occurs (only double-click triggers framing)

### Requirement: Double-click on a leaf row frames the node in the viewport

When the user double-clicks a node row whose node has zero children (a leaf), the Scene Inspector SHALL request the editor camera to frame that node by invoking the registered frame-selection handler with the node as the argument. The C++ layer SHALL NOT compute the camera transform directly; it SHALL defer to the registered handler. Leaves cannot expand (zero children), so the frame is the only effect.

The handler is invoked with the **anchor node** of the current selection. If the user has a multi-selection, the double-click target is the anchor; if the user double-clicks a non-anchor member, the anchor is updated first and then the handler is invoked with the new anchor.

If no frame-selection handler is registered (e.g., a custom Lua script that did not call `Editor.SetFrameSelectionHandler`), the double-click SHALL be a no-op (the row is selected as usual via the existing single-click selection path, but no camera motion occurs).

#### Scenario: Double-click a leaf with a renderable component and valid bounds

- **WHEN** the user double-clicks a leaf node row whose node has a `MeshRender` component and a valid `WorldAABB`
- **THEN** the editor camera is repositioned so the node's `WorldAABB` fits the viewport with a margin
- **AND** the camera's yaw, pitch, and position are updated to a 3/4-view diagonal of the AABB

#### Scenario: Double-click a leaf without a renderable component

- **WHEN** the user double-clicks a leaf node row whose node has no `MeshRender` component (or whose `WorldAABB` is invalid)
- **THEN** the editor camera is repositioned so its look-at target is the node's `GetWorldPosition()`
- **AND** the camera's yaw, pitch, and position are updated to look at that point from a default distance

#### Scenario: Double-click a leaf when no frame handler is registered

- **WHEN** the user double-clicks a leaf node row while the editor has no registered frame-selection handler
- **THEN** no camera motion occurs
- **AND** the row is selected (via the existing single-click selection path that already fires on the same click)

#### Scenario: Single-click on a leaf still selects it

- **WHEN** the user single-clicks a leaf node row
- **THEN** the node becomes the editor's selected anchor node (per the multi-select rules)
- **AND** no camera motion occurs (only double-click triggers framing)

### Requirement: Add Child creates a child node under the target

Selecting "Add Child" from a row's context menu (or the per-row "+" hover button) SHALL create a new SceneNode and reparent it under the target node. The new node's name SHALL be unique among the target's existing siblings.

The "Add Child" entry SHALL be enabled only when the current selection is a single node (the anchor). With a multi-selection the entry is shown but disabled.

#### Scenario: Add Child under an empty node

- **WHEN** the user picks "Add Child" on a node with no children
- **THEN** a new node named "Node" appears as its first child and becomes the editor's selected node

#### Scenario: Add Child appends to existing siblings

- **WHEN** the user picks "Add Child" on a node that already has children named "Node" and "Node (1)"
- **THEN** a new node named "Node (2)" is appended

#### Scenario: Add Child is disabled with a multi-selection

- **WHEN** the user opens a context menu while three nodes are selected
- **THEN** the "Add Child" entry is rendered disabled
- **AND** picking it has no effect

### Requirement: Delete removes the node from the scene

Selecting "Delete" SHALL detach the selected node(s) from their parent. The "Delete" entry SHALL be enabled whenever the selection is non-empty (single or multi). On a multi-selection, every node in the selection is detached. The editor's selection SHALL be cleared after delete.

The root node SHALL NOT be deletable: the entry is omitted from the root row's menu and any attempt to delete the root SHALL be ignored.

#### Scenario: Delete a non-selected node

- **WHEN** the user deletes a node that is not in the selection
- **THEN** the node is detached from the parent and removed from the visible tree

#### Scenario: Delete the selected node

- **WHEN** the user deletes the currently selected node
- **THEN** the node is detached and the editor's selection is cleared

#### Scenario: Delete a multi-selection

- **WHEN** the user picks "Delete" while three nodes are selected
- **THEN** all three nodes are detached from their parents
- **AND** the editor's selection becomes empty

#### Scenario: Root node is not deletable

- **WHEN** the user attempts to delete the root node
- **THEN** no "Delete" entry is shown and the operation cannot be triggered

### Requirement: Duplicate produces a deep copy including descendants

Selecting "Duplicate" SHALL create a deep clone of the node and all of its descendants (including their components) under the original node's parent. The new node's name SHALL be the original's name plus a unique "(copy)" or "(copy N)" suffix.

The "Duplicate" entry SHALL be enabled only when the current selection is a single node (the anchor). With a multi-selection the entry is shown but disabled.

#### Scenario: Duplicate a leaf node

- **WHEN** the user duplicates a leaf node named "Cube" with a Light component
- **THEN** a sibling named "Cube (copy)" appears under the same parent, also with a Light component

#### Scenario: Duplicate a subtree

- **WHEN** the user duplicates a node with children
- **THEN** the entire subtree (every descendant) is cloned, and each clone carries a copy of its components

#### Scenario: Duplicate is disabled with a multi-selection

- **WHEN** the user opens a context menu while three nodes are selected
- **THEN** the "Duplicate" entry is rendered disabled

### Requirement: Drag-and-drop reparents a node under the drop target

The Scene Inspector SHALL allow a node to be dragged onto another node row. On drop, the dragged node SHALL be detached from its current parent and reparented under the drop target. Dropping onto empty space inside the inspector SHALL reparent under the root.

#### Scenario: Drop onto a node row

- **WHEN** the user drags a node and drops it onto a different node row
- **THEN** the dragged node becomes a child of the drop target

#### Scenario: Drop onto empty space reparents to root

- **WHEN** the user drags a node and drops it on empty space at the end of the inspector
- **THEN** the dragged node is reparented under the root

#### Scenario: Drop onto a descendant is rejected

- **WHEN** the user drags a node and drops it onto one of its own descendants
- **THEN** the drop is rejected and the original hierarchy is unchanged

### Requirement: Hover-to-expand during drag

While a drag is active, the Scene Inspector SHALL automatically expand any collapsed node row that the cursor hovers over for at least 0.5 seconds, so the user can drop onto a deeper descendant without first manually expanding.

#### Scenario: Hover expands a collapsed node

- **WHEN** the user drags a node and hovers over a collapsed row for more than 0.5 seconds
- **THEN** that row expands and the dragged node becomes droppable onto its children

#### Scenario: Hover does not collapse an already-open node

- **WHEN** the user hovers over an already-expanded node during a drag
- **THEN** its open state is preserved

### Requirement: Hover buttons provide discoverable per-row affordances

When the cursor is over a node row, the Scene Inspector SHALL reveal two compact hover buttons: "+" (Add Child) and "..." (open context menu). The "+" button SHALL be disabled when the current selection is a multi-selection; it SHALL always be enabled when the selection is a single node (the row's own node).

#### Scenario: Hover reveals buttons

- **WHEN** the cursor enters a node row
- **THEN** a "+" and "..." button become visible at the right side of the row

#### Scenario: Off-hover hides buttons

- **WHEN** the cursor leaves a node row
- **THEN** the row's hover buttons are not rendered

#### Scenario: Hover "+" is disabled with a multi-selection

- **WHEN** the cursor is over a node row while three nodes are selected
- **THEN** the "+" hover button is rendered disabled
- **AND** the "..." button still opens the context menu
