# compute-shaders

## Purpose

Opportunistic GL 4.3+ compute-shader support on the GL 3.3-baseline engine: highest-core-profile context negotiation, runtime capability detection, a user-controllable global switch, and a compute stage in the single-file shader convention. The infra is kept in place for any future compute writer; currently no in-tree consumer ships (the ocean's GPU wave generation was retired in favour of the offline baker - change `remove-gpu-ocean-generation`). Every call site continues to gate on `HasEffectiveCompute()` so macOS (GL 4.1) and headless runs always take the fallback path.
## Requirements
### Requirement: GL context creation SHALL negotiate the highest available core profile

The app bootstrap SHALL attempt core-profile context creation at descending
versions (4.6, 4.5, 4.3, 4.1, 3.3) in `examples/main.cpp` (runtime and
editor entry points) instead of pinning 3.3, and SHALL record and log the
negotiated version. The GL 3.3 attempt remains as the guaranteed fallback,
so existing rendering on old drivers is unchanged.

#### Scenario: macOS gets 4.1 and keeps working

- **WHEN** the app starts on a platform capped at GL 4.1 core (all macOS)
- **THEN** a 4.1 (or 3.3) context is created, the app runs normally, and the compute capability reports unavailable

#### Scenario: Capable driver gets compute-capable context

- **WHEN** the app starts on a driver supporting GL 4.3+ core
- **THEN** the negotiated context is 4.3 or higher and the compute capability reports available

### Requirement: The engine SHALL detect compute shader usability at runtime

A capability query (for example `GLLoader::HasComputeShaders()`) SHALL return
true only when the negotiated context is GL 4.3+ OR both
`ARB_compute_shader` and `ARB_shader_image_load_store` extensions are
present, AND the compute entry points (`glDispatchCompute`,
`glMemoryBarrier`, `glBindImageTexture`) resolved to non-null pointers. With
no GL context (headless exec) the query SHALL return false. A second query,
`HasEffectiveCompute()`, SHALL combine the capability with the user setting
and environment override.

#### Scenario: Headless exec never reports compute

- **WHEN** any `fury exec` headless run queries the capability
- **THEN** it returns false and no compute code path executes

### Requirement: The Shader system SHALL compile and dispatch compute shaders

`Shader::Compile` SHALL support a `COMPUTE_SHADER` define section in the
existing single-file convention, injecting `#version 430 core` for that
stage, compiling `GL_COMPUTE_SHADER`, and linking single-stage programs.
Compile or link failure SHALL log the shader name and info log and leave
the shader invalid - never crash. `Shader` SHALL expose
`DispatchCompute(groupsX, groupsY, groupsZ)` and a memory-barrier helper.
`Texture` no longer exposes a compute-image binding helper (it was only
used by the removed ocean generator) - the GL entry points for it remain
loaded for future use.

#### Scenario: Incapable machine skips compute compilation cleanly

- **WHEN** a compute shader would be compiled while `HasEffectiveCompute()` is false
- **THEN** compilation is skipped (or fails with a clear logged error) and the caller takes its fallback path

### Requirement: Compute usage SHALL be user-controllable

A global compute-shaders enable setting SHALL default to on, persist via
the editor settings registry (`Settings.*` in imgui.ini, following the
quality-of-life settings convention), and be overridable by the environment
variable `FURY_COMPUTE_SHADER` (`0`/`1`) for runtime and test determinism.
`HasEffectiveCompute()` SHALL be false whenever the setting or env disables
compute, even on capable drivers. The setting is currently dormant in
shipped code (no consumer); it is kept so future compute writers plug in
without re-adding the toggle machinery.

#### Scenario: User disables compute on a capable driver

- **WHEN** the persisted setting or `FURY_COMPUTE_SHADER=0` disables compute on a GL 4.6 machine
- **THEN** `HasEffectiveCompute()` returns false and any future compute consumer takes its fallback path

#### Scenario: Env override wins for tests

- **WHEN** `FURY_COMPUTE_SHADER=0` is set with the setting left on
- **THEN** `HasEffectiveCompute()` returns false for that run

