# cli (delta for add-rhi-layer)

## ADDED Requirements

### Requirement: Engine-boot paths SHALL accept a global `--rhi=<backend>` option

The `fury`/`furye` launcher paths (Lua launcher, `fury exec` scene runs, and `.pak` boot) SHALL accept `--rhi=opengl|vulkan|metal` anywhere in argv and pass it into engine initialization per the `rhi-selection` resolution chain. The option SHALL NOT affect subcommand dispatch classification (a bare `--rhi=...` argv[1] still runs the default Lua launcher). `furye-cli` headless subcommands SHALL ignore the option. The help text and `docs/CLI.md` SHALL document it.

#### Scenario: Launcher picks up the flag

- **WHEN** the engine launches as `./fury game.pak --rhi=vulkan`
- **THEN** the pak boots with the Vulkan backend selected

#### Scenario: Flag alone does not trigger subcommand dispatch

- **WHEN** the engine launches as `./fury --rhi=metal`
- **THEN** the default Lua launcher path runs with the Metal backend selected (no CLI subcommand dispatch)

#### Scenario: Help documents the option

- **WHEN** `./fury help` runs
- **THEN** the help text documents `--rhi` with the supported backend names
