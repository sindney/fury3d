# scene-round-trip (delta)

## ADDED Requirements

### Requirement: SceneNode SHALL support an editor-only flag that serialization skips

`SceneNode` SHALL expose `SetEditorOnly(bool)` / `IsEditorOnly()` (default
false) with a Lua binding. `SceneNode::Save` SHALL omit editor-only subtrees
from the `"nodes"` output, so tooling nodes (the editor camera, future
helpers) never persist to scene files — including the temp scenes written by
play mode. The flag is runtime/editor state: it is NOT itself loaded from
scene files (older files simply have all nodes unflagged).

#### Scenario: Editor camera never reaches a saved file

- **WHEN** the editor camera node is flagged editor-only and the scene is saved (normal save or play-mode temp save)
- **THEN** the saved document's node tree contains no editor-camera node
- **AND** reloading that file yields a tree without it

#### Scenario: Unflagged nodes are unaffected

- **WHEN** a scene with no editor-only nodes is saved and reloaded
- **THEN** the node count and hierarchy round-trip exactly as before this change

#### Scenario: Legacy leaked nodes are cleaned by the editor

- **WHEN** a scene file containing previously leaked `EditorCamera` nodes is loaded into `furye`
- **THEN** the editor removes the stale nodes on load (its fresh editor camera is flagged editor-only)
- **AND** the next save produces a file with zero `EditorCamera` nodes
