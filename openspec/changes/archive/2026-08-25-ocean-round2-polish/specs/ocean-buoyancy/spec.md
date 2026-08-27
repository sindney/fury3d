## ADDED Requirements

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
