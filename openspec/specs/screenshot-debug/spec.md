# screenshot-debug

## Purpose

A C++-side capture path on the `fury` Lua launcher that lets any Lua script be run, rendered for N frames, and captured to a PNG without modification to the script. Provides `--screenshot <path>` and `--screenshot-frame <N>` flags for debugging, regression imagery, and AI-agent-driven verification of rendered output. Implemented in `engine/Fury/Engine.cpp` / `examples/main.cpp` using the vendored `stb_image_write` — no Lua-side coupling, no new third-party dependency.

## Requirements

### Requirement: The `fury` runtime SHALL accept a `--screenshot <path>` flag that captures the rendered output of the running Lua script

The `fury` Lua launcher (the path that runs when `argv[1]` is not a CLI subcommand) SHALL recognize a `--screenshot <path>` flag anywhere in `argv` (after `argv[1]`). When set, after the engine's main loop has rendered N frames (N defaults to 2; see `--screenshot-frame`), the engine SHALL read the back-buffer via `glReadPixels`, write a PNG to `<path>` using the engine's vendored `stb_image_write`, and exit cleanly with code 0. The capture SHALL happen *after* `Pipeline::Execute` and *after* the GUI overlay for the chosen frame, so the captured image matches what the user would see on screen for that frame. The `--screenshot` flag and its value SHALL be stripped from the `arg` table forwarded to Lua, so user scripts do not see them as user-supplied arguments.

If the file write fails, the engine SHALL log the error via `FURYE`, exit with code 1, and SHALL NOT crash the process. The flag is independent of windowing — the same `sf::Window` is created (a window must exist for the GL context), but the loop is short-circuited so the user sees at most a brief window flash before the process exits.

#### Scenario: Screenshot captures the rendered Demo.lua scene

- **WHEN** a user runs `./fury Demo.lua --screenshot /tmp/demo.png`
- **THEN** an SFML window opens briefly
- **AND** after 2 rendered frames the engine writes a PNG to `/tmp/demo.png`
- **AND** the process exits with code 0
- **AND** the PNG's pixel content matches the back-buffer of the rendered scene (non-zero pixel data, not pure black or pure white unless the scene legitimately is)

#### Scenario: Screenshot works for any Lua script the user passes

- **WHEN** a user runs `./fury MyEditor.lua --screenshot /tmp/editor.png`
- **THEN** `MyEditor.lua` runs through `on_init` and at least 2 `on_update` ticks
- **AND** the captured PNG reflects what `MyEditor.lua`'s pipeline rendered (not Demo.lua's)

#### Scenario: Screenshot flag is stripped from Lua's `arg` table

- **WHEN** a user runs `./fury Demo.lua tank.fbx --screenshot /tmp/x.png extra`
- **THEN** the running Lua script sees `arg[1] == "tank.fbx"`, `arg[2] == "extra"`, `#arg == 2`
- **AND** the script does NOT see `--screenshot` or `/tmp/x.png` in `arg`

#### Scenario: Screenshot write failure exits 1, no crash

- **WHEN** a user runs `./fury Demo.lua --screenshot /no/such/dir/x.png`
- **THEN** the engine logs an error naming the path and the strerror reason
- **AND** the process exits with code 1
- **AND** no segfault, no abort, no terminate

### Requirement: The `--screenshot-frame <N>` flag SHALL choose which rendered frame is captured

A `--screenshot-frame <N>` flag SHALL configure the frame index (1-based) at which capture happens. The default is `2` so that one full update tick has passed (Lua scripts that build state in `on_init` plus the first `on_update` are reflected). `N == 1` captures after the first rendered frame. Values `<= 0` SHALL be rejected with a user error (exit code 1, stderr message). Values larger than 1000 SHALL be clamped to 1000 with a one-line warning (this is a debug feature, not a profiling tool).

#### Scenario: Default frame index is 2

- **WHEN** a user runs `./fury Demo.lua --screenshot /tmp/x.png` with no `--screenshot-frame`
- **THEN** the capture happens at the end of frame 2's render
- **AND** the engine logs a one-line info message naming the frame index used

#### Scenario: Custom frame index is honored

- **WHEN** a user runs `./fury Demo.lua --screenshot /tmp/x.png --screenshot-frame 30`
- **THEN** 30 frames are rendered before the capture
- **AND** the captured image reflects state after 29 `on_update` ticks
- **AND** the process exits 0

#### Scenario: Non-positive frame index is rejected

- **WHEN** a user runs `./fury Demo.lua --screenshot /tmp/x.png --screenshot-frame 0` or `... --screenshot-frame -5`
- **THEN** the process exits with code 1
- **AND** stderr contains a message naming `--screenshot-frame` and stating the value must be positive

### Requirement: The screenshot pipeline SHALL be implemented in C++ in the engine, not in Lua

The screenshot capture logic — flag parsing, frame counting, `glReadPixels`, vertical flip, PNG encoding, and exit — SHALL live in `engine/Fury/Engine.cpp` and `examples/main.cpp`. No Lua-side code SHALL be required for a script to be captureable. This means the capture works for any script, including ones the engine team has not seen, and survives changes to Lua scripts without requiring those scripts to be modified.

The capture path SHALL use the existing vendored `stb_image_write.h` (already included in `engine/ThirdParty/STB/`); no new third-party dependency SHALL be added. PNG output SHALL be 8-bit RGBA, with a vertical flip applied (because OpenGL's read origin is bottom-left, PNG's is top-left).

#### Scenario: Capture works without modifying the script

- **WHEN** a user copies `Demo.lua` to `Demo.copy.lua`, makes no other changes, and runs `./fury Demo.copy.lua --screenshot /tmp/x.png`
- **THEN** the capture works identically to running it on the original `Demo.lua`

#### Scenario: PNG orientation matches the on-screen image

- **WHEN** a script renders a recognizable shape (e.g. the tank viewed from above with a known light direction)
- **AND** a screenshot is captured
- **THEN** the PNG's "up" matches the on-screen "up" (no upside-down flip)

#### Scenario: No new third-party dependency

- **WHEN** the change is implemented
- **THEN** `engine/CMakeLists.txt` and `examples/CMakeLists.txt` reference no PNG library not already in the tree
- **AND** the only image-write call site uses `stbi_write_png` from `engine/ThirdParty/STB/`

### Requirement: The screenshot mode SHALL be documented in `docs/CLI.md`

`docs/CLI.md` SHALL gain a section describing the `--screenshot` and `--screenshot-frame` flags: their syntax, default values, exit codes, the headless-vs-windowed caveat (a window opens briefly because SFML/OpenGL needs one), and a one-line example invocation. The section SHALL also note that flags are stripped from `arg` so user scripts can be run identically with or without capture.

#### Scenario: docs/CLI.md describes the screenshot flags

- **WHEN** the repository is inspected
- **THEN** `docs/CLI.md` contains a section titled (case-insensitive) "screenshot" or similar
- **AND** that section names both flags, lists the default frame index (`2`), and gives at least one full example command line
