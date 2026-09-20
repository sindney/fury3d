# cli (delta)

## MODIFIED Requirements

### Requirement: The `fury` binary SHALL dispatch CLI subcommands without booting the engine when argv[1] is a known subcommand

The `fury` executable's `main()` SHALL inspect `argv[1]` before any window or engine initialization. When `argv[1]` is one of the reserved subcommand tokens (`convert`, `info`, `exec`, `help`, `--help`, `-h`, `version`, `--version`), `main()` SHALL invoke `fury::Cli::Run(argc, argv)` and return its exit code without creating an `sf::Window`, without calling `fury::Engine::Initialize`, and without running the Lua-launcher path's `sol::state`. The `exec` subcommand SHALL create its own short-lived `sol::state` internally (see the dedicated `fury exec` requirement below) — this is not the launcher's VM, and `Engine.run` is never invoked. When `argv[1]` names an existing file with the `.pak` extension, `main()` SHALL instead boot from the pak: mount it and load its boot scene per the `pak-runtime-loading` spec (this is a windowed/engine boot, not a CLI dispatch). For any other value of `argv[1]` (or when `argc == 1`), the current Lua-launcher path SHALL run unchanged.

#### Scenario: Subcommand bypasses engine init
- **WHEN** a user runs `./fury convert gltf in.gltf out.json`
- **THEN** no SFML window is opened (no window appears on screen)
- **AND** no Lua state is created
- **AND** `fury::Engine::Initialize` is not called
- **AND** the process exits with the code returned by `fury::Cli::Run`

#### Scenario: Default invocation runs Lua launcher
- **WHEN** a user runs `./fury` with no arguments
- **THEN** the current behavior is preserved: SFML window opens, engine initializes, Lua VM loads `Demo.lua`

#### Scenario: Lua script path with no subcommand collision
- **WHEN** a user runs `./fury MyScript.lua`
- **THEN** `MyScript.lua` is loaded as the Lua entry point (current behavior)
- **AND** the CLI dispatch is not triggered

#### Scenario: Pak path boots the packaged game
- **WHEN** a user runs `./fury game.pak` and `game.pak` exists
- **THEN** the pak is mounted and its boot scene loads per the `pak-runtime-loading` spec
- **AND** the file is NOT treated as a Lua script

#### Scenario: Help-equivalents normalized
- **WHEN** a user runs `./fury -h`, `./fury --help`, or `./fury help`
- **THEN** all three produce the same top-level help output and exit code 0

#### Scenario: Exec subcommand routes through Cli::Run
- **WHEN** a user runs `./fury exec scene.json script.lua`
- **THEN** `Cli::LooksLikeSubcommand("exec")` returns `true`
- **AND** `Cli::Run(argc, argv)` is invoked
- **AND** no SFML window is opened
- **AND** `fury::Engine::Initialize` is not called
- **AND** the `exec` handler creates its own short-lived `sol::state` internally (the Lua launcher path is not run)
