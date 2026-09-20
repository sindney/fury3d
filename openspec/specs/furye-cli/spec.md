# furye-cli

## Purpose

The headless `furye-cli` binary built next to `fury` and `furye`: it reuses the existing CLI dispatch and exit-code conventions for scene/asset inspection (`convert`, `info`, `exec`, `help`, `version`), hosts the editor-side tooling subcommands (`kraut`, `render-mesh`), provides the `cook` and `package` pipeline entry points (see `asset-cooking` / `pak-packaging`), and runs editor-side Lua scripts without booting the editor UI.

## Requirements

### Requirement: A `furye-cli` binary SHALL exist for headless editor-side work

The build SHALL produce a third executable `furye-cli` next to `fury` and `furye`. `furye-cli` SHALL run fully headless: no window creation, no GL context, no engine render loop. It SHALL reuse the existing CLI dispatch and exit-code conventions of `fury` (known subcommand in `argv[1]` routes to the CLI handler, `--help`/`-h`/`help` print help and exit 0, unknown input exits 1 with an error).

#### Scenario: Binary builds and runs headless

- **WHEN** `furye-cli --help` runs on a machine with no display
- **THEN** it prints help and exits 0 without opening a window or creating a GL context

#### Scenario: Exit codes match fury conventions

- **WHEN** `furye-cli` runs an unknown subcommand
- **THEN** it exits 1 with an error message, matching `fury`'s CLI behavior

### Requirement: `furye-cli` SHALL support `fury`'s CLI subcommands plus the editor-side tools

`furye-cli` SHALL accept the existing subcommand inventory (`convert`, `info`, `exec`, `help`, `version`) with the same semantics as `fury`, so existing headless workflows (scene conversion, analysis, test scripts) can migrate to the editor-side binary without behavior changes. The editor-side tooling subcommands (`kraut`, `render-mesh`) SHALL live in `furye-cli` only: `fury` keeps scene/asset inspection (`convert`/`info`/`exec`) and play; `furye-cli render-mesh` MAY create a hidden window for its GL context (headless-by-default, windowed on demand).

#### Scenario: Exec works identically

- **WHEN** `furye-cli exec scene.bin tests/lua/some_test.lua` runs
- **THEN** the scene loads, the script executes against the engine bindings, and the exit code matches what `fury exec` would return

### Requirement: `furye-cli` SHALL provide `cook` and `package` subcommands

`furye-cli` SHALL add:

- `cook <scene> [--texture-target legacy|modern] [--ddc <path>]` - runs the cook step per the `asset-cooking` spec and writes the cook manifest.
- `package <scene> [--compression none|lz4] [--output <path>]` - runs cook if no fresh manifest exists, then packs per the `pak-packaging` spec.

Each SHALL accept `--help` and print its own usage. Combined, `furye-cli package scene.bin` SHALL leave a deployable `scene.pak` next to the scene.

#### Scenario: One-shot package

- **WHEN** a user runs `furye-cli package scene.bin` on a never-cooked scene
- **THEN** cook runs first (writing the manifest), then packaging runs
- **AND** `scene.pak` appears next to the scene file

#### Scenario: Cook-only invocation

- **WHEN** a user runs `furye-cli cook scene.bin --texture-target legacy`
- **THEN** the DDC is populated, the manifest is written, and no `.pak` is produced

### Requirement: `furye-cli` SHALL run editor Lua scripts headlessly

`furye-cli` SHALL provide a way to execute editor-side Lua scripts (the scripts that today run inside `furye`) without booting the editor UI, registering the editor binding surface the script needs. This gives the cook/package pipelines and future batch tools (asset analysis, kraut/render-mesh batch runs) a supported scripting entry point.

#### Scenario: Editor script runs without a window

- **WHEN** `furye-cli` runs an editor script that enumerates scene assets
- **THEN** the script executes to completion with no window created and its output appears on stdout
