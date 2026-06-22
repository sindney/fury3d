## ADDED Requirements

### Requirement: The `FbxConverter` SHALL invoke the vendored `FBX2glTF` binary as a subprocess to produce a glTF file from an FBX input

The engine SHALL include a class `fury::FbxConverter` that wraps the `engine/ThirdParty/FBX2glTF/FBX2glTF-darwin-x64` binary. Given an FBX input path and a target output directory, the converter SHALL invoke the binary, wait for it to complete, capture stdout and stderr, and return either the path to the produced `.glb` (the binary's default output) or an error describing what went wrong. The converter SHALL NOT link the FBX SDK; it only invokes the prebuilt binary via `posix_spawn` / `fork` + `execvp` / equivalent.

#### Scenario: Convert a known-good FBX
- **WHEN** `FbxConverter::Convert("james.fbx", "/tmp/")` is called
- **THEN** the FBX2glTF binary is invoked with arguments equivalent to `--input james.fbx --output /tmp/james`
- **AND** the process waits for completion
- **AND** on success, the returned path points at the produced glTF file (typically `/tmp/james.glb`)
- **AND** the process's exit code is 0

#### Scenario: FBX2glTF exits non-zero
- **WHEN** the FBX2glTF subprocess exits with non-zero status (corrupt input, unsupported feature, etc.)
- **THEN** `FbxConverter::Convert` returns an error
- **AND** the captured stderr is propagated to the caller (logged via `FURYE` in the runtime path; printed to the CLI's stderr in the CLI path)

#### Scenario: Subprocess capturing
- **WHEN** the converter runs
- **THEN** stdout and stderr from the subprocess are captured and made available to the caller (not silently dropped)

### Requirement: The `FbxConverter` SHALL resolve the `FBX2glTF` binary path relative to the running executable

The path to `FBX2glTF-darwin-x64` SHALL be computed from the `fury` executable's own location, not from the current working directory. This makes the converter work whether `fury` is invoked from `examples/bin/`, from an installed location, or from an arbitrary `cwd`. The build system SHALL ensure the FBX2glTF binary is copied next to the `fury` executable at build time so the runtime resolution succeeds.

#### Scenario: Build copies the binary next to fury
- **WHEN** the engine is built with `cmake --build <build-dir> --target fury`
- **THEN** `FBX2glTF-darwin-x64` exists in the same directory as the `fury` binary (or in a stable, documented relative path the converter knows to look in)

#### Scenario: Binary resolution at runtime
- **WHEN** `FbxConverter::Convert` runs from any working directory
- **THEN** the converter locates the FBX2glTF binary via the path to `fury` (e.g. `/proc/self/exe` on Linux, `_NSGetExecutablePath` on macOS) and not via `getcwd()`

#### Scenario: Binary missing
- **WHEN** the FBX2glTF binary is absent (e.g. user copied just the `fury` binary)
- **THEN** `FbxConverter::Convert` returns an error naming the expected path and pointing at `docs/CLI.md` for the install layout

### Requirement: The `FbxConverter` SHALL clean up temporary intermediate files when used in chained conversions

When the CLI's `convert fbx` chains FBX → glTF → scene.json/.bin, the intermediate `.glb` produced by FBX2glTF SHALL be written to a tempdir and deleted on success. On error (the glTF importer fails downstream), the intermediate file SHALL be preserved so a developer can inspect it; the error message SHALL include its path.

#### Scenario: Successful chain deletes intermediate
- **WHEN** `fury convert fbx in.fbx out.json` succeeds
- **THEN** no intermediate `.glb` file remains on disk after the command exits

#### Scenario: Failed chain preserves intermediate
- **WHEN** the chain's glTF-importer step fails after FBX2glTF succeeds
- **THEN** the intermediate `.glb` is preserved
- **AND** the error message names its path

### Requirement: The `FbxConverter` SHALL support macOS, Linux, and Windows via per-platform vendored binaries

The converter SHALL select the correct FBX2glTF binary at compile time based on the host platform and invoke it via the platform's process-spawn API.

The vendored FBX2glTF binaries cover all three platforms:

- `engine/ThirdParty/FBX2glTF/FBX2glTF-darwin-x64` (macOS x86_64; runs on arm64 via Rosetta)
- `engine/ThirdParty/FBX2glTF/FBX2glTF-linux-x64`
- `engine/ThirdParty/FBX2glTF/FBX2glTF-windows-x64.exe`

`FbxConverter::LocateBinary()` SHALL select the platform-appropriate binary at compile time via `#ifdef _WIN32` / `__APPLE__` / `__linux__`. The build's POST_BUILD step SHALL copy only the binary for the host platform next to the `fury` executable.

#### Scenario: macOS x86_64 runs natively
- **WHEN** the converter runs on macOS x86_64
- **THEN** `FBX2glTF-darwin-x64` is invoked directly

#### Scenario: macOS arm64 runs via Rosetta
- **WHEN** the converter runs on macOS arm64 with Rosetta installed
- **THEN** `FBX2glTF-darwin-x64` is invoked via Rosetta
- **AND WHEN** Rosetta is not installed, the converter returns an error suggesting `softwareupdate --install-rosetta`

#### Scenario: Linux x64 runs natively
- **WHEN** the converter runs on Linux x64
- **THEN** `FBX2glTF-linux-x64` is invoked directly

#### Scenario: Windows x64 runs natively
- **WHEN** the converter runs on Windows x64
- **THEN** `FBX2glTF-windows-x64.exe` is invoked via `CreateProcess`

#### Scenario: Unsupported architecture errors cleanly
- **WHEN** the converter runs on an unsupported platform (e.g. Linux arm64)
- **THEN** the converter returns an error naming the missing binary
