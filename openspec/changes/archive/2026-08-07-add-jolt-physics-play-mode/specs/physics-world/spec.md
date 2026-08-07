# physics-world

## Purpose

JoltPhysics integration: build-system vendoring, the engine-level
`PhysicsWorld` service that owns the Jolt `PhysicsSystem`, the fixed-step
simulation loop with interpolated transform sync, the centimeter unit
convention, and the runtime-vs-editor simulation gate.

## ADDED Requirements

### Requirement: JoltPhysics SHALL be vendored as a submodule and linked into both binaries

`engine/ThirdParty/JoltPhysics` SHALL be a git submodule tracking
`https://github.com/jrouwe/JoltPhysics.git`, pinned to a stable release tag.
`engine/CMakeLists.txt` SHALL fail configure with a clear
`git submodule update --init --recursive` message when the submodule is
absent, SHALL build Jolt as a static library via its own
`Build/CMakeLists.txt` with all consumer targets (tests, samples, viewers)
disabled, and SHALL link it into both `fury` and `furye`. Jolt headers SHALL
be on a SYSTEM include path so vendored-header warnings do not pollute the
engine's `-Wall` build.

#### Scenario: Fresh clone configures and builds

- **WHEN** a developer runs `git submodule update --init --recursive` and configures the project
- **THEN** both `fury` and `furye` build with Jolt linked
- **AND** no Jolt test/sample/viewer targets are produced

#### Scenario: Missing submodule fails with actionable message

- **WHEN** `engine/ThirdParty/JoltPhysics/Jolt/Jolt.h` is absent at configure time
- **THEN** CMake stops with a message naming the missing submodule and the `git submodule update --init --recursive` remedy

### Requirement: The engine SHALL own a PhysicsWorld service with engine-managed lifetime

A `PhysicsWorld` singleton SHALL be created during `Engine::Initialize` and
destroyed during `Engine::Shutdown`, bracketing Jolt's
`RegisterTypes`/`UnregisterTypes`. It SHALL own the Jolt `PhysicsSystem`, a
temp allocator, and a job system, with a two-layer broadphase (non-moving /
moving) sufficient for static geometry, dynamic bodies, and one character.

#### Scenario: Engine startup creates the physics world

- **WHEN** `fury` starts with any scene
- **THEN** a usable `PhysicsWorld` instance exists after `Engine::Initialize` returns
- **AND** shutdown tears it down without leaks or Jolt assert failures

### Requirement: Simulation SHALL step on the engine fixed tick and interpolate node transforms per frame

When simulation is enabled, `PhysicsWorld` SHALL advance the Jolt world on
`Engine::OnFixedUpdate` (25 Hz, `Engine::GetFixedDt()`) using at least 2
collision substeps per tick, and SHALL write interpolated body transforms to
owning `SceneNode`s on `Engine::OnUpdate` using `Engine::GetFixedTickAlpha()`.
Dynamic-body write-back SHALL go through parent-world-matrix inverse +
`MathUtil::Decompose`; it SHALL NOT compose local TRS from piecewise
`GetWorld*` getters.

#### Scenario: Dynamic body falls under gravity and node follows smoothly

- **WHEN** a scene contains a dynamic BodySetup on a node suspended above a static floor
- **AND** the scene runs in `fury`
- **THEN** the node accelerates downward and comes to rest on the floor
- **AND** its rendered motion is smooth (interpolated between fixed ticks, not 25 Hz stair-steps)

#### Scenario: Body under a scaled ancestor lands at the correct world spot

- **WHEN** a dynamic body is attached to a node nested under the ×100 `RootNode`
- **THEN** the node's world transform after settling matches the Jolt body transform within tolerance
- **AND** its local scale is unchanged from its authored value

### Requirement: Physics SHALL use engine world units (1 unit = 1 cm) with scaled constants

The Jolt world SHALL operate directly in engine world units (centimeters).
The default gravity SHALL be `(0, -981, 0)` units/s². Unit-dependent
constants (penetration slop, character dimensions, speeds) SHALL be authored
in centimeters. A scene MAY carry an optional `"physics"` settings block
(sibling of `renderSettings`) overriding gravity; absent the block, defaults
apply.

#### Scenario: Scene without physics block uses centimeter defaults

- **WHEN** a scene file has no `"physics"` block
- **THEN** the world runs with gravity `(0, -981, 0)` and centimeter-scaled tolerances

#### Scenario: Scene physics block overrides gravity

- **WHEN** a scene's `"physics"` block sets gravity to `(0, -490, 0)`
- **THEN** dynamic bodies fall at half the default acceleration

### Requirement: Simulation SHALL run in the fury runtime and never in the editor process

`PhysicsWorld` SHALL expose `SetSimulationEnabled(bool)` / `IsSimulationEnabled()`.
The flag SHALL default to enabled in `fury` and disabled in `furye` (set via
the existing `WITH_EDITOR` build split). While disabled, no bodies are
created and no stepping occurs. Enabling simulation after a scene is loaded
SHALL build bodies for all existing BodySetup components. A Lua binding
`Physics.SetEnabled(bool)` SHALL expose the toggle to scripts.

#### Scenario: Editor loads a physics scene without simulating

- **WHEN** `furye` opens a scene containing BodySetup components and dynamic bodies
- **THEN** no Jolt bodies are created and no node transforms change over time

#### Scenario: Played scene simulates in the runtime

- **WHEN** the same scene is launched via `fury Player.lua <scene>`
- **THEN** bodies are created and dynamic bodies move

#### Scenario: Headless script can opt into physics

- **WHEN** a `fury exec` script calls `Physics.SetEnabled(true)` and steps the world
- **THEN** bodies simulate without a window
