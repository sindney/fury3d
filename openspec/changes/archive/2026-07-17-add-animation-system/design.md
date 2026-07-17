## Context

fury3d already has the data plane for skeletal animation: `GltfImporter` translates glTF animations into `AnimationClip`s (tick-based, 24 Hz) and registers them in `EntityManager`; `Joint` carries double-buffered TRS pairs and a recursive `Update(parentMatrix)` that builds `m_FinalMatrix` per joint; `Mesh::CalculateAABB` CPU-skins; `Shader.cpp:534-598` uploads `bone_matrices` to the GPU. The `Transform` component mirrors the same double-buffer pattern, evidently designed for node-level animation. None of this is driven: `AnimationPlayer` exists as a standalone `Entity`, is never instantiated, has zero Lua bindings, and is not registered in `ComponentRegistry`. Every imported animated mesh therefore renders frozen in bind pose.

The engine's main loop (`Engine::Run`, Engine.cpp:244-349) already runs a 25 Hz fixed-tick accumulator (`OnFixedUpdate`) followed by a per-frame `OnUpdate(dt)` — exactly the two-phase split the existing `AdvanceTime`/`Display` design in `AnimationPlayer.cpp` was built for.

Reference APIs: Unity's legacy `Animation` component (`Play`/`Stop`/`CrossFade`/`Rewind`, `AnimationState` per-clip with `weight`/`speed`/`layer`/`time`/`normalizedTime`/`wrapMode`, `PlayMode.StopSameLayer`/`StopAll`, `WrapMode.Once`/`Loop`/`ClampForever`/`PingPong`). Editor reference: ImSequencer (timeline track editor) and ImCurveEdit (keyframe curve editor) from upstream ImGuizmo — currently **not vendored** (only `ImGuizmo.{h,cpp}` is present).

Test assets on disk: `examples/Resource/Scene/james.fbx` (FBX path via `FbxConverter`), `glTF-Sample-Assets/Models/AnimatedCube` (single-channel node rotation, no skin), `…/Fox` (24-joint skin, 3 named clips), `…/SimpleSkin`/`RiggedSimple` (minimal skinning), `…/RiggedFigure`/`CesiumMan`/`BrainStem` (~19-joint humanoids).

## Goals / Non-Goals

**Goals:**
- Wire existing animation data into the engine tick so imported clips actually play, both for skinned meshes (`Joint` targets) and node-level transforms (`Transform` component targets).
- Expose a Unity-legacy-style C++ and Lua API on a new `Animator` `Component` so users can `Play`/`Stop`/`CrossFade` from script.
- Add an editor `Animation` window using ImSequencer + ImCurveEdit for timeline scrubbing and keyframe curve editing.
- Fix the root-joint world-matrix bug (currently passes identity, ignoring the mesh's world transform).
- Land all of this without changing `AnimationClip`'s tick-based format or the glTF import path — they already produce the right data.

**Non-Goals:**
- **No Mecanim-style state machines / Animator Controller graphs.** Unity's legacy `Animation` API is the reference, not the modern `Animator`. State-machine blending is a future change.
- **No additive blending, no avatar masks, no IK.** Per-layer highest-weight-wins is the only blend this change supports.
- **No root motion** beyond what falls out of driving a `Transform` on a node channel. No character-controller integration.
- **No morph-target animation.** glTF morph targets are already rejected by `GltfImporter.h:36-41`; that stays.
- **No re-authoring of the glTF/FBX import pipeline.** Clips come in tick-based at 24 Hz; we keep that.
- **No animation event tracks** (Unity's `AnimationEvent`). Future scope.
- **No retargeting** between rigs.

## Decisions

### D1: Promote `AnimationPlayer` to an `Animator` `Component` (vs. keep as standalone `Entity`)

**Choice:** Refactor `AnimationPlayer` into `Animator : public Component`, registered in `SceneNode::ComponentRegistry` (SceneNode.cpp:19-25) alongside `MeshRender`/`Light`/`Camera`/`Transform`.

**Why:** Components are the engine's only attachment model — `SceneNode::AddComponent` rejects anything else, and the Node Properties inspector dispatches off `ComponentRegistry`. A standalone `Entity` cannot be inspected, serialized, or reached via `node:GetComponent`. The existing `AnimationPlayer` is dead code with no call sites (verified by grep), so there is no migration burden.

**Alternatives considered:**
- *Keep `AnimationPlayer` as `Entity`, add a thin `Animator` `Component` wrapper.* Rejected: two classes for one concept, and the wrapper would have to forward every API.
- *Make it a global system keyed by scene.* Rejected: loses the per-node binding that makes the Unity API ergonomic (`node:GetComponent("Animator"):Play("Walk")`).

### D2: Drive playback from `Engine::OnFixedUpdate` + `Engine::OnUpdate` (vs. single per-frame tick)

**Choice:** `AdvanceTime(fixedDt)` runs on `OnFixedUpdate` (25 Hz, the existing accumulator in Engine.cpp:281-288), `Display(alpha)` runs on `OnUpdate` with `alpha = accumulatorMs / SKIP_TICKS_MS`. The `Animator` self-subscribes on `OnAttaching` and unsubscribes on `OnDetaching`/`OnOwnerDestructing` so the engine core stays animation-agnostic. `animatePhysics=false` collapses both phases into `OnUpdate` (matches Unity's `animatePhysics` toggle semantics).

**Why:** The existing `Joint` TRS pairs (`m_Position.first`/`m_Position.second`, etc.) and `Transform::SetDeltaTime` are already designed for this two-phase split — `AdvanceTime` writes old/new, `Display` interpolates by render alpha. A single-tick design would either lock pose updates to 25 Hz (visible judder at high refresh) or recompute the entire joint tree at frame rate (wasteful). Self-subscription keeps `Engine.cpp` free of animation-specific code.

**Alternatives considered:**
- *Central animation manager ticked by Engine.* Rejected: requires Engine to know about animators; the component self-subscribe pattern matches how `MeshRender`/`Light` already participate in rendering.
- *Everything on `OnUpdate`.* Rejected: wastes the existing fixed-tick accumulator and breaks `animatePhysics` semantics.

### D3: Resolve root-joint parent from the owning `SceneNode`'s world matrix

**Choice:** In `Display(alpha)`, call `mesh->GetRootJoint()->Update(m_SceneNode->GetWorldMatrix())` instead of `Update(Matrix4())` (the current dead-code behavior at AnimationPlayer.cpp:214).

**Why:** A skinned mesh translated/rotated in the scene graph must deform in its world space. Passing identity silently produces correctly-shaped-but-wrongly-placed skinning. This is a latent bug the current dead code happens to encode.

### D4: Skinned vs. node-level target resolution by name lookup order

**Choice:** For each `AnimationChannel`, the `Animator` resolves the target by:
1. If the owning `MeshRender`'s `Mesh` has a `Joint` named `channel.name` → write `Joint` TRS pairs (skinned path).
2. Else if a descendant `SceneNode` named `channel.name` exists → write the `Transform` component on that node (node-level path).
3. Else → log a one-time warning per channel name and skip.

**Why:** glTF channels target nodes by name (see `GltfImporter.cpp:1131-1139`); for skinned meshes those names map to joints, for non-skinned meshes they map to descendant nodes. The engine's `Joint` and `SceneNode` both inherit `Entity::m_Name`, so name collision is the natural disambiguator. The order (joint first) matches the common case: a `Fox` clip targets joints; an `AnimatedCube` clip targets the cube node itself.

**Alternatives considered:**
- *Separate `AnimationClip` subclasses for skinned vs. node.* Rejected: glTF mixes both in one animation (a humanoid clip often animates the root node's translation AND joint rotations); splitting would force authors to merge.
- *Tag channels with a target type at import.* Rejected: would require modifying `GltfImporter` and the clip format — explicitly a non-goal.

### D5: Keep the tick-based (24 Hz integer) time base

**Choice:** Leave `AnimationClip`'s `KeyFrame.tick` (unsigned int), `m_TicksPerSecond` (default 24), and `GltfImporter`'s `anim_ticks_per_second` resampling unchanged. The `Animator` converts to seconds only at the `AnimationState` API boundary (`state.time`, `state.normalizedTime`).

**Why:** The importer already resamples glTF float-seconds to integer ticks (GltfImporter.cpp:1096-1164) and `FbxConverter` bakes FBX at `--anim-framerate bake24` (FbxConverter.cpp:177). Changing the format would force reworking both importers — a non-goal. The Unity legacy API surface is in seconds, so the conversion happens in `AnimationState` getters/setters.

### D6: `AnimationState` as a separate object (vs. fields on `Animator`)

**Choice:** `AnimationState` is its own class with `name`/`clip`/`enabled`/`weight`/`speed`/`layer`/`time`/`normalizedTime`/`length`/`wrapMode`. The `Animator` owns `vector<shared_ptr<AnimationState>>` keyed by clip name. `Animator::GetState(name)` returns the state, `GetStateAt(i)` enumerates.

**Why:** Mirrors Unity's legacy `AnimationState` API exactly, lets multiple clips be registered on one `Animator` with independent weights/speeds/layers, and keeps the `Animator` class focused on ticking rather than per-clip bookkeeping. It also makes the Lua surface clean (`state.time = 0.5` reads naturally).

**Alternatives considered:**
- *Single "current clip" on the Animator.* Rejected: cannot represent crossfade (two clips must coexist with weights) or multi-layer playback.
- *Map of clip→inline struct.* Rejected: loses the named type that Lua usertypes and the inspector need.

### D7: Unity-legacy API surface (vs. modern Unity Animator / Mecanim)

**Choice:** Implement `Play(name[, mode])`/`Stop()`/`Stop(name)`/`Rewind()`/`Rewind(name)`/`CrossFade(name, fadeLength)`/`IsPlaying(name)` and `PlayMode.StopSameLayer`/`StopAll`, `WrapMode.Default`/`Once`/`Loop`/`ClampForever`/`PingPong`. Per-layer highest-weight-wins blending only.

**Why:** The user explicitly asked for the Unity legacy API. Legacy `Animation` is dramatically simpler than Mecanim (no state graph, no avatar, no blend tree) and is sufficient for the basic playback goal. Additive blending and state machines are listed as non-goals.

### D8: Vendor ImSequencer + ImCurveEdit from upstream ImGuizmo

**Choice:** Add `ImSequencer.h`/`ImSequencer.cpp` and `ImCurveEdit.h`/`ImCurveEdit.cpp` to `engine/ThirdParty/ImGuizmo/` from the same upstream ImGuizmo commit the vendored `ImGuizmo.{h,cpp}` already tracks (header comment: `be8aa4aeab86b402701c8c1df011bd8cd776760b`). Add the two `.cpp` files to `FURY_EDITOR_SRC` in CMakeLists.txt:159.

**Why:** The user explicitly asked for ImSequencer as the timeline starting point and ImCurveEdit (a.k.a. `imVectorEditor`) for curves. Both are MIT-licensed parts of the same upstream ImGuizmo repo, so the license surface does not change. Building from raw ImGui primitives would be a multi-hundred-line reimplementation of solved problems (timeline ruler, playhead drag, curve point editing).

**Alternatives considered:**
- *Build timeline from raw ImGui.* Rejected: significant scope expansion for no benefit; the user asked for ImSequencer specifically.
- *Use a different timeline library.* Rejected: introduces a new dependency; ImGuizmo is already vendored.

### D9: Editor window via the existing `g_Show*` pattern

**Choice:** Add `g_ShowAnimation` flag, menu item under `View`, `RenderAnimationWindow(&g_ShowAnimation)` function, and `Editor.SetWindowVisible("Animation", bool)` Lua entry — exactly the pattern at Editor.cpp:557-563. New file `engine/Fury/Editor/EditorAnimationWindow.cpp` (#ifdef `WITH_EDITOR`), auto-globbed by CMakeLists.txt:158.

**Why:** Every other editor window follows this pattern; deviating would require new registration infrastructure. The CMake glob picks up new files automatically on reconfigure.

### D10: Clip resolution through `EntityManager` + direct `SetClip`

**Choice:** `Animator::Play(name)` resolves the clip via `Scene::Active->GetEntityManager()->Get<AnimationClip>(name)` (matches the GltfImporter registration at GltfImporter.cpp:1163). `Animator::SetClip(name, clip)` lets scripts register a procedurally-built clip without going through `EntityManager`.

**Why:** `EntityManager` is already the engine's clip registry — every imported clip is there. Direct `SetClip` covers the script-authored-clip case without forcing the user to manually register and unregister.

## Risks / Trade-offs

- **[Two-phase update doubles joint work]** `AdvanceTime` writes TRS pairs and `Display` interpolates+rebuilds — for very high joint counts this is more work than a single-tick design. → Mitigation: only tick `Animator`s whose `AnimationState` is `enabled` and whose mesh is not culled (future: respect a culling type like Unity's `CullingType`). For the test assets (max 24 joints on Fox, 19 on humanoids) this is negligible.
- **[Tick-based time base is unusual]** Float-second APIs (`state.time`) require conversion at every boundary. → Mitigation: conversion is centralized in `AnimationState` getters/setters; the `AnimationClip` format is unchanged so no importer work.
- **[ImSequencer/ImCurveEdit upstream stability]** These two files are less actively maintained than ImGuizmo core. → Mitigation: vendor at the same commit as the existing ImGuizmo; treat as code we own and patch if needed.
- **[Per-layer highest-weight-wins blending is limited]** Cannot do smooth additive blends or blend trees. → Mitigation: explicitly a non-goal; `CrossFade` covers the common "smoothly transition between two clips" case. Full blending is a future change.
- **[Channel name collision between joints and nodes]** If a glTF scene names a joint and a node identically, the joint wins by D4's ordering. → Mitigation: this matches the skinned-mesh case (joints are the meaningful target); for non-skinned scenes there are no joints so the node path is taken. Document the resolution order in the `Animator` header.
- **[Refactoring `AnimationPlayer` while it's dead code is safe, but `Fury.h` umbrella includes it]** Removing the `AnimationPlayer` symbol could break downstream includes. → Mitigation: either alias `using AnimationPlayer = Animator;` in `AnimationPlayer.h` for one release, or just rename in place and update `Fury.h`. Verified no call sites exist outside `AnimationPlayer.cpp` itself.
- **[Root-joint parent fix may change existing bind-pose renders]** Currently a skinned mesh renders at bind pose in identity; feeding `node->GetWorldMatrix()` will move it. → Mitigation: this is the correct behavior; any test scene that relied on identity was wrong. Verify with `Fox` and `james.fbx` test scripts.
- **[Editor `Animation` window adds per-frame work]** ImSequencer + ImCurveEdit re-render every frame the window is open. → Mitigation: only render curves for the currently selected channel, not all channels at once.

## Migration Plan

1. Implement `Animator` component (D1), tick wiring (D2), root-joint fix (D3), target resolution (D4), `AnimationState` (D6), Unity API surface (D7).
2. Add Lua usertypes for `AnimationClip`/`Animator`/`AnimationState`/`Joint`/`AnimationUtil` + `WrapMode`/`PlayMode` enums.
3. Vendor ImSequencer + ImCurveEdit (D8); add editor window (D9).
4. Add the three example Lua scripts and verify each test asset plays.
5. No rollback strategy needed: this is additive (new component, new window, new Lua API). The only deletion is `AnimationPlayer`'s dead code, which has no callers.

## Open Questions

- **`AnimationPlayer` symbol aliasing.** Keep `using AnimationPlayer = Animator;` for one release, or rename outright? Leaning toward outright rename since the old class has zero callers (verified by grep), but worth confirming during implementation.
- **Culling.** Should `Animator` skip `AdvanceTime`/`Display` when the owner node is outside the camera frustum (Unity `CullingType.BasedOnRenderers`)? Out of scope for this change but worth noting; current design always ticks enabled animators.
- **ImSequencer customization depth.** The vendored ImSequencer API is fairly generic (track list + entry start/end). Adapting it to the per-channel-per-keyframe model may require a custom `SequenceInterface` subclass. To be resolved during implementation of the editor window (D9).
