# play-mode

## Purpose

The editor Play flow: a Play button that persists the active scene to a temp
file (never touching the original), launches the `fury` runtime as a detached
child process on that temp scene, and the `Player.lua` runtime bootstrap that
activates the scene's player controller and render camera. Includes the
editor-only node mechanism that keeps editor artifacts out of saved scenes.

## ADDED Requirements

### Requirement: The editor SHALL provide a Play action that never mutates the original scene

The menu bar SHALL show a Play button (with F5 shortcut), enabled whenever a
scene is loaded. Activating it SHALL serialize the active scene to a temp
file named `.play_<scenename>.tmp.bin` in the scene's own directory
(overwritten each play) and leave the original scene file and in-memory scene
untouched. If the scene has never been saved, the temp file SHALL go to the
system temp directory with a console warning that relative asset paths may not
resolve. The `.play_*.tmp.bin` pattern SHALL be git-ignored, and the editor
SHALL delete stale temp scenes from its previous sessions on startup.

#### Scenario: Play writes a temp copy and leaves the original alone

- **WHEN** the user has unsaved edits in `outdoor_physics.bin` and clicks Play
- **THEN** `.play_outdoor_physics.tmp.bin` appears next to the scene containing those edits
- **AND** `outdoor_physics.bin` on disk is byte-identical before and after

#### Scenario: Play disabled with no scene

- **WHEN** no scene is loaded
- **THEN** the Play button is disabled (or absent) and F5 does nothing

### Requirement: Play SHALL launch fury as a detached child process on the temp scene

The editor SHALL locate the `fury` binary next to the running `furye`
executable and spawn `fury Player.lua <temp scene path>` as a detached
process (no pipe capture, no blocking wait; no zombie on POSIX, handle closed
on Windows). The editor SHALL remain fully interactive during play, and
closing the fury window SHALL end the play session without editor
involvement. If `fury` is missing, the editor SHALL log an actionable error
instead of spawning.

#### Scenario: Play opens a running game window

- **WHEN** the user clicks Play with a saved scene
- **THEN** a fury window opens within a few seconds showing the scene
- **AND** the editor remains responsive

#### Scenario: Closing the game leaves no trace

- **WHEN** the user closes the fury window
- **THEN** the child process exits and no zombie process remains

### Requirement: Nodes flagged editor-only SHALL be excluded from serialization

`SceneNode` SHALL expose an `editorOnly` flag (default false, not itself
serialized as node content — it describes tooling intent). `SceneNode::Save`
SHALL skip editor-only subtrees. `Editor.lua` SHALL flag its editor camera
node editor-only and SHALL remove stale root-level `EditorCamera` nodes on
scene load, so previously leaked nodes are cleaned on next save.

#### Scenario: Play session contains no editor camera node

- **WHEN** a scene is played via the Play button
- **THEN** the temp scene's node tree contains no `EditorCamera` node

#### Scenario: Legacy scenes are cleaned on save

- **WHEN** `outdoor_water.bin` (containing 4 leaked `EditorCamera` nodes) is loaded in the editor and saved
- **THEN** the saved file contains no `EditorCamera` nodes

### Requirement: Player.lua SHALL bootstrap the runtime play session

`examples/Player.lua` SHALL load the scene named by `arg[1]`, find the first
enabled `PlayerController` in the scene tree, and let it activate its bound
camera as the render camera. If no enabled controller exists, it SHALL create
a free-fly controller node framed on the scene's world AABB so every scene is
navigable in play mode. It SHALL NOT create the editor camera or call
editor-only APIs.

#### Scenario: Scene with a character controller plays in third person

- **WHEN** `fury Player.lua outdoor_physics.bin` runs on a scene whose fox node has an enabled CharacterController bound to a camera
- **THEN** the bound camera renders the frame and the controller receives input

#### Scenario: Scene with no controller falls back to free-fly

- **WHEN** `fury Player.lua plain_scene.bin` runs on a scene with no PlayerController
- **THEN** a free-fly controller is spawned overlooking the scene content
- **AND** mouse-drag look and WASD/Shift flying work
