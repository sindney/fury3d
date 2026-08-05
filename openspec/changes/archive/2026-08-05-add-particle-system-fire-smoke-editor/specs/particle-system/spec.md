# particle-system

## Purpose

CPU-driven billboard particle system for fury3d — Unity-style `ParticleSystem` +
`ParticleSystemRenderer` split, with module-driven authoring (Emission, Shape,
Velocity, ColorOverLifetime, SizeOverLifetime, RotationOverLifetime,
Renderer). Renders into the existing transparent pass; persists as a project
asset in the scene's `EntityManager` alongside `Mesh` / `Material`.

> **Implementation note (2026-07-31):** `ParticleSystem` landed as an
> **Entity asset** (referenced by name from `ParticleRenderer`), not as a
> `Component` as originally drafted — the asset model gives lifetime,
> serialization, content-browser tiles, and asset-picker rebinding for free.
> Per-frame ticking is each instance's own `Engine::OnUpdate` subscription
> (the `Animator` pattern), not a `Scene::Update` walk.

## ADDED Requirements

### Requirement: ParticleSystem SHALL be a scene EntityManager asset that owns a CPU-side particle pool

`ParticleSystem` SHALL be an `Entity` registered in the active scene's
`EntityManager` (like `Mesh` / `Material` / `AnimationClip`), constructible
via `ParticleSystem::Create()` and referenced **by name** from
`ParticleRenderer` components. Creating a `ParticleSystem` SHALL allocate a
CPU-side pool of particle slots with a per-instance cap defaulting to 4096
(overridable per emitter, bounded by `FURY_PARTICLE_MAX_PER_SYSTEM`). The
system SHALL expose `Emit(int count)` (immediate burst, like
Unity's `ParticleSystem.Emit(int)`), `SetEmissionRate(float perSecond)`,
`IsAlive()` (true while at least one slot is non-empty), and `Update(float dt)`
which advances every live particle's `age` by `dt`, kills particles whose
`age >= lifetime`, and spawns new particles from the configured emission rate
(or `Emit` bursts). Each `ParticleSystem` instance SHALL tick every frame via
its own `Engine::OnUpdate` subscription (subscribe on create, unsubscribe in
the destructor — the `Animator` pattern). While a particle editor window has
the system open, `SetExternallyDriven(true)` SHALL suspend that subscription
tick so the editor preview owns the clock (the preview drives `Update(dt)`
manually); closing the window SHALL resume the tick.

#### Scenario: Emission rate spawns particles each frame

- **WHEN** a `ParticleSystem` with `emissionRate = 60` (particles/second) is
  attached to a `SceneNode` and the scene ticks at 60 fps
- **THEN** exactly one new particle is spawned per frame on average
- **AND** each spawned particle has `age = 0` and a `lifetime` drawn from the
  configured `lifetime` distribution

#### Scenario: Particles die after their lifetime

- **WHEN** a particle is spawned with `lifetime = 2.0` and `age` advances to
  `2.0` via `Update(dt)`
- **THEN** the particle is removed from the live pool on the same frame

#### Scenario: Explicit burst via Emit

- **WHEN** the user calls `particleSystem.Emit(50)` on a system with 0 live
  particles
- **THEN** exactly 50 new particles are spawned on the next `Update` call

#### Scenario: Editor preview suspends the wall-clock tick

- **WHEN** a particle editor window is open on a `ParticleSystem`
- **THEN** the system's `Engine::OnUpdate` tick is suspended (no double
  advance) until the window closes

### Requirement: ParticleSystem SHALL support module-driven authoring

`ParticleSystem` SHALL own a struct-per-module configuration covering the
minimum authoring surface required for fire + smoke:

- `EmissionModule { float rateOverTime; std::vector<Burst> bursts; }`
- `ShapeModule { ShapeType type; Vector3 scale; float radius; float angle; }`
  where `ShapeType ∈ { BOX, SPHERE, CONE }`
- `VelocityModule { Vector3 linear; float speed; bool inheritFromParent; }`
- `ColorOverLifetimeModule { Gradient color; }` (Unity-style gradient: list of
  `(time01, Color)` pairs, linearly interpolated at lookup time)
- `SizeOverLifetimeModule { AnimationCurve size; }` (Unity-style curve: list
  of `(time01, value)` keys, evaluated by linear interpolation; the value is
  the multiplier applied to the initial size)
- `RotationOverLifetimeModule { float angularVelocity; }`
- `RendererModule { std::string materialName; BlendMode blendMode;
  bool receiveShadows = true; }` where `BlendMode ∈ { ALPHA, ADDITIVE }`.
  `receiveShadows` dims particles by the dominant shadow-casting light's
  map (point cube / single-map dir / CSM / spot — see `transparent-rendering`
  shadow spec) with an ambient floor. The pipeline picks ONE dominant
  casting light per renderer (by `intensity / distance²` to the emitter's
  world position), so each emitter focuses on its locally dominant
  shadow source. The flag applies to ALPHA systems only — ADDITIVE systems
  are light-emitting by convention and SHALL never be shadowed.

Each module SHALL have a `Load/Save` pair that round-trips through the scene
serializer so emitters survive save → reload. A default-constructed module
SHALL be safe to use (no null deref on first `Update`).

#### Scenario: Color gradient is sampled by age

- **WHEN** a particle has `age = 0.5` and `lifetime = 1.0` and the
  `ColorOverLifetimeModule.color` is `[(0, white), (1, black)]`
- **THEN** the sampled color is 50% gray

#### Scenario: Size curve multiplier is applied at render

- **WHEN** a particle has initial size `2.0` and the
  `SizeOverLifetimeModule.size` evaluated at `t=0.5` returns `0.3`
- **THEN** the rendered quad is `0.6` units wide

### Requirement: ParticleRenderer SHALL draw the live pool as billboard quads into the transparent pass

`ParticleRenderer` SHALL be a `Component` registered in
`SceneNode::ComponentRegistry` under `"ParticleRenderer"`. It SHALL hold the
**name** of its `ParticleSystem` asset (resolved lazily to a
`std::weak_ptr<ParticleSystem>` via the active scene's `EntityManager` on
first use / attach), a `std::weak_ptr<Material>` (the smoke/fire texture
binding, overridable by the system's `RendererModule.materialName`), and a
dynamic `Mesh` whose vertex buffer is rewritten every frame with one
quad per live particle. The renderer SHALL bind the `ParticleShader`
(engine-side, `ShaderType::PARTICLE`), set blending state per the
`RendererModule.blendMode` (`ALPHA` → `SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA`,
`ADDITIVE` → `ONE`/`ONE`), test depth against the G-buffer, NOT write depth,
and participate in the existing transparent pass's back-to-front sort. The
billboard orientation SHALL be camera-facing with optional `RotationOverLifetime`
spin around the camera-axis (Unity's billboard mode default). Resolving the
system SHALL also expand the owner node's AABB by
`ParticleSystem::GetLocalBounds()` (once) so frustum culling — which tests
the node AABB — never culls particles that drift beyond the spawn point.

#### Scenario: Two overlapping emitters sort back-to-front

- **WHEN** two `ParticleRenderer` instances render in the transparent pass
- **AND** emitter A's center is closer to the camera than emitter B's center
- **THEN** emitter B is drawn first (farther first)
- **AND** emitter A blends over emitter B on screen

#### Scenario: Additive blend mode sums into the framebuffer

- **WHEN** a `ParticleRenderer` has `blendMode = ADDITIVE` and the
  pre-existing pixel value is `(0.3, 0.3, 0.3)`
- **THEN** the output pixel is `(0.3 + srcR, 0.3 + srcG, 0.3 + srcB)` clamped
  to `[0, 1]`

#### Scenario: Alpha blend mode respects source alpha

- **WHEN** a `ParticleRenderer` has `blendMode = ALPHA` and source alpha is
  `0.5`
- **THEN** the output pixel is `0.5 * src + 0.5 * dst`

#### Scenario: Smoke dims in shadow, fire does not

- **WHEN** a shadow-casting light's map has occluders between the light
  and a SmokePlume (ALPHA, receiveShadows=true)
- **THEN** smoke fragments in the occluded region are dimmed to the
  ambient floor (`u_shadow_floor`)
- **AND** a FireEmber (ADDITIVE) in the same region keeps full brightness

#### Scenario: Particle shadow narrows to the emitter's dominant local source

- **WHEN** a `ParticleRenderer` sees 5 casting lights (1 dir + 2 spot + 2
  point) but is closest to only 1 spot
- **THEN** that spot (or the dir, whichever scores higher per
  `intensity / distance²`) populates the single shadow slot, and the
  dimming on the emitter matches its dominant local shadow source

#### Scenario: ParticleRenderer without a ParticleSystem draws nothing

- **WHEN** a `ParticleRenderer`'s system name is empty or resolves to no
  asset in the active scene's `EntityManager`
- **THEN** the renderer is skipped by the transparent pass without warning
  beyond a one-time debug log

### Requirement: Particle serialization SHALL round-trip through scene.json/.bin

`ParticleSystem` assets SHALL persist in a top-level `particleSystems` array
in `scene.json` / `scene.bin` with full module state (same top-level asset
lists as `meshes` / `materials` / `animations`), and each `ParticleRenderer`
component SHALL persist its system-name reference, optional material
override, and blend mode inside its node entry. Loading a scene containing
these entries SHALL reconstruct the runtime state such that
`Update + Draw` reproduces the same visual output as the saved frame. Module
structs SHALL use the existing `Load/Save` member-by-member pattern (the same
pattern `MeshRender::Load` / `Light::Load` use). `Importer.MergeInto` SHALL
transfer `ParticleSystem` assets alongside every other asset kind so imported
emitters are not stranded in the discarded source scene.

#### Scenario: Re-loaded emitter emits the same visual output

- **WHEN** a scene with a `ParticleSystem` + `ParticleRenderer` is saved
- **AND** the saved scene is reloaded in a fresh process
- **THEN** the emitter's `emissionRate`, `lifetime`, modules, and material
  binding match the saved state
- **AND** the first `Update` after reload produces a visually equivalent
  emitter

### Requirement: Per-emitter particle count SHALL be capped with a warning

The engine SHALL cap the live particle count per `ParticleSystem` at 4096 in
v1. If the user-configured emission rate + bursts would exceed the cap,
`Update` SHALL clamp the live pool at 4096 and emit a one-shot `FURYW` warning
identifying the emitter by name. The cap SHALL be a `#define` constant
(`FURY_PARTICLE_MAX_PER_SYSTEM`) so it can be tuned in one place.

#### Scenario: Cap is enforced with a warning

- **WHEN** an emitter's combined rate + bursts would push the live pool to
  5000 particles
- **THEN** the pool is clamped at 4096
- **AND** a `FURYW` log entry names the emitter and reports the cap

### Requirement: Particle emission counts SHALL appear in `fury info` output

`fury info <scene>` SHALL add a `particles:` line reporting the total count of
`ParticleSystem` assets and `ParticleRenderer` components in the scene.
Emitters with zero live particles SHALL still be counted (the user authored
them; they need to know they're there).

#### Scenario: info lists emitter count

- **WHEN** `fury info Resource/Scene/outdoor_water.bin` runs against a scene
  with 2 `ParticleSystem` assets and 2 `ParticleRenderer` components
- **THEN** the output includes `particles: 2 systems, 2 renderers`