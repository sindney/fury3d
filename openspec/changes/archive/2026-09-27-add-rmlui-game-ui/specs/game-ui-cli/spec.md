# game-ui-cli

## Purpose

The `furye-cli gui` subcommand family: a headless, machine-parseable loop for building and testing game UI without launching the editor — dump the element tree, inspect one element's layout and computed style, dispatch synthetic events, and capture screenshots. This is the agent-facing tooling for UI iteration, following the `kraut` / `render-mesh` subcommand precedent (hidden SFML window for GL, exit codes 0/1/2, listed in help).

## ADDED Requirements

### Requirement: gui subcommands SHALL run headless in furye-cli with a hidden GL window

The `furye-cli gui` subcommands SHALL load a scene (same dispatch as `exec`: `.json`, `.bin`, `.gltf`, `.glb`, `.fbx`, `.pak`), load the named RML document, and execute against a hidden SFML window's GL context — never opening a visible window, never launching the editor, never entering the interactive Lua launcher. Each subcommand SHALL exit 0 on success, 1 on user error (bad args, scene/document/element not found), 2 on internal error, and appear in `furye-cli help`.

#### Scenario: Help lists the gui family

- **WHEN** the user runs `furye-cli help`
- **THEN** the help text lists the `gui` subcommands with one-line descriptions

#### Scenario: Document not found

- **WHEN** the user runs `furye-cli gui tree scene.json ui/missing.rml`
- **THEN** the command prints an error naming the missing document to stderr
- **AND** exits with code 1

### Requirement: `gui tree` SHALL print the document element tree as JSON

`furye-cli gui tree <scene> <doc.rml> [--frame N]` SHALL advance N frames (default 1) after loading, then print the document's element tree to stdout as JSON: for each element its tag, id, classes, visibility, and client rect, nested by parent/child. Output SHALL be parseable by `jq` and stable across runs for the same inputs.

#### Scenario: Tree of a HUD document

- **WHEN** the user runs `furye-cli gui tree scene.json ui/hud.rml`
- **THEN** stdout is valid JSON whose root corresponds to the document root
- **AND** every element in the document appears with tag, id, classes, and rect
- **AND** the command exits 0

### Requirement: `gui inspect` SHALL print one element's layout and computed style as JSON

`furye-cli gui inspect <scene> <doc.rml> <selector>` SHALL resolve the selector (`#id`, `.class`, or tag name; first match), then print JSON for that element: tag/id/classes, border/padding/content box metrics, computed style properties, and attributes. No match SHALL exit 1 with a message naming the selector.

#### Scenario: Inspect a button by id

- **WHEN** the user runs `furye-cli gui inspect scene.json ui/menu.rml '#play-button'`
- **THEN** stdout is valid JSON containing the element's boxes and computed styles (e.g. width, height, font-size, color)

#### Scenario: Selector matches nothing

- **WHEN** the selector matches no element
- **THEN** the command prints an error naming the selector and exits 1

### Requirement: `gui event` SHALL dispatch a synthetic event and report resulting state

`furye-cli gui event <scene> <doc.rml> <selector> <event> [--param k=v]...` SHALL resolve the element, dispatch the named event (e.g. `click`, `change`, `mouseover`) with optional parameters, advance one frame, then print as JSON the resulting relevant state (for example the element tree, or the values a data-model listener changed), so event handlers wired in RML/Lua are verifiable without a display.

#### Scenario: Click updates a label through a Lua handler

- **WHEN** a document wires `onclick` to a handler that increments a counter label
- **AND** the user runs `furye-cli gui event scene.json ui/menu.rml '#btn' click`
- **THEN** the printed state shows the incremented label text
- **AND** the command exits 0

### Requirement: `gui shot` SHALL render the UI and write a PNG screenshot

`furye-cli gui shot <scene> <doc.rml> <out.png> [--size WxH] [--frame N] [--script init.lua]` SHALL create the hidden window at the requested size (default 1280x720), optionally run an init Lua script (to set wave time, game state, or data models — the frozen-headless-frame lesson), advance N frames (default 30), capture the frame including the UI overlay, and write it to `<out.png>`.

#### Scenario: Screenshot of a menu

- **WHEN** the user runs `furye-cli gui shot scene.json ui/menu.rml /tmp/menu.png --frame 30`
- **THEN** `/tmp/menu.png` exists, has the requested dimensions, and shows the menu over the rendered scene
- **AND** the command exits 0

#### Scenario: Scripted state before capture

- **WHEN** the user passes `--script setup.lua` that sets a data model value
- **THEN** the captured PNG reflects that UI state
