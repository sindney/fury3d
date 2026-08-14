# scene-inspector-node-operations

## Purpose

TBD

## Requirements

### Requirement: Context menu on every node row
The Scene Inspector SHALL display a right-click context menu on every visible node row. The menu SHALL offer "Add Child", "Duplicate", "Rename", and "Delete" actions. The root node row SHALL offer only "Add Child".

Entries are gated by the current selection size; see `scene-inspector-multi-select` for the full rules.

#### Scenario: Right-click on a non-root node with one selected node
- **WHEN** the user right-clicks a non-root node while exactly one node is selected
- **THEN** a popup appears with "Add Child", "Duplicate", "Rename", and "Delete" entries, all enabled

#### Scenario: Right-click with a multi-selection
- **WHEN** the user right-clicks while three nodes are selected
- **THEN** "Add Child", "Duplicate", "Rename" are rendered disabled (greyed out)
- **AND** "Delete" is enabled

#### Scenario: Right-click on the root node
- **WHEN** the user right-clicks on the root node row
- **THEN** a popup appears with only "Add Child"

### Requirement: In-place rename via context menu or F2
The Scene Inspector SHALL activate an in-place text input on a node row when the user picks "Rename" from the row's context menu, or presses F2 while the inspector window has focus and a row is selected. Double-clicking a node row SHALL NOT activate rename.

#### Scenario: Rename commits via Enter
- **WHEN** the user types a new name into the active rename field and presses Enter
- **THEN** the node's name is updated via SetName and the rename field closes

#### Scenario: Rename cancels via Escape
- **WHEN** the user presses Escape while the rename field is active
- **THEN** the rename field closes without modifying the node's name

#### Scenario: Double-click does not start rename
- **WHEN** the user double-clicks a non-root node row
- **THEN** no rename field is activated
- **AND** the row's existing name is left unchanged

### Requirement: Double-click on a parent row toggles its expand/collapse state and frames the camera
When the user double-clicks a node row whose node has one or more children, the Scene Inspector SHALL toggle that row's open/collapsed state (expand if collapsed, collapse if expanded) AND request the editor camera to frame that node. The expand/collapse is implemented by flipping `g_OpenedNodes` directly via `Editor::SetNodeOpen` instead of relying on the unreliable `ImGuiTreeNodeFlags_OpenOnDoubleClick` detection; the frame goes through the registered frame-selection handler (the C++ layer does NOT compute the camera transform directly). Both effects happen on the same double-click.

#### Scenario: Double-click expands a collapsed parent and frames it
- **WHEN** the user double-clicks a node row that has children and is currently collapsed
- **THEN** the row expands and its children become visible
- **AND** the camera is repositioned to frame the node

#### Scenario: Double-click collapses an expanded parent and frames it
- **WHEN** the user double-clicks a node row that has children and is currently expanded
- **THEN** the row collapses and its children become hidden
- **AND** the camera is repositioned to frame the node

#### Scenario: Double-click on the root node also toggles
- **WHEN** the user double-clicks the root row
- **THEN** the root's open/collapsed state toggles
- **AND** the frame-selection handler is not invoked

### Requirement: Double-click on a leaf row frames the node in the viewport
When the user double-clicks a node row whose node has zero children (a leaf), the Scene Inspector SHALL request the editor camera to frame that node by invoking the registered frame-selection handler with the node as the argument. The C++ layer SHALL NOT compute the camera transform directly; it SHALL defer to the registered handler. Leaves cannot expand (zero children), so the frame is the only effect.

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
- **THEN** the node becomes the editor's selected node (existing selection behavior preserved)
- **AND** no camera motion occurs (only double-click triggers framing)

### Requirement: Root node double-click toggles its expand/collapse state and never frames
Because the root node cannot be deleted, renamed, or reparented, double-clicking the root row SHALL only toggle its expand/collapse state (per the parent-row behavior above, minus the camera frame). The frame-selection handler SHALL NOT be invoked on the root.

#### Scenario: Double-click the root
- **WHEN** the user double-clicks the root row
- **THEN** the root's open/collapsed state toggles
- **AND** the frame-selection handler is not invoked

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
- **THEN** the node is detached from the parent and removed from the visible tree

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
While a drag is active, the Scene Inspector SHALL automatically expand any collapsed node row that the cursor hovers over for at least 0.5 seconds, so the user can drop onto a deeper descendant without first manually expanding. The expansion goes through `Editor::SetNodeOpen` so it integrates with the inspector's tree-state storage.

#### Scenario: Hover expands a collapsed node
- **WHEN** the user drags a node and hovers over a collapsed row for more than 0.5 seconds
- **THEN** that row expands and the dragged node becomes droppable onto its children

#### Scenario: Hover does not collapse an already-open node
- **WHEN** the user hovers over an already-expanded node during a drag
- **THEN** its open state is preserved