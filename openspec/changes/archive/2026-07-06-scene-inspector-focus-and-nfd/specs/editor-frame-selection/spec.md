## ADDED Requirements

### Requirement: The editor SHALL expose a frame-selection handler registration API
The editor SHALL provide a C++ API `Editor::SetFrameSelectionHandler(std::function<void(SceneNode*)>)` that registers a single handler invoked when the editor wants the camera to frame a specific `SceneNode` (e.g., on a leaf double-click in the Scene Inspector). Calling `SetFrameSelectionHandler` with a new handler SHALL replace any previously-registered handler. Calling it with a null `std::function` SHALL clear the handler.

The handler SHALL be invoked on the main thread, during the frame in which the framing request originates (the editor's tick). The handler is responsible for updating whatever state the active camera system reads each frame; the C++ layer SHALL NOT also write to the camera's transform.

#### Scenario: Registering a handler
- **WHEN** a Lua script calls `Editor.SetFrameSelectionHandler(fn)` with a function `fn`
- **THEN** subsequent frame-selection requests invoke `fn` with the target `SceneNode*`

#### Scenario: Replacing a handler
- **WHEN** a Lua script calls `Editor.SetFrameSelectionHandler(fn2)` while a previous handler `fn1` is registered
- **THEN** subsequent frame-selection requests invoke `fn2` (not `fn1`)

#### Scenario: Clearing a handler
- **WHEN** a Lua script calls `Editor.SetFrameSelectionHandler(nil)`
- **THEN** subsequent frame-selection requests are no-ops at the C++ layer

#### Scenario: WITH_EDITOR=OFF stub
- **WHEN** the engine is built with `WITH_EDITOR=OFF`
- **THEN** `Editor::SetFrameSelectionHandler` is a no-op inline stub (compiles cleanly, links to nothing)

### Requirement: The editor SHALL invoke the frame-selection handler on a focus request
The C++ editor SHALL provide an internal `Editor::FrameSelection(SceneNode*)` (or equivalent) entry point that, when called with a non-null node, invokes the currently-registered frame-selection handler with that node. If no handler is registered, `FrameSelection` SHALL be a no-op. If the node is null, `FrameSelection` SHALL be a no-op.

The Scene Inspector's leaf-double-click handler SHALL call `Editor::FrameSelection(node)`.

#### Scenario: FrameSelection invokes the registered handler
- **WHEN** the C++ editor calls `Editor::FrameSelection(node)` while a handler is registered
- **THEN** the registered handler is called with `node` as the argument

#### Scenario: FrameSelection with no handler is a no-op
- **WHEN** the C++ editor calls `Editor::FrameSelection(node)` while no handler is registered
- **THEN** no handler is invoked and no error is emitted

#### Scenario: FrameSelection with null node is a no-op
- **WHEN** the C++ editor calls `Editor::FrameSelection(nullptr)`
- **THEN** no handler is invoked and no error is emitted

### Requirement: The Lua binding SHALL expose `Editor.SetFrameSelectionHandler`
The Lua binding layer SHALL expose `Editor.SetFrameSelectionHandler` accepting either a Lua function (registered as the handler) or `nil` (clears the handler). The binding SHALL wrap the Lua function in a `sol::protected_function` and trap errors, logging them via `FURYE` and continuing (mirroring the existing `SetCommandHandler` / `SetCameraSettings` error-handling pattern).

The Lua handler receives the `SceneNode*` as its sole argument. The handler's return value is ignored.

#### Scenario: Lua registers a frame handler
- **WHEN** `Editor.lua` calls `Editor.SetFrameSelectionHandler(function(node) ... end)`
- **THEN** the function is registered as the frame-selection handler

#### Scenario: Lua handler error does not crash the editor
- **WHEN** the registered Lua handler raises an error when invoked
- **THEN** the error is logged via `FURYE`
- **AND** the editor continues running on subsequent frames

#### Scenario: Lua clears the handler
- **WHEN** `Editor.lua` calls `Editor.SetFrameSelectionHandler(nil)`
- **THEN** the handler is cleared and subsequent `Editor::FrameSelection` calls are no-ops

#### Scenario: WITH_EDITOR=OFF stub
- **WHEN** the engine is built with `WITH_EDITOR=OFF`
- **THEN** `Editor.SetFrameSelectionHandler` is a no-op stub in Lua (accepts and discards any argument)
