## ADDED Requirements

### Requirement: When a viewport pick selects a node, the Scene Inspector SHALL auto-expand the picked node's ancestor chain

On a viewport pick that resolves to a non-null `SceneNode`, the Scene Inspector SHALL make that node visible in the tree by force-opening every collapsed ancestor. The reveal path SHALL:

1. Resolve the picked node.
2. Walk the ancestor chain from the picked node upward (`SceneNode::GetParent()`) until reaching the root.
3. For every ancestor that is currently collapsed in the inspector tree, set its open state to true so the picked node's row will be rendered on the next frame.

The reveal SHALL NOT alter the open/closed state of any sibling subtree. The reveal SHALL NOT scroll the inspector -- the user keeps their current scroll position; only the tree's open/closed state changes. The reveal SHALL NOT move the camera -- picking is strictly a selection + tree-state operation. The existing `Editor::FrameSelection` handler continues to own camera framing (fired by the Scene Inspector's leaf-double-click path).

#### Scenario: Pick a node inside a collapsed subtree

- **WHEN** the user clicks the viewport and the picked node is a child of one or more collapsed parents
- **THEN** every collapsed ancestor on the path from the picked node to the root is force-opened
- **AND** the picked node is selected
- **AND** the camera pose is unchanged
- **AND** the inspector's vertical scroll position is unchanged

#### Scenario: Pick a node whose ancestors are already expanded

- **WHEN** the user picks a node whose ancestors are all expanded
- **THEN** the visible tree is unchanged
- **AND** the picked node becomes the selection
- **AND** the camera pose is unchanged

#### Scenario: Pick misses all nodes

- **WHEN** the user clicks the viewport and the picker resolves to null
- **THEN** no row is force-opened
- **AND** the editor's selection is cleared
- **AND** the camera pose is unchanged

#### Scenario: Pick on a descendant of a selected parent does not collapse the parent

- **WHEN** the user picks a node whose parent is already in the selection and is expanded
- **THEN** the parent remains expanded
- **AND** the selection is updated to the picked node
