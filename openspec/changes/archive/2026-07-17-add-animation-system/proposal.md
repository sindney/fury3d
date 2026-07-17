## Why

The engine already imports glTF/FBX animation clips and skins into `AnimationClip` / `Joint` / `Mesh` data, and the GPU shader path already consumes `Joint::GetFinalMatrix()` for skinning — but nothing drives those matrices per frame. `AnimationPlayer` exists as dead code (it is an `Entity`, not a `Component`, and is never instantiated or ticked), so every imported animated mesh renders frozen in bind pose. There is also no Lua API to play clips, no editor timeline to scrub them, and no path to animate non-skinned node transforms. This change closes that loop: it wires playback into the engine tick, exposes it as a Unity-legacy-style `Animation` component with a Lua API, and adds an ImSequencer/ImCurveEdit-backed editor panel for scrubbing and editing.

## What Changes

- Promote `AnimationPlayer` from a standalone `Entity` to a `Component` (`Animator`) attachable to a `SceneNode`, matching the existing `MeshRender` / `Light` / `Camera` / `Transform` pattern. Register it in `SceneNode::ComponentRegistry` so it serializes and shows in the Node Properties inspector.
- Drive playback from the engine's existing two-phase tick: `AdvanceTime` on `Engine::OnFixedUpdate` (25 Hz, computes old/new TRS pairs on joints and `Transform` components), `Display(alpha)` on `Engine::OnUpdate` (per-frame interpolation and joint-tree rebuild). This matches the double-buffer design already present in `Joint` and `Transform`.
- Fix the root-joint world matrix feed: pass the owning `SceneNode::GetWorldMatrix()` as the root joint's parent (currently `AnimationPlayer.cpp:214` passes identity, ignoring the mesh's world transform).
- Support both skinned animation (writes `Joint` TRS for `Mesh::m_RootJoint`) and node-level animation (writes the `Transform` component on the targeted child node — covers glTF `translation`/`rotation`/`scale` channels targeting non-skinned nodes, e.g. `AnimatedCube`, `Fox` root motion).
- Add a Unity-legacy-inspired playback API on the component: `Play(name)`, `Stop()`, `CrossFade(name, fadeLength)`, `Rewind()`, `PlayMode` (`StopSameLayer`/`StopAll`), `WrapMode` (`Default`/`Once`/`Loop`/`ClampForever`/`PingPong`), per-clip `AnimationState` (weight, speed, layer, normalizedTime, time, length), and `animatePhysics` (drive on `OnFixedUpdate` vs `OnUpdate`).
- Expose `AnimationClip`, `AnimationPlayer`/`Animator`, `AnimationState`, `Joint`, and `AnimationUtil` to Lua via `sol2` usertypes in `LuaBindings.cpp`, following the existing pattern. Auto-regenerate `docs/LUA_API.md` via the existing docgen hook.
- Vendor `ImSequencer.h/.cpp` and `ImCurveEdit.h/.cpp` from upstream ImGuizmo (currently absent from `engine/ThirdParty/ImGuizmo/`) and add them to `engine/CMakeLists.txt`. These are the timeline track editor and curve editor the user requested as the starting point.
- Add an `Animation` editor window (`RenderAnimationWindow` + `g_ShowAnimation` flag + menu item, following the pattern at `Editor.cpp:557-563`) that uses ImSequencer for clip/track timeline scrubbing and ImCurveEdit for keyframe-curve editing of the selected clip.
- Update `Fury.h` umbrella header (already includes anim headers — no change needed there) and ensure new sources are picked up by the existing CMake glob.
- Add Lua test scripts under `examples/` exercising `AnimatedCube` (node rotation), `Fox` (3 clips on a 24-joint skin), and `james.fbx` (FBX → glTF → skinned playback).

## Capabilities

### New Capabilities
- `animation-playback`: Runtime animation system — `Animator` component, two-phase tick integration, skinned (`Joint`) and node-level (`Transform`) target binding, `AnimationState` per-clip state, Unity-legacy-style play/stop/crossfade/wrap-mode API.
- `animation-editor`: Editor `Animation` window backed by vendored ImSequencer (timeline/track scrubbing) and ImCurveEdit (keyframe curve editing), plus inspector entries for the `Animator` component.
- `lua-animation-bindings`: Lua API surface for `AnimationClip`, `Animator`, `AnimationState`, `Joint`, `AnimationUtil`, and the `Engine.OnFixedUpdate` hook integration for playback.

### Modified Capabilities
<!-- None. Existing gltf-importer already produces AnimationClips in the engine tick-based format; this change consumes them as-is. -->

## Impact

- **Code**:
  - `engine/Fury/AnimationPlayer.{h,cpp}` — refactor from `Entity` to `Component`, add Unity-legacy API surface, fix root-joint parent matrix, add `Transform`-target path.
  - `engine/Fury/AnimationClip.{h,cpp}` — minor: expose wrap mode / state plumbing if needed (mostly unchanged).
  - `engine/Fury/Joint.{h,cpp}` — likely unchanged; the two-phase `Update(parent_matrix)` / `Update(dt)` API already fits.
  - `engine/Fury/SceneNode.cpp:19-25` — add `Animator` to `ComponentRegistry`.
  - `engine/Fury/Engine.cpp` — subscribe animation playback to `OnFixedUpdate` and `OnUpdate` signals (or have the component self-subscribe on `OnAttaching`).
  - `engine/Fury/LuaBindings.cpp` — new usertypes for `AnimationClip`, `Animator`, `AnimationState`, `Joint`, `AnimationUtil`.
  - `engine/Fury/Editor/Editor.{h,cpp}` — new `g_ShowAnimation` flag, menu item, `RenderAnimationWindow` dispatch.
  - New file `engine/Fury/Editor/EditorAnimationWindow.cpp` — ImSequencer + ImCurveEdit panel.
  - `engine/ThirdParty/ImGuizmo/` — vendor `ImSequencer.{h,cpp}` and `ImCurveEdit.{h,cpp}`.
  - `engine/CMakeLists.txt:159` — add the two new ImGuizmo sources to `FURY_EDITOR_SRC`.
- **APIs**: New C++ `Animator` component, new Lua usertypes, new editor window. No existing public C++ or Lua API is broken.
- **Dependencies**: ImSequencer/ImCurveEdit come from the already-vendored ImGuizmo upstream (same MIT license, same namespace). No new external dependency.
- **Test assets**: Reuse `examples/Resource/Scene/james.fbx` (FBX path) and existing `glTF-Sample-Assets/Models/{AnimatedCube,Fox,SimpleSkin,RiggedFigure}` (glTF path). No new assets required.
- **Docs**: `docs/LUA_API.md` auto-regenerated by the existing docgen CMake hook when `LuaBindings.cpp` changes.
