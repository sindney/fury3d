## MODIFIED Requirements

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

## ADDED Requirements

### Requirement: Double-click on a parent row toggles its expand/collapse state and frames the camera
When the user double-clicks a node row whose node has one or more children, the Scene Inspector SHALL toggle that row's open/collapsed state (expand if collapsed, collapse if expanded) AND request the editor camera to frame that node. The expand/collapse SHALL be provided by `ImGuiTreeNodeFlags_OpenOnDoubleClick` on the row's `TreeNodeEx` flags; the frame SHALL go through the registered frame-selection handler (the C++ layer does NOT compute the camera transform directly). Both effects happen on the same double-click.

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
