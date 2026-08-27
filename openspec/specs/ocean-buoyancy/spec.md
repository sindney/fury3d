# ocean-buoyancy

## Purpose

Jolt buoyancy for the FFT ocean: `BuoyancyComponent` floats dynamic bodies in play mode via per-point submersion forces against the CPU wave sampler, registered pre-step with `PhysicsWorld`, with a line-debug view and graceful degradation (flat water, missing ocean, edit mode).
## Requirements
### Requirement: BuoyancyComponent SHALL be a serializable scene component driving Jolt bodies

`BuoyancyComponent` SHALL be registered in `SceneNode::ComponentRegistry`
(name `BuoyancyComponent`) with: an editable list of float sample points
(node-local offsets, each with a radius/depth scale), water density, linear
and angular water drag, a righting-torque strength, and the name of the
target ocean node. It SHALL operate on the `RigidBodyComponent` of the same
node and SHALL follow the engine's play-mode physics stepping conventions.

#### Scenario: Buoyancy parameters survive scene round-trip

- **WHEN** a scene with a configured buoyant body is saved and reloaded
- **THEN** all float points and coefficients are restored

### Requirement: Floating bodies SHALL settle at the animated waterline

During play mode, each float point SHALL query the CPU wave sampler at its
world (x, z) and the current wave time, compute submersion, and apply the
corresponding buoyant force (plus drag) to the Jolt body at that point, using
the same wave data and time base as rendering so visuals and physics agree.

#### Scenario: Ball bobs and settles

- **WHEN** a light sphere with one central float point is dropped into the ocean in play mode
- **THEN** it splashes (submersion force), bobs with decaying amplitude due to drag, and settles floating at the animated waterline, visually tracking the rendered surface

#### Scenario: Cube levels out via multiple float points

- **WHEN** a cube with four corner float points lands on the water tilted
- **THEN** differential submersion produces torque that rocks it and the righting term levels it flat on the surface

#### Scenario: Physics and visuals stay in sync

- **WHEN** any floating body is observed over a full wave loop
- **THEN** its rendered position tracks the rendered water surface within sampler tolerance (no floating-above-air or sinking-through)

### Requirement: Buoyancy SHALL degrade gracefully

The component SHALL tolerate a missing ocean node or a failed wave asset by
logging one warning per scene load and leaving the body under normal
gravity. Outside play mode the component SHALL apply no forces and perform
no wave queries.

#### Scenario: Missing ocean warns and falls back

- **WHEN** play mode starts with a BuoyancyComponent naming a nonexistent ocean node
- **THEN** one warning is logged and the body falls with normal gravity

#### Scenario: Edit mode is inert

- **WHEN** the scene is edited without entering play mode
- **THEN** no buoyancy forces or wave queries occur

### Requirement: Buoyancy SHALL have editor debug visualization

The component SHALL expose a debug-draw flag that renders its float points as
markers in the editor viewport (via the engine's line-drawing debug
utilities), and, in a play session, colors each marker by its current
submersion. (Play mode is a detached process without editor selection, so the
flag lives on the component, not in editor selection state.)

#### Scenario: Float points visible in play mode

- **WHEN** a floating body's BuoyancyComponent has debug draw enabled and play mode runs
- **THEN** its float points render as markers whose color reflects current submersion depth

### Requirement: Buoyancy angular terms SHALL be integration-stable for any body shape

The buoyancy angular drag SHALL be applied per body-local axis using the
body's REAL inverse inertia read from Jolt, clamped so the per-tick angular
velocity change never exceeds 90% - an explicit-integration factor above 2
diverges (thin long shapes spin up on their own). Coefficients SHALL keep
their meaning for shapes that were already stable (sphere/cube behavior
unchanged within tolerance).

#### Scenario: Plank does not spin up

- **WHEN** a long thin floater (2 m plank) floats for 20 s of simulated time
- **THEN** its angular velocity settles below 0.5 rad/s instead of diverging

#### Scenario: Existing floaters unchanged

- **WHEN** the sphere and cube floaters of the buoyancy smoke test run
- **THEN** settle heights and the cube leveling assertion hold as before

