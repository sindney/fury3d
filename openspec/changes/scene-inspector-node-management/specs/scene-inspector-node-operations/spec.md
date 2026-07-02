## ADDED Requirements

### Requirement: Context menu on every node row
The Scene Inspector SHALL display a right-click context menu on every visible node row. The menu SHALL offer "Add Child", "Duplicate", "Rename", and "Delete" actions. The root node row SHALL offer only "Add Child".

#### Scenario: Right-click on a non-root node
- **WHEN** the user right-clicks on a non-root node row
- **THEN** a popup appears with "Add Child", "Duplicate", "Rename", and "Delete" entries

#### Scenario: Right-click on the root node
- **WHEN** the user right-clicks on the root node row
- **THEN** a popup appears with only "Add Child"

### Requirement: In-place rename via context menu, double-click, or F2
The Scene Inspector SHALL activate an in-place text input on a node row when the user picks "Rename" from the context menu, double-clicks the row label, or presses F2 while a row is selected.

#### Scenario: Rename commits via Enter
- **WHEN** the user types a new name into the active rename field and presses Enter
- **THEN** the node's name is updated via SetName and the rename field closes

#### Scenario: Rename cancels via Escape
- **WHEN** the user presses Escape while the rename field is active
- **THEN** the rename field closes without modifying the node's name

### Requirement: Add Child creates a child node under the target
Selecting "Add Child" from a row's context menu (or the per-row "+" hover button) SHALL create a new SceneNode and reparent it under the target node. The new node's name SHALL be unique among the target's existing siblings.

#### Scenario: Add Child under an empty node
- **WHEN** the user picks "Add Child" on a node with no children
- **THEN** a new node named "Node" appears as its first child and becomes the editor's selected node

#### Scenario: Add Child appends to existing siblings
- **WHEN** the user picks "Add Child" on a node that already has children named "Node" and "Node (1)"
- **THEN** a new node named "Node (2)" is appended

### Requirement: Delete removes the node from the scene
Selecting "Delete" SHALL detach the node from its parent. If the deleted node is the editor's currently selected node, the editor's selection SHALL be cleared.

#### Scenario: Delete a non-selected node
- **WHEN** the user deletes a node that is not selected
- **THEN** the node is detached from its parent and removed from the visible tree

#### Scenario: Delete the selected node
- **WHEN** the user deletes the currently selected node
- **THEN** the node is detached and the editor's selection is cleared

#### Scenario: Root node is not deletable
- **WHEN** the user attempts to delete the root node
- **THEN** no "Delete" entry is shown and the operation cannot be triggered

### Requirement: Duplicate produces a deep copy including descendants
Selecting "Duplicate" SHALL create a deep clone of the node and all of its descendants (including their components) under the original node's parent. The new node's name SHALL be the original's name plus a unique "(copy)" or "(copy N)" suffix.

#### Scenario: Duplicate a leaf node
- **WHEN** the user duplicates a leaf node named "Cube" with a Light component
- **THEN** a sibling named "Cube (copy)" appears under the same parent, also with a Light component

#### Scenario: Duplicate a subtree
- **WHEN** the user duplicates a node with children
- **THEN** the entire subtree (every descendant) is cloned, and each clone carries a copy of its components

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
When the cursor is over a node row, the Scene Inspector SHALL reveal two compact hover buttons: "+" (Add Child) and "..." (open context menu).

#### Scenario: Hover reveals buttons
- **WHEN** the cursor enters a node row
- **THEN** a "+" and "..." button become visible at the right side of the row

#### Scenario: Off-hover hides buttons
- **WHEN** the cursor leaves a node row
- **THEN** the row's hover buttons are not rendered