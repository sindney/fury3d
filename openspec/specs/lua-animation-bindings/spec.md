## ADDED Requirements

### Requirement: AnimationClip Lua usertype

The Lua bindings (`LuaBindings.cpp`) SHALL register an `AnimationClip` usertype following the existing `new_usertype` pattern (e.g., `Mesh` at line 1337, `Material` at 1485). Exposed members SHALL include: `name` (get/set via `Entity`), `duration` (read-only), `ticksPerSecond` (read-only), `speed` (get/set), `loop` (get/set, maps to `WrapMode.Loop` for legacy compat), `GetChannelCount()`, `GetChannelAt(i)` (returns a channel table with `name`, `positions`, `rotations`, `scalings` arrays of `{tick, x, y, z}`), `AddChannel(name)`, `RemoveChannel(name)`, `CalculateDuration()`. Static factory: `AnimationClip.Create(name)`.

#### Scenario: Script reads clip metadata
- **WHEN** a script does `local clip = EntityManager:Get("AnimationClip", "Walk")`
- **THEN** `clip.name`, `clip.duration`, `clip.ticksPerSecond` return the imported clip's values

#### Scenario: Script builds a clip procedurally
- **WHEN** a script calls `AnimationClip.Create("bob")` and adds a channel with keyframes
- **THEN** `clip:CalculateDuration()` returns the expected duration and the clip can be played by an `Animator`

### Requirement: Animator Lua usertype

The Lua bindings SHALL register an `Animator` usertype exposing the Unity-legacy playback API: `Play(name[, mode])`, `Stop()`, `Stop(name)`, `Rewind()`, `Rewind(name)`, `CrossFade(name, fadeLength)`, `IsPlaying(name)`, `GetState(name)`, `GetStateCount()`, `GetStateAt(i)`, `SetClip(name, clip)`, `RemoveClip(name)`, plus properties `animatePhysics` (get/set), `clip` (get, returns the currently playing clip or nil), `wrapMode` (get/set default wrap mode for new plays). The usertype SHALL integrate with `SceneNode::AddComponent("Animator")` so `node:GetComponent("Animator")` returns the usertype instance.

#### Scenario: Script plays a clip on a node
- **WHEN** a script does `local anim = node:GetComponent("Animator"); anim:Play("Walk")`
- **THEN** the clip plays and `anim:IsPlaying("Walk")` returns true

#### Scenario: Script crossfades between clips
- **WHEN** a script calls `anim:CrossFade("Run", 0.3)` while "Walk" is playing
- **THEN** the blend proceeds and `anim:IsPlaying("Walk")` returns false after the fade completes

### Requirement: AnimationState Lua usertype

The Lua bindings SHALL register an `AnimationState` usertype exposing: `name` (read-only), `clip` (read-only), `enabled` (get/set), `weight` (get/set), `speed` (get/set), `layer` (get/set), `time` (get/set, seconds), `normalizedTime` (get/set, 0..1), `length` (read-only), `wrapMode` (get/set).

#### Scenario: Script scrubs a stopped clip
- **WHEN** a script does `local st = anim:GetState("Walk"); st.enabled = false; st.time = 0.5`
- **THEN** the rig poses at time 0.5 on the next frame without advancing

#### Scenario: Script changes speed at runtime
- **WHEN** a script sets `st.speed = 2.0` on a playing clip
- **THEN** subsequent ticks advance the clip's time at 2x its native rate

### Requirement: Joint Lua usertype

The Lua bindings SHALL register a `Joint` usertype exposing: `name` (read-only), `parent` (read-only, returns parent `Joint` or nil), `firstChild` (read-only), `sibling` (read-only), `localMatrix` (read-only), `combinedMatrix` (read-only), `finalMatrix` (read-only), `offsetMatrix` (read-only). Joints SHALL be reachable via `mesh:GetJoint(name)` / `mesh:GetRootJoint()` (already present on `Mesh`). Writes to joint TRS SHALL NOT be exposed (joint poses are driven exclusively by the `Animator`).

#### Scenario: Script traverses the joint tree
- **WHEN** a script does `local root = mesh:GetRootJoint(); local child = root.firstChild`
- **THEN** `child` is the `Joint` usertype for the root's first child, or nil if there is none

#### Scenario: Script reads final matrix
- **WHEN** a script reads `joint.finalMatrix` while an animation is playing
- **THEN** the returned `Matrix4` reflects the current animated pose

### Requirement: AnimationUtil Lua usertype

The Lua bindings SHALL register an `AnimationUtil` usertype (or table namespace) exposing `OptimizeAnimClip(clip, quality)` (mirrors `AnimationUtil::OptimizeAnimClip`), which decimates keyframes by angle threshold and is editor-safe to call on a clip in the `EntityManager`.

#### Scenario: Script optimizes a clip
- **WHEN** a script calls `AnimationUtil.OptimizeAnimClip(clip, 0.85)`
- **THEN** the clip's keyframe count is reduced and `clip:CalculateDuration()` still returns the same duration

### Requirement: WrapMode and PlayMode enums exposed

The Lua bindings SHALL expose `WrapMode` (`Default=0`, `Once=1`, `Loop=2`, `ClampForever=3`, `PingPong=4`) and `PlayMode` (`StopSameLayer=0`, `StopAll=1`) as Lua tables, mirroring the existing `EnumUtil` pattern used for other engine enums.

#### Scenario: Script references wrap mode
- **WHEN** a script does `state.wrapMode = WrapMode.PingPong`
- **THEN** the state's wrap mode is set to PingPong and subsequent playback reverses at the ends

### Requirement: LUA_API.md auto-regenerated

The existing CMake docgen hook (`engine/Tools/lua_api_docgen.py`, wired at `CMakeLists.txt:225-231`) SHALL pick up the new usertypes automatically when `LuaBindings.cpp` changes; no manual doc edits SHALL be required.

#### Scenario: Docs rebuild after binding change
- **WHEN** `LuaBindings.cpp` is modified to add the animation usertypes and the editor is rebuilt
- **THEN** `docs/LUA_API.md` contains entries for `AnimationClip`, `Animator`, `AnimationState`, `Joint`, `AnimationUtil`, `WrapMode`, `PlayMode`

### Requirement: Example Lua playback scripts

The repo SHALL include at least three example Lua scripts under `examples/` exercising the animation system: (a) `play_animated_cube.lua` — loads `AnimatedCube.gltf`, attaches an `Animator`, plays the rotation clip; (b) `play_fox.lua` — loads `Fox.gltf`, cycles through the three clips (`Survey`/`Walk`/`Run`) with crossfade; (c) `play_james.lua` — converts `james.fbx` via `FbxConverter`, loads the result, and plays the first clip on the skinned mesh.

#### Scenario: AnimatedCube example plays
- **WHEN** `furye` runs `play_animated_cube.lua`
- **THEN** the cube's rotation animates visibly in the viewport

#### Scenario: Fox example crossfades
- **WHEN** `furye` runs `play_fox.lua`
- **THEN** the fox mesh deforms through `Survey`, `Walk`, and `Run` clips with visible crossfades

#### Scenario: James FBX example plays
- **WHEN** `furye` runs `play_james.lua`
- **THEN** the FBX-converted skinned mesh deforms through its first clip
