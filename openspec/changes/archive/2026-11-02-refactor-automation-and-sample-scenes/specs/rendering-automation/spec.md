## ADDED Requirements

### Requirement: Rendering scenarios SHALL be declared in a maintained test manifest

The rendering suite SHALL describe each scenario with a scene or sample entry point, camera/setup script, render backend, resolution, deterministic simulation state, HDR setting, expected semantic checks, and capture outputs. The initial manifest SHALL include the feature showcase with HDR disabled and enabled and the canonical island scene. The feature-showcase scenarios SHALL include the game UI overlay in at least one captured state.

#### Scenario: Enumerate the core rendering matrix

- **WHEN** the rendering runner lists its built-in scenarios
- **THEN** it includes feature-showcase HDR-off, feature-showcase HDR-on, and island scenarios
- **AND** each entry names all inputs needed to reproduce the run

### Requirement: Rendering captures SHALL use deterministic scenario state

Each automated rendering scenario SHALL fix viewport dimensions, camera transform, simulation/wave time, animation sample time, particle warmup or sample state, and relevant render settings before capture. Dynamic UI text or regions that cannot be made deterministic SHALL be fixed to test values or explicitly masked in comparisons. Capture completion SHALL close through the normal window path after required GPU work and screenshot output finish.

#### Scenario: Same scenario produces comparable captures

- **WHEN** the same scenario is run twice on the same backend and build
- **THEN** both captures have the requested dimensions and camera/state metadata
- **AND** their comparison remains within the scenario's image tolerance

#### Scenario: Animated features are sampled at a known time

- **WHEN** the feature showcase reaches its capture point
- **THEN** skeletal animation, particles, and other animated state are at the manifest's specified sample state
- **AND** capture does not depend on an arbitrary wall-clock frame count

### Requirement: Rendering tests SHALL combine semantic checks with image verification

A scenario SHALL fail when its scene is missing required content, a required render feature is not enabled, shader compilation or rendering reports an error, capture fails, or the resulting image exceeds its approved comparison tolerance. Headless scene checks SHALL verify content and serialized state without requiring a graphics context; windowed rendering checks SHALL verify the rendered output and UI composition.

#### Scenario: Required feature is absent

- **WHEN** a feature-showcase scenario loads without one of its required tanks, lights, particle systems, skeletal meshes, or UI elements
- **THEN** the semantic check fails and identifies the missing content
- **AND** a screenshot alone cannot make the scenario pass

#### Scenario: HDR settings differ as declared

- **WHEN** HDR-off and HDR-on feature-showcase scenarios initialize
- **THEN** each reports the manifest's requested HDR state
- **AND** their captures are associated with separate expected results

### Requirement: Image baselines SHALL be backend-specific and tolerance-based

Approved baselines SHALL be keyed by rendering backend and scenario and SHALL use documented tolerances or masks for accepted rasterization and nondeterminism differences. The runner SHALL NOT compare a capture from one backend against another backend's baseline and SHALL NOT require bit-identical PNG bytes. Baseline creation or replacement SHALL require an explicit update operation and produce a reviewable diff artifact.

#### Scenario: Missing baseline is explicit

- **WHEN** a scenario has no approved baseline for the selected backend
- **THEN** the runner reports that baseline as missing and writes the actual capture for review
- **AND** it does not silently approve the capture

#### Scenario: Changed image produces review artifacts

- **WHEN** an actual capture exceeds its scenario tolerance
- **THEN** the test fails and retains the actual image, expected image, and difference image or metric

### Requirement: Existing render harnesses SHALL be consolidated without losing unique coverage

The maintained rendering runner SHALL replace redundant scene-specific shell and Lua scaffolding where its manifest and shared scenario helpers provide the same coverage. Existing visual checks SHALL be inventoried before removal; a harness with unique coverage SHALL be migrated, retained as a named scenario, or explicitly excluded with a documented reason. All wrappers and documentation SHALL refer to the new scenario names after consolidation.

#### Scenario: Legacy wrapper is migrated

- **WHEN** an existing render wrapper's checks are represented by manifest scenarios
- **THEN** the wrapper invokes the shared runner or is removed with its references updated
- **AND** no documentation invokes the deleted script path

#### Scenario: Unique check remains covered

- **WHEN** an existing harness verifies behavior not represented in the initial core matrix
- **THEN** that check remains runnable in the suite or in a named retained harness
- **AND** its exclusion from consolidation is documented if it remains separate
