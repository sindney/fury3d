## ADDED Requirements

### Requirement: SceneNode exposes a deep-clone operation
The SceneNode class SHALL expose a method that returns a new SceneNode whose components and descendants are deep copies of the source node's components and descendants.

#### Scenario: CloneTree of a leaf
- **WHEN** `CloneTree("copy")` is called on a leaf node named "Cube" with a Light component
- **THEN** the returned node is named "copy", has a Light component equal to the source's, has no children, and is not yet attached to any parent

#### Scenario: CloneTree of a subtree
- **WHEN** `CloneTree("copy")` is called on a node with two children
- **THEN** the returned node has two cloned children, each carrying its own cloned components, recursively

#### Scenario: CloneTree preserves local transform
- **WHEN** the source node has a non-identity local transform
- **THEN** the cloned node's local transform equals the source's

### Requirement: Engine-level Clone remains leaf-only
The existing `SceneNode::Clone(const std::string&)` method SHALL continue to copy only the node's own components and local transform, NOT its children. New subtree duplication MUST use `CloneTree`.

#### Scenario: Existing Clone callers unaffected
- **WHEN** a caller invokes `Clone("foo")` on a node with children
- **THEN** the returned node has the same components and local transform as the source but no children

### Requirement: CloneTree is reachable from Lua
The Lua bindings for SceneNode SHALL expose `CloneTree` so Lua scripts can deep-clone a subtree.

#### Scenario: Lua deep-clone
- **WHEN** Lua code calls `node:CloneTree("copy")` on a node with descendants
- **THEN** the returned Lua handle refers to a new SceneNode whose children are also Lua-accessible SceneNodes