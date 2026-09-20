# kraut-toolchain

## Purpose

Kraut-CLI vendored as a ThirdParty submodule providing an on-demand, deterministic, headless tree-generation toolchain: `KrautCLI` (generate/export, JSON output) and `KrautPreview` (GL viewer with `--screenshot`), wrapped by a `furye-cli kraut` CLI subcommand following the FbxConverter subprocess precedent. Agents and scripts can go from a `.tree` descriptor + seed to a previewed, engine-importable asset with one command.

## Requirements

### Requirement: Kraut-CLI SHALL be vendored as a submodule and built behind a CMake option

`engine/ThirdParty/Kraut` SHALL be a git submodule pointing at `https://github.com/sindney/Kraut-CLI.git` pinned to a stable commit. `engine/CMakeLists.txt` SHALL provide `option(FURY_WITH_KRAUT "Build KrautCLI + KrautPreview tools" ON)` driving an ExternalProject that builds the KrautCLI and KrautPreview targets only (editor disabled, no Qt required). A missing-submodule guard SHALL emit a clear FATAL_ERROR with fetch instructions when the option is ON and the submodule is absent. KrautPreview MAY be gated by a sub-option `FURY_WITH_KRAUT_PREVIEW` (default ON) for machines lacking SDL2.

#### Scenario: Default configure builds the tools

- **WHEN** the engine is configured with default options and the Kraut submodule is present
- **THEN** the build produces `KrautCLI` and `KrautPreview` binaries copied next to the `fury`/`furye` executables

#### Scenario: Submodule missing

- **WHEN** the engine is configured with `FURY_WITH_KRAUT=ON` and the submodule has not been fetched
- **THEN** configuration fails with a FATAL_ERROR naming the missing submodule and the fetch command

#### Scenario: Preview disabled

- **WHEN** configured with `-DFURY_WITH_KRAUT=ON -DFURY_WITH_KRAUT_PREVIEW=OFF`
- **THEN** only `KrautCLI` is built and copied, and the build succeeds without SDL2

### Requirement: Tree generation SHALL be deterministic per descriptor and seed

Given the same `.tree` descriptor file and the same integer seed, `furye-cli kraut generate` SHALL produce identical output geometry.

#### Scenario: Same seed, same tree

- **WHEN** `furye-cli kraut generate oak.tree --seed 42 --out a/` and `furye-cli kraut generate oak.tree --seed 42 --out b/` are run
- **THEN** the exported geometry files in `a/` and `b/` are byte-identical

### Requirement: The engine SHALL ship a `furye-cli kraut` CLI subcommand wrapping the toolchain

`furye-cli kraut generate <descriptor.tree> [--seed N] [--out dir]` SHALL invoke KrautCLI as a subprocess (located relative to the executable, FbxConverter pattern), run generation and glb export, and then invoke KrautPreview `--screenshot` to produce a PNG preview per LOD tier when the preview binary is available. `furye-cli kraut import <tree.glb>` SHALL import a Kraut-exported glb into a fury scene fragment (see `kraut-tree-import`). The subcommand SHALL print machine-readable progress/errors and use exit codes consistent with the existing CLI (0 success, 1 user error, 2 internal error). `furye-cli help` SHALL mention the `kraut` subcommand.

The subcommand moved from `fury` to `furye-cli`: the player binary's CLI keeps only scene/asset inspection (`convert`/`info`/`exec`); editor-side tooling lives in the headless editor CLI. `fury kraut` is no longer a subcommand (the launcher treats it as a Lua script path and errors).

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
