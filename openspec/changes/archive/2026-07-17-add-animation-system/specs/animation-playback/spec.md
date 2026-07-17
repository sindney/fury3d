## ADDED Requirements

### Requirement: Animator component attaches to a SceneNode

The system SHALL provide an `Animator` component (subclass of `Component`) that attaches to a `SceneNode` via `SceneNode::AddComponent`, registered in `SceneNode::ComponentRegistry` so it serializes, deserializes, clones, and appears in the Node Properties inspector alongside `MeshRender` / `Light` / `Camera` / `Transform`. A node SHALL hold at most one `Animator`. The `Animator` SHALL locate its target `Mesh` via the same node's `MeshRender` component (for skinned animation) and SHALL locate target child nodes by name (for node-level animation channels).

#### Scenario: Animator attaches and serializes
- **WHEN** a script calls `node:AddComponent("Animator")` and the scene is saved and reloaded
- **THEN** the reloaded node has an `Animator` component with its clip references and state restored

#### Scenario: Animator requires MeshRender for skinned playback
- **WHEN** an `Animator` is attached to a node whose `MeshRender` references a skinned `Mesh`
- **THEN** the `Animator` resolves joint channels against that mesh's `Joint` tree by channel name

#### Scenario: Duplicate Animator rejected
- **WHEN** `AddComponent("Animator")` is called on a node that already has one
- **THEN** the call is rejected (per the existing one-component-per-type rule in `SceneNode.h:45`) and the original is preserved

### Requirement: Two-phase tick integration

The `Animator` SHALL advance playback using the engine's existing two-phase update: `AdvanceTime(fixedDt)` is invoked on `Engine::OnFixedUpdate` (25 Hz) and computes per-channel old/new TRS pairs on `Joint` and `Transform` targets; `Display(alpha)` is invoked on `Engine::OnUpdate` (per-frame) with `alpha` = accumulated-time / fixed-step, and performs the per-frame TRS interpolation plus full joint-tree rebuild via `Joint::Update(parentMatrix)`. When `animatePhysics` is false, both phases SHALL run on `OnUpdate`.

#### Scenario: Skinned mesh deforms over time
- **WHEN** an `Animator` with a playing skinned clip is ticked by the engine
- **THEN** `Joint::GetFinalMatrix()` for each animated joint reflects the interpolated pose for the current frame, and the GPU shader receives updated `bone_matrices`

#### Scenario: Frame-rate-independent interpolation
- **WHEN** the render frame rate differs from the 25 Hz fixed tick
- **THEN** the rendered pose interpolates between the last fixed tick and the next using `alpha`, producing smooth motion at any frame rate

#### Scenario: animatePhysics toggles tick source
- **WHEN** `Animator.animatePhysics` is true
- **THEN** `AdvanceTime` runs on `OnFixedUpdate` and `Display` on `OnUpdate`
- **WHEN** `Animator.animatePhysics` is false
- **THEN** both `AdvanceTime` and `Display` run on `OnUpdate`

### Requirement: Root joint inherits owning node's world matrix

When propagating the joint tree, the `Animator` SHALL pass `m_SceneNode->GetWorldMatrix()` as the parent matrix of the mesh's root joint (fixing the current `AnimationPlayer.cpp:214` behavior of passing identity). This ensures a skinned mesh rendered anywhere in the scene graph deforms in the correct world space.

#### Scenario: Skinned mesh translated in the scene graph
- **WHEN** a skinned mesh's `SceneNode` is translated/rotated and an animation plays
- **THEN** the deformed vertices follow the node's world transform, not the identity

### Requirement: Skinned and node-level animation targets

The `Animator` SHALL drive two classes of targets from a single `AnimationClip`: (a) **skinned targets** — channels whose name matches a `Joint` in the `Mesh`'s joint tree write the joint's TRS pairs; (b) **node-level targets** — channels whose name matches a descendant `SceneNode` write the `Transform` component on that node, covering glTF `translation`/`rotation`/`scale` channels on non-skinned nodes (e.g. `AnimatedCube`, `Fox` root motion). Channels that match neither SHALL emit a one-time warning and be skipped.

#### Scenario: AnimatedCube rotates
- **WHEN** the `AnimatedCube` glTF sample is loaded and its clip is played
- **THEN** the cube `SceneNode`'s rotation matches the clip's rotation channel at the current time, applied through its `Transform` component

#### Scenario: Fox skin deforms
- **WHEN** the `Fox` glTF sample is loaded and its `Walk` clip is played
- **THEN** the 24-joint skin deforms per the clip's rotation and translation channels targeting joints

#### Scenario: Unknown channel name warns and skips
- **WHEN** a clip channel name matches neither a joint nor a descendant node
- **THEN** the `Animator` logs a warning once per such channel and continues playing the rest

### Requirement: Unity-legacy-style playback API

The `Animator` SHALL expose a Unity-legacy-inspired API: `Play(name)`, `Play(mode)`, `Stop()`, `Stop(name)`, `Rewind()`, `Rewind(name)`, `CrossFade(name, fadeLength)`, `IsPlaying(name)`, with `PlayMode` (`StopSameLayer` = 0, `StopAll` = 1). Playing a clip SHALL respect the clip's `WrapMode` (`Default`/`Once`/`Loop`/`ClampForever`/`PingPong`) and the per-clip `AnimationState` (`weight`, `speed`, `layer`, `time`, `normalizedTime`, `length`, `enabled`). Multiple clips on different layers SHALL blend by weight within their layer; the highest-weight enabled clip per layer wins (no full additive blending in this change).

#### Scenario: Play a clip by name
- **WHEN** `animator:Play("Walk")` is called and a clip named "Walk" is registered
- **THEN** the clip begins playing from time 0 at its native speed and the `AnimationState` for "Walk" becomes enabled

#### Scenario: Stop same layer only
- **WHEN** `animator:Play("Run", PlayMode.StopSameLayer)` is called while "Walk" is playing on layer 0
- **THEN** "Walk" is stopped if it shares a layer with "Run"; clips on other layers continue

#### Scenario: CrossFade blends weight over time
- **WHEN** `animator:CrossFade("Run", 0.3)` is called while "Walk" is at weight 1.0
- **THEN** over the next 0.3 seconds "Walk"'s weight ramps to 0 and "Run"'s ramps to 1, then "Walk" is stopped

#### Scenario: WrapMode.Loop loops seamlessly
- **WHEN** a clip with `WrapMode.Loop` reaches its end time
- **THEN** playback continues from time 0 without interruption

#### Scenario: WrapMode.PingPong reverses direction
- **WHEN** a clip with `WrapMode.PingPong` reaches its end or beginning
- **THEN** playback direction reverses (speed sign flips) and continues

#### Scenario: WrapMode.Once stops at end
- **WHEN** a clip with `WrapMode.Once` reaches its end time
- **THEN** playback stops and `IsPlaying(name)` returns false

### Requirement: AnimationState tracks per-clip runtime state

The system SHALL expose an `AnimationState` object per clip registered with an `Animator`, with read/write properties: `name`, `clip` (read-only), `enabled`, `weight` (0..1), `speed` (signed), `layer` (int), `time` (seconds, writable for scrubbing), `normalizedTime` (0..1, writable), `length` (read-only), `wrapMode` (read/write). The `Animator` SHALL expose `animator:GetState(name)`, `animator:GetStateCount()`, `animator:GetStateAt(i)`.

#### Scenario: Scrub by setting time
- **WHEN** a script sets `state.time = 0.5` on a stopped clip
- **THEN** the next `Display` call poses the rig at exactly time 0.5 without advancing playback

#### Scenario: Speed reverse plays backward
- **WHEN** `state.speed = -1.0` on a playing clip
- **THEN** the clip's `time` decreases each tick until it reaches 0 (subject to wrap mode)

### Requirement: Clip registration via EntityManager

The `Animator` SHALL resolve `AnimationClip` instances by name (and optionally by UUID hash) through the owning `Scene`'s `EntityManager` (`Scene::Active->GetEntityManager()->Get<AnimationClip>(name)`), matching the existing `GltfImporter.cpp:1163` registration. The `Animator` SHALL also allow direct `SetClip(name, clip)` registration for clips created in script.

#### Scenario: Clip resolved from imported scene
- **WHEN** a glTF scene with animations is loaded and `animator:Play("Walk")` is called
- **THEN** the `Animator` resolves the "Walk" clip from `EntityManager` and plays it

#### Scenario: Script-created clip registered
- **WHEN** a script builds an `AnimationClip` via `AnimationClip.Create(...)` and calls `animator:SetClip("custom", clip)` then `animator:Play("custom")`
- **THEN** the clip plays without going through `EntityManager`

### Requirement: Existing AnimationPlayer dead code refactored away

The existing standalone `AnimationPlayer` class (currently an `Entity`, never instantiated) SHALL be replaced by the new `Animator` component. The `AnimationPlayer.{h,cpp}` files SHALL either be deleted or repurposed to host the `Animator` class. No call sites reference the old `AnimationPlayer` (verified: grep returns only self-references), so removal is safe.

#### Scenario: No AnimationPlayer symbol remains unconverted
- **WHEN** the refactor is complete
- **THEN** grepping the repo for `AnimationPlayer` returns either zero matches or only the new `Animator`-aliased API
