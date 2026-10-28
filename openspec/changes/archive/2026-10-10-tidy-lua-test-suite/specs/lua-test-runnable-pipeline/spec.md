# lua-test-runnable-pipeline

## Purpose

Conditions under which the cook / package / pak pipeline test (`tests/pak_pipeline.sh`) is runnable from a fresh checkout. Defines the `furye-cli` build precondition and the skip-with-reason behavior when the precondition is not met.

## ADDED Requirements

### Requirement: `tests/pak_pipeline.sh` SHALL be runnable when `examples/furye-cli` is built locally

The shell script `tests/pak_pipeline.sh` SHALL be invokable from the repo root after `examples/furye-cli` has been produced by the build system. The script's `[ -x "$FURYCLI" ]` guard at line 30 SHALL find the batch-relative path and proceed.

#### Scenario: Pak pipeline runs end-to-end
- **WHEN** `examples/furye-cli` is built and `tests/pak_pipeline.sh` is invoked
- **THEN** all 8 stages (`a` through `h`) print `PASS`
- **AND** the script exits 0

### Requirement: `examples/furye-cli` SHALL be built with `FURY_HEADLESS_CLI=1`

The `furye-cli` binary SHALL be built using the same `examples/main.cpp` source as `fury` and `furye`, but compiled with `FURY_HEADLESS_CLI=1` so the launcher path is short-circuited and only `Cli::Run` is reached. The build target is registered alongside the existing `fury` and `furye` targets.

#### Scenario: Build target produces furye-cli
- **WHEN** the build system is invoked with the `furye-cli` target
- **THEN** `examples/furye-cli` (or its platform-equivalent) is produced
- **AND** the binary reports its help string with `cook`, `package`, `exec-script`, `render-mesh`, `gui` listed

### Requirement: The build SHALL NOT fetch dependencies from the network

When the build system is invoked for `furye-cli`, the build SHALL reuse cached / vendored dependencies (the existing `_deps/` and `ThirdParty/` directories). If a dependency is missing from the cache and the build would need to fetch it from the network, the build SHALL fail fast with a clear error rather than hanging on a network call.

#### Scenario: Missing cached dependency halts the build
- **WHEN** the `furye-cli` target is invoked
- **AND** a required dependency is missing from the cache
- **THEN** the build fails with a stderr message naming the missing dependency
- **AND** the failure is propagated to the user without a network fetch attempt

### Requirement: `tests/pak_pipeline.sh` SHALL skip with a clear reason when `furye-cli` is missing

The script SHALL exit non-zero with a clear precondition error when `examples/furye-cli` is not present. The shell script's `[ -x "$FURYCLI" ]` guard exits 1 with the message `pak_pipeline FAIL: $FURY not found (build first)`. This is a precondition failure, not a script failure.

#### Scenario: Missing furye-cli yields a clear precondition error
- **WHEN** `examples/furye-cli` is absent
- **THEN** `tests/pak_pipeline.sh` exits non-zero
- **AND** stderr contains `pak_pipeline FAIL:` and the missing path
- **AND** no partial pipeline state is left behind (the `trap cleanup EXIT` removes `/tmp/paktest-<pid>`)