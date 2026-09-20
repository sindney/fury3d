# kraut-toolchain (delta)

## MODIFIED Requirements

### Requirement: The engine SHALL ship a `fury kraut` CLI subcommand wrapping the toolchain

`furye-cli kraut generate <descriptor.tree> [--seed N] [--out dir]` SHALL invoke KrautCLI as a subprocess (located relative to the executable, FbxConverter pattern), run generation and glb export, and then invoke KrautPreview `--screenshot` to produce a PNG preview per LOD tier when the preview binary is available. `furye-cli kraut import <tree.glb>` SHALL import a Kraut-exported glb into a fury scene fragment (see `kraut-tree-import`). The subcommand SHALL print machine-readable progress/errors and use exit codes consistent with the existing CLI (0 success, 1 user error, 2 internal error). `furye-cli help` SHALL mention the `kraut` subcommand.

The subcommand moves from `fury` to `furye-cli`: the player binary's CLI keeps only scene/asset inspection (`convert`/`info`/`exec`); editor-side tooling lives in the headless editor CLI. `fury kraut` is no longer a subcommand (the launcher treats it as a Lua script path and errors).

#### Scenario: Generate a tree end to end

- **WHEN** the user runs `furye-cli kraut generate palm.tree --seed 7 --out /tmp/palm`
- **THEN** `/tmp/palm` contains the exported `.glb`, its textures, and preview PNG screenshots
- **AND** the exit code is 0

#### Scenario: Missing descriptor is a user error

- **WHEN** the user runs `furye-cli kraut generate nonexistent.tree`
- **THEN** the command prints an error naming the missing file and exits with code 1

#### Scenario: Help mentions kraut

- **WHEN** the user runs `furye-cli help`
- **THEN** the help text includes a one-line description of the `kraut` subcommand

#### Scenario: fury no longer dispatches kraut

- **WHEN** the user runs `fury kraut generate palm.tree`
- **THEN** `Cli::LooksLikeSubcommand("kraut")` returns false for the fury build
- **AND** the launcher path treats `kraut` as a Lua script and errors
