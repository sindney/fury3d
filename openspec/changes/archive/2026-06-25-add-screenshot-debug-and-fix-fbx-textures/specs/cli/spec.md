## ADDED Requirements

### Requirement: The Lua-launcher path SHALL recognize `--screenshot` and `--screenshot-frame` runtime flags

The non-subcommand path of the `fury` binary (the path that creates a window and runs a Lua script) SHALL recognize two new flags appearing anywhere in `argv` after `argv[1]`:

- `--screenshot <path>`: Capture a PNG of the rendered scene to `<path>` after the configured number of frames, then exit cleanly. See the `screenshot-debug` spec for full behavior.
- `--screenshot-frame <N>`: Configure the frame index at which capture happens. Default `2`.

These are **runtime flags**, not CLI subcommands — they do NOT trigger `Cli::Run` and do NOT cause `Cli::LooksLikeSubcommand` to return `true`. The Lua-launcher path SHALL parse them out of `argv` (along with their values), then pass the remaining argv to the Lua `arg` table as the existing forwarding requirement specifies. From the Lua script's perspective, the flags are invisible.

When `--screenshot` is absent, the launcher behavior is unchanged from the prior spec — the engine runs to completion under user control.

#### Scenario: Screenshot flags do not trigger CLI subcommand routing

- **WHEN** a user runs `./fury Demo.lua --screenshot /tmp/x.png`
- **THEN** `Cli::LooksLikeSubcommand("Demo.lua")` returns `false` (its current behavior — `--screenshot` doesn't change subcommand detection)
- **AND** the engine creates an `sf::Window` and initializes the Lua VM (the Lua launcher path runs)
- **AND** the screenshot is captured per the `screenshot-debug` spec

#### Scenario: Flags are stripped before Lua sees `arg`

- **WHEN** a user runs `./fury Demo.lua tank.fbx --screenshot /tmp/x.png --screenshot-frame 5`
- **THEN** the running Lua script sees `arg[0] == "Demo.lua"`, `arg[1] == "tank.fbx"`, `#arg == 1`
- **AND** the script does NOT see `--screenshot`, `/tmp/x.png`, `--screenshot-frame`, or `5`

#### Scenario: Subcommand path is unaffected

- **WHEN** a user runs `./fury convert fbx in.fbx out.glb --screenshot /tmp/x.png`
- **THEN** the `convert` subcommand handles the `--screenshot` flag according to its own argument-parsing rules (in v1 the subcommands SHALL reject unknown trailing flags with a clear error — `--screenshot` is a runtime flag and is invalid for offline subcommands)
- **AND** no window is created and no screenshot is taken
