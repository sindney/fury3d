# game-ui-lua Specification

## Purpose
TBD - created by archiving change add-rmlui-game-ui. Update Purpose after archive.
## Requirements
### Requirement: The RmlUi Lua plugin SHALL initialise into the engine's existing lua_State

The engine SHALL call `Rml::Lua::Initialise` with the engine's own `lua_State` (the same state sol2 bindings register into) during script-engine setup, in both windowed and headless `exec` modes. The `rmlui` global SHALL be available to every game script, and RmlUi's raw C-API bindings SHALL coexist with the engine's sol2 bindings in that state.

#### Scenario: rmlui global available in a game script

- **WHEN** a scene Lua script runs in the player or under `fury exec`
- **THEN** the `rmlui` global is defined and its context/document APIs are callable

#### Scenario: Inline RML handler calls a game-script function

- **WHEN** a loaded RML document has `onclick="OnPlayClicked()"` and the game script defines `OnPlayClicked`
- **THEN** clicking the element invokes the Lua function in the engine state
- **AND** that function can use engine bindings (e.g. manipulate scene nodes) in the same call

### Requirement: A GameUI Lua table SHALL expose document lifecycle and capture state

`LuaBindings.cpp` SHALL register a `GameUI` table with document lifecycle functions (load by content path, show/hide/toggle by document handle or id, close, query loaded documents) and the capture queries `WantCaptureMouse()`/`WantCaptureKeyboard()`. The table SHALL be registered identically in windowed and headless-exec script contexts.

#### Scenario: Show and hide a document from Lua

- **WHEN** a script calls `GameUI.LoadDocument("ui/menu.rml")`, then `GameUI.Show(doc)` / `GameUI.Hide(doc)`
- **THEN** the document's visibility changes accordingly, observable via the element tree

#### Scenario: Capture gating from Lua

- **WHEN** the pointer is over a visible UI panel
- **THEN** `GameUI.WantCaptureMouse()` returns true in Lua
- **AND** the game script uses it to suppress camera/world input

### Requirement: Lua data models SHALL drive live UI state

Game scripts SHALL be able to create RmlUi data models from Lua (via the plugin's data-model API) binding Lua tables/values to RML `data-*` views, and updates to the model SHALL reflect in the document without reloading it.

#### Scenario: Health bar driven by a data model

- **WHEN** a script binds a data model with `health = 0.5` and the document binds a progress element to `health`
- **AND** the script later sets the model value to `0.25`
- **THEN** the element's rendered/attribute state reflects the new value on the next update

### Requirement: Lua event listeners SHALL attach to elements

Game scripts SHALL be able to attach event listeners to elements from Lua (plugin `AddEventListener`) with a named or anonymous Lua function, covering at least click, change, and submit-style events on form controls.

#### Scenario: Button click mutates the scene

- **WHEN** a script attaches a click listener to a button and the listener moves a scene node
- **AND** a synthetic click is dispatched to the button
- **THEN** the scene node's transform changes

### Requirement: UI Lua APIs SHALL be testable headlessly

The GameUI table and `rmlui` plugin SHALL function under `fury exec` (no GL) for document load, tree queries, data models, and event dispatch, so `tests/lua` scripts can assert UI behavior with the standard `os.exit(code)` pattern.

#### Scenario: Headless UI test

- **WHEN** `fury exec <scene> tests/lua/game_ui.lua` runs
- **THEN** the script loads a document, asserts tree structure and a data-model update
- **AND** exits with code 0

