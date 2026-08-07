# player-controllers

## Purpose

Two engine-provided C++ player controller components — a free-fly camera and
a Jolt-driven capsule character — plus the shared camera-binding contract that
decides which camera renders a play session.

## Requirements

### Requirement: Player controllers SHALL be C++ components with a camera binding

`FreeFlyController` and `CharacterController` SHALL be `Component` subclasses
registered in `SceneNode::ComponentRegistry` under those exact names,
serializable, removable, and addable via the inspector's Add Component popup.
Both SHALL expose a `cameraNode` field (scene-node name) and an `enabled`
flag (default true). At play start, the first enabled controller in scene
tree order SHALL resolve its bound camera node and set it as the pipeline's
current camera; additional enabled controllers SHALL log a warning and stay
inactive.

#### Scenario: Bound camera becomes the render camera

- **WHEN** a played scene contains an enabled controller whose `cameraNode` names a node with a Camera component
- **THEN** the runtime renders from that camera

#### Scenario: Two enabled controllers pick the first

- **WHEN** a played scene contains two enabled controllers
- **THEN** the first in tree order drives the camera and input
- **AND** a warning names the ignored controller

### Requirement: FreeFlyController SHALL mirror the editor fly-cam controls

`FreeFlyController` SHALL move its node (typically the camera node) each
frame: mouse-drag look (yaw/pitch, ±89° pitch clamp), WASD/arrow-key
translation in the look basis, LShift speed boost, and mouse-wheel base-speed
adjustment — matching the editor camera scheme (sensitivity, boost factor,
speed range). It SHALL NOT require physics and SHALL work in any played
scene.

#### Scenario: Fly through a scene

- **WHEN** a scene with an enabled FreeFlyController is played
- **AND** the user drags the mouse and holds W with LShift
- **THEN** the camera looks around and accelerates forward, matching editor feel

### Requirement: CharacterController SHALL simulate a capsule via Jolt CharacterVirtual

`CharacterController` SHALL own a Jolt `CharacterVirtual` capsule with
editable `height`, `radius`, `walkSpeed`, `runSpeed`, and `jumpSpeed`
(auto-fit from the owning node's world AABB on add). On the fixed tick it
SHALL read WASD (camera-relative when a camera is bound, node-relative
otherwise), apply walk/run speed (LShift runs), jump on Space when grounded,
and apply scene gravity; slope and small-step handling SHALL use Jolt's
character update. The owning node's transform SHALL follow the capsule via
the physics-world interpolated sync. The capsule SHALL collide with static
and dynamic bodies.

#### Scenario: Walk, run, and jump in the outdoor scene

- **WHEN** the fox node has a CharacterController and the scene plays
- **AND** the user holds W (fox walks forward), adds LShift (fox runs), presses Space (fox jumps)
- **THEN** the capsule moves accordingly, is blocked by rocks/fences, walks up gentle slopes, and falls back to ground after a jump

#### Scenario: Capsule pushes a dynamic crate

- **WHEN** the character walks into a light dynamic crate
- **THEN** the crate is pushed/toppled by the capsule

### Requirement: CharacterController SHALL drive locomotion animations

The controller SHALL expose `idleClip`, `walkClip`, `runClip` (defaulting to
`Survey`, `Walk`, `Run`) and, when the owning node (or a named descendant)
has an `Animator`, SHALL crossfade between them based on planar speed
thresholds. A `modelYawOffset` field SHALL correct models whose forward axis
differs from movement direction; the model SHALL yaw toward the velocity
direction when moving.

#### Scenario: Fox animation follows movement state

- **WHEN** the fox stands still, walks (W), and runs (W+LShift)
- **THEN** the Animator plays `Survey`, `Walk`, and `Run` respectively with smooth crossfades
- **AND** the fox model faces its movement direction (after `modelYawOffset` correction if needed)

### Requirement: Controllers SHALL be inert in the editor

Controller components SHALL NOT move nodes, capture input, or switch cameras
while simulation is disabled (editor). They activate only in the play
runtime.

#### Scenario: Editor camera unaffected by a controller in the scene

- **WHEN** a scene containing an enabled CharacterController is edited in `furye`
- **THEN** the editor camera and node positions remain exactly as authored
