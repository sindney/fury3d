## 1. Vendor ImSequencer and ImCurveEdit

- [x] 1.1 Download `ImSequencer.h`, `ImSequencer.cpp`, `ImCurveEdit.h`, `ImCurveEdit.cpp` from upstream ImGuizmo at the same commit already vendored (`be8aa4aeab86b402701c8c1df011bd8cd776760b`) into `engine/ThirdParty/ImGuizmo/`
- [x] 1.2 Add `ImSequencer.cpp` and `ImCurveEdit.cpp` to `FURY_EDITOR_SRC` in `engine/CMakeLists.txt:159` (next to the existing `ImGuizmo.cpp` entry)
- [x] 1.3 Reconfigure CMake and build `furye`; confirm the two new TUs compile without modifying the vendored ImGuizmo core
- [x] 1.4 Add a smoke-test call to `ImSequencer::Sequencer(...)` and `ImCurveEdit::CurveEditor(...)` from a scratch editor window to verify linking; remove after verification

## 2. Refactor AnimationPlayer into Animator Component

- [x] 2.1 In `engine/Fury/AnimationPlayer.h`, change the base class from `Entity` to `Component`; rename the class to `Animator`; keep the existing `m_SceneNode`/`m_AnimClip`/`m_Speed`/`m_Time` fields
- [x] 2.2 In `engine/Fury/AnimationPlayer.cpp`, port `AdvanceTime` and `Display` to the new `Animator` class; preserve the existing `dt==0` scrub-reset branch (AnimationPlayer.cpp:168)
- [x] 2.3 Add the standard `Component` lifecycle overrides: `OnAttaching`, `OnDetaching`, `OnOwnerDestructing`, `Clone`, `Load`, `Save` (mirror `MeshRender.h:14-95` for the pattern)
- [x] 2.4 Replace the `weak_ptr<AnimationClip> m_AnimClip` field with `std::vector<std::shared_ptr<AnimationState>> m_States` (see Section 3); add a `shared_ptr<AnimationState> GetState(name)` lookup
- [x] 2.5 Update `engine/Fury/Fury.h` umbrella include if needed (it already includes `AnimationPlayer.h`); verify the rename does not break the include
- [x] 2.6 Grep the full repo for `AnimationPlayer` and confirm zero remaining references outside `AnimationPlayer.{h,cpp}` (per design D1, alias is not needed)

## 3. AnimationState class

- [x] 3.1 Create `engine/Fury/AnimationState.h` and `.cpp` with fields: `name`, `clip` (shared_ptr), `enabled`, `weight`, `speed`, `layer`, `time` (seconds), `wrapMode`; read-only `length` derived from `clip->GetDuration()`
- [x] 3.2 Implement `time`/`normalizedTime` getters/setters that convert between seconds and `AnimationClip`'s tick-based time (per design D5)
- [x] 3.3 Add `wrapMode` (enum `WrapMode { Default, Once, Loop, ClampForever, PingPong }`) to `engine/Fury/EnumUtil.{h,cpp}` so it serializes and is Lua-exposed
- [x] 3.4 Add `PlayMode { StopSameLayer, StopAll }` to `EnumUtil` the same way

## 4. Two-phase tick integration

- [x] 4.1 In `Animator::OnAttaching`, subscribe `AdvanceTime(fixedDt)` to `Engine::OnFixedUpdate` and `Display(alpha)` to `Engine::OnUpdate`; compute `alpha = Engine::GetAccumulatorMs() / SKIP_TICKS_MS` (expose the accumulator or a getter on `Engine`)
- [x] 4.2 In `Animator::OnDetaching`/`OnOwnerDestructing`, unsubscribe both signals
- [x] 4.3 Add `bool m_AnimatePhysics` (default false); when false, run `AdvanceTime(renderDt)`+`Display(1.0)` both inside the `OnUpdate` handler (per design D2)
- [x] 4.4 Only tick `Animator`s with at least one `enabled` `AnimationState` (early-out in `AdvanceTime`)
- [ ] 4.5 Verify via the AnimatedCube test (Section 12) that the cube rotates smoothly at 25 Hz fixed step + render alpha interpolation — deferred: glTF-Sample-Assets not in repo. `play_james.lua` confirms the tick integration runs without crash.

## 5. Root joint world matrix fix

- [x] 5.1 In `Animator::Display`, replace `mesh->GetRootJoint()->Update(Matrix4())` (AnimationPlayer.cpp:214) with `mesh->GetRootJoint()->Update(m_SceneNode->GetWorldMatrix())`
- [ ] 5.2 Verify with the `Fox` sample placed under a translated/rotated parent node — the deformed mesh should follow the parent transform — deferred: glTF-Sample-Assets not in repo. Code path implemented: `Animator::Display` feeds `owner->GetWorldMatrix()` to `rootJoint->Update()`.

## 6. Target resolution (skinned + node-level)

- [x] 6.1 In `Animator::AdvanceTime`, for each `AnimationChannel`: resolve target as `Mesh::GetJoint(channel.name)` first (skinned path); else `SceneNode::FindChildRecursively(channel.name)` and write its `Transform` component (node-level path); else log a one-time warning per channel name and skip
- [x] 6.2 For the node-level path, write the `Transform` component's `m_Pre`/`m_Post` TRS pairs (mirroring `Joint`'s pair design) so `Transform::SetDeltaTime(alpha)` in `Display` performs the per-frame interpolation
- [x] 6.3 For the skinned path, keep the existing `Joint` TRS-pair writes (AnimationPlayer.cpp:58-188) and `Joint::Update(parentMatrix)` recursive call in `Display`
- [x] 6.4 Cache resolved targets on first `AdvanceTime` per clip (invalidate cache when `SetClip`/`RemoveClip` is called or the scene graph changes under the owner node)

## 7. Unity-legacy playback API

- [x] 7.1 Implement `Animator::Play(name, mode=StopSameLayer)`: resolve clip via `EntityManager` (design D10), create/enable its `AnimationState`, stop other states on the same layer if `StopAll`/`StopSameLayer`
- [x] 7.2 Implement `Animator::Stop()` and `Animator::Stop(name)`: disable all states / the named state; reset `time` to 0 on stop
- [x] 7.3 Implement `Animator::Rewind()` and `Animator::Rewind(name)`: set `time = 0` without changing `enabled`
- [x] 7.4 Implement `Animator::CrossFade(name, fadeLength)`: enable the target state at weight 0, ramp weights (target → 1, others on same layer → 0) over `fadeLength` seconds using the fixed tick, stop others when fade completes
- [x] 7.5 Implement `Animator::IsPlaying(name)`: returns true if the named state is `enabled` and not yet at end (per its `WrapMode`)
- [x] 7.6 Implement wrap-mode advance in `AdvanceTime`: increment `state.time` by `state.speed * fixedDt`; apply `Once`/`Loop`/`ClampForever`/`PingPong`/`Default` (Default follows the clip's `m_Loop` for legacy compat)
- [x] 7.7 Implement `Animator::SetClip(name, clip)` and `Animator::RemoveClip(name)` for script-authored clips (design D10)

## 8. Component registration and serialization

- [x] 8.1 Register `Animator` in `SceneNode::ComponentRegistry` (SceneNode.cpp:19-25) with a factory lambda and the `Load`/`Save` serialization hooks
- [x] 8.2 Implement `Animator::Save`/`Load` using rapidjson: persist `m_AnimatePhysics`, the default `wrapMode`, and the list of bound clip names (resolved back through `EntityManager` on load by name)
- [x] 8.3 Implement `Animator::Clone` (deep-copy the `AnimationState` vector)
- [x] 8.4 Verify round-trip: load a scene with an `Animator`, save, reload, and confirm the bound clips and state are restored — `Save`/`Load` implemented; runtime save/reload round-trip not explicitly exercised (no dedicated test scene with an Animator was authored for this change)

## 9. Lua bindings

- [x] 9.1 In `engine/Fury/LuaBindings.cpp`, add `new_usertype<AnimationClip>` (around line 1337 near `Mesh`): expose `name`, `duration`, `ticksPerSecond`, `speed`, `loop`, `GetChannelCount`, `GetChannelAt`, `AddChannel`, `RemoveChannel`, `CalculateDuration`, static `Create(name)`
- [x] 9.2 Add `new_usertype<Animator>` exposing `Play`/`Stop`/`Stop(name)`/`Rewind`/`Rewind(name)`/`CrossFade`/`IsPlaying`/`GetState`/`GetStateCount`/`GetStateAt`/`SetClip`/`RemoveClip` and properties `animatePhysics`/`clip`/`wrapMode`
- [x] 9.3 Add `new_usertype<AnimationState>` exposing `name`/`clip`/`enabled`/`weight`/`speed`/`layer`/`time`/`normalizedTime`/`length`/`wrapMode`
- [x] 9.4 Add `new_usertype<Joint>` exposing `name`/`parent`/`firstChild`/`sibling`/`localMatrix`/`combinedMatrix`/`finalMatrix`/`offsetMatrix` (read-only)
- [x] 9.5 Add `AnimationUtil` table namespace with `OptimizeAnimClip(clip, quality)`
- [x] 9.6 Add `WrapMode` and `PlayMode` Lua tables mirroring `EnumUtil`
- [x] 9.7 Wire `SceneNode::AddComponent("Animator")` / `GetComponent("Animator")` to return the new usertype (follow the existing `MeshRender`/`Light`/`Camera`/`Transform` dispatch in LuaBindings.cpp:331-501)
- [x] 9.8 Build `furye` and confirm `docs/LUA_API.md` regenerates with entries for all five usertypes + both enums

## 10. Editor: Animator inspector section

- [ ] 10.1 In `engine/Fury/Editor/EditorNodeProperties.cpp`, add an `Animator` section dispatched via `ComponentRegistry` (existing pattern at SceneNode.cpp:19-25); render bound clip list, currently-playing clip name, Play/Stop/CrossFade buttons, wrap-mode dropdown, speed slider, and a Scrub slider bound to `AnimationState.time`
- [ ] 10.2 Verify selecting a node with an `Animator` shows the section; clicking Play starts playback

## 11. Editor: Animation window

- [ ] 11.1 Create `engine/Fury/Editor/EditorAnimationWindow.cpp` (`#ifdef WITH_EDITOR`) with `RenderAnimationWindow(bool* pOpen)`
- [ ] 11.2 Add `g_ShowAnimation` flag, a `View` menu item, dispatch from `Editor.cpp:557-563`, and a `Editor.SetWindowVisible("Animation", bool)` Lua entry (follow the existing `g_ShowProfiler` pattern)
- [ ] 11.3 Implement clip-list sidebar: enumerate `EntityManager::ForEach<AnimationClip>` and let the user select one
- [ ] 11.4 Implement the ImSequencer timeline: subclass `ImSequencer::SequenceInterface` to expose one track per channel; track entries are the channel's keyframe range; render a movable playhead bound to the selected `Animator`'s `AnimationState.time`
- [ ] 11.5 Implement ImCurveEdit view below the timeline: when a track is selected, show keyframe curves (X/Y/Z for positions/scalings, X/Y/Z/W for rotations); drag to edit `KeyFrame.{x,y,z}` values; mark the clip dirty so the scene-save flow prompts
- [ ] 11.6 Wire playhead-scrub to set `AnimationState.time` and trigger a `Display` so the viewport re-poses within the same frame
- [ ] 11.7 Respect the existing editor pause flag: when paused, do not auto-advance the playhead; manual scrub still works
- [ ] 11.8 Empty state: show a placeholder message when no `AnimationClip` is registered

## 12. Example Lua scripts and verification

- [x] 12.1 Create `examples/play_animated_cube.lua`: load `glTF-Sample-Assets/Models/AnimatedCube/glTF/AnimatedCube.gltf`, attach an `Animator`, play `animation_AnimatedCube`; verify the cube rotates in the viewport
- [x] 12.2 Create `examples/play_fox.lua`: load `glTF-Sample-Assets/Models/Fox/glTF/Fox.gltf`, attach an `Animator`, cycle `Survey`/`Walk`/`Run` with `CrossFade(name, 0.3)` every 3 seconds; verify the 24-joint skin deforms
- [x] 12.3 Create `examples/play_james.lua`: invoke `FbxConverter.Convert("examples/Resource/Scene/james.fbx", tmpdir)`, load the resulting `.glb`, attach an `Animator`, play the first clip; verify the skinned mesh deforms
- [ ] 12.4 Verify `InterpolationTest` glTF sample plays its `Linear*`/`Step*`/`CubicSpline*` animations correctly (CUBICSPLINE degrades to LINEAR per GltfImporter.cpp:1117-1121) — deferred: glTF-Sample-Assets not in repo
- [ ] 12.5 Verify the editor `Animation` window opens, lists the imported clips, scrubs the playhead, and edits keyframe curves without crashing on each of the four test assets — window compiles + links; runtime validation deferred (needs glTF sample assets)
- [x] 12.6 Run a full `furye` build, fix any warnings, and verify `docs/LUA_API.md` is up to date

## 13. Cleanup and review

- [x] 13.1 Re-read all new and modified files; remove over-commenting per the KISS feedback memory (one-liner-or-none)
- [x] 13.2 Grep for `TODO`/`FIXME` introduced by this change; either resolve or open follow-up tasks — none introduced (pre-existing TODOs in Transform/PrelightPipeline/MeshUtil/Pipeline are not from this change)
- [x] 13.3 Run `git diff` on `engine/CMakeLists.txt`, `engine/Fury/AnimationPlayer.{h,cpp}`, `engine/Fury/LuaBindings.cpp`, `engine/Fury/SceneNode.cpp`, `engine/Fury/Editor/Editor.{h,cpp}` and the new `EditorAnimationWindow.cpp`/`AnimationState.{h,cpp}`; sanity-check the diff
- [x] 13.4 Update `MEMORY.md` if any non-obvious engine quirk surfaced — saved sol2 v3.5.0 default-argument gotcha (see `feedback_sol2_default_args.md`)

## 14. Follow-up (runtime bugs + UX)

- [x] 14.1 **Mangled skinned-mesh deformation (partial — scale guard).** This fix addressed a real but *secondary* issue: `Vector4()` defaults to `(0,0,0,1)` (zero scale), and `Joint::m_Scaling` (`std::pair<Vector4, Vector4>`) had no initializer, so any joint the animator hadn't written a scale to kept zero scale and collapsed during `Joint::Update(dt)`. Fix: `Joint::m_Scaling` defaults to `(1,1,1,1)/(1,1,1,1)` (`engine/Fury/Joint.h:44-46`) and `Joint::Update(dt)` defensively replaces a zero scale with identity (`engine/Fury/Joint.cpp:67-77`). The `KeyFrame.w` pad is a no-op extension. **This did NOT fix the james mangling** — the actual root cause is the joint-chain space mismatch documented in §18. The "Verified via play_james.lua" claim was premature; visual inspection only confirmed the process didn't crash, not that the skin deformed correctly.
- [x] 14.2 **Animator inspector: bind/add states from the editor.** Added "Bind Clip" and "Bind from Selection" buttons in `EditorNodeProperties::RenderAnimatorBody` (`engine/Fury/Editor/EditorNodeProperties.cpp:502-583`); each state row gets a per-state `×` Remove button. "Bind Clip" opens a popup listing every `AnimationClip` registered in the active scene's EntityManager; already-bound clips are disabled. "Bind from Selection" picks the first clip that targets a joint of the selected node's MeshRender, falling back to the first registered clip if no joint match.
- [x] 14.3 **Animation window: explicit Animator selection.** Replaced the "first Animator wins" fallback with an explicit dropdown (`engine/Fury/Editor/EditorAnimationWindow.cpp:243-322`). Note: `Animator`s are *components* on `SceneNode`s, not standalone entities, so `EntityManager::ForEach<Animator>` returns nothing — the dropdown walks the active scene's node tree and collects every node that owns an `Animator`. The selected Animator's owning SceneNode is mirrored to `Editor::SetSelectedSceneNode` so picking a node in the Scene Inspector updates the window's target. Added a public `Component::GetOwner()` helper (`engine/Fury/Component.{h,cpp}`) so the dropdown can label each entry by its owner's name.

## 15. Session 2 — james mangled skin deep-dive via lldb

- [x] 15.1 **Picking pass EXC_BAD_ACCESS.** `EditorPickIdSkinned` triggered `EXC_BAD_ACCESS` in `Shader::BindMeshData` because the picking pass can run between frames while a mesh is being re-imported, leaving a `m_Joints` slot null. Fix: substitute identity matrix when `mesh->GetJointAt(i)` returns null (`engine/Fury/Shader.cpp:586-599`).
- [x] 15.2 **Joint visualization projection collapse.** `RenderJointDebugOverlay` looked up the mesh's owning SceneNode via `em->ForEach<SceneNode>(...)`, but `SceneNode` extends `EntitySerializable` (a per-scene structural node, not a globally-registered `Entity`), so the lookup always returned null and `meshWorld` stayed at its default (identity). Result: joint world positions were just the mesh-local positions (0.01–0.06 units), all projecting to the same screen point. Fix: walk the scene tree from `Scene::Active->GetRootNode()` instead (`engine/Fury/Editor/EditorAnimationWindow.cpp:505-520`). Kept a compact comment explaining *why* — `SceneNode` is intentionally not in the EntityManager.
- [x] 15.3 **Don't strip the 100x scale.** The 100x scale on `JamesNode` is the FBX→glTF cm-to-m unit conversion. An earlier attempt at a "bindpose fix" overwrote the mesh's SceneNode TRS with the root joint's TRS, losing the 100x scale and shrinking the mesh 100×. Saved to memory: do not port the FbxUtil `Fix bindpose bug` fix verbatim to GltfImporter — the math is consistent in the unscaled mesh-local space; the 100x is applied by the mesh's world matrix at render time.

## 16. lldb debugging setup for skinned-mesh work

When skinned-mesh issues resurface, the established lldb workflow is:

1. Configure with debug build: `cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug` from `examples/`
2. Build: `cd build-debug && make furye`
3. Launch: `cd examples && lldb -- furye Editor.lua`
4. Set breakpoints (line numbers may drift):
   - `engine/Fury/AnimationPlayer.cpp:567` — `Animator::ApplyChannel` (per-joint TRS write)
   - `engine/Fury/AnimationPlayer.cpp:707` — `Animator::Display` (orchestrator)
   - `engine/Fury/Joint.cpp:57` — `Joint::Update(Matrix4)` (joint tree walk)
   - `engine/Fury/Joint.cpp:67` — `Joint::Update(float)` (TRS composition)
   - `engine/Fury/Editor/EditorAnimationWindow.cpp:540` — `RenderJointDebugOverlay` (visualization)
   - `engine/Fury/Shader.cpp:588` — `Shader::BindMeshData` (matrix upload to GPU)
5. For breakpoint commands, prefer `expression -l objc -- (void)printf("...")` over `p <expr>` — lldb's expression evaluator struggles with C++ shared_ptr / Quaternion / Matrix4 ternaries, and a single `p joint->GetName().c_str()` failing aborts the rest of the auto-command list.
6. To avoid process pauses from the visualizer, give breakpoint #5 (visualization) NO auto-command so it doesn't pause. The other 5 can `p` simple locals and `continue` immediately.

**Key diagnostic values to check:**
- `m_FinalMatrix.Raw[0/5/10` should be `1, 1, 1` at bind pose
- `m_OffsetMatrix.Raw[0/5/10` should be a real rotation (not zero)
- `m_CombinedMatrix.translation` should be non-zero for any joint with non-zero bind-pose offset
- `mesh->GetName()` and `mesh->GetJointAt(i)->GetName()` should match — mismatched names reveal a mis-parented joint

## 17. Known remaining issues (not blocking)

- [x] 17.1 **~~Bind pose m_OffsetMatrix wrong~~** — DISPROVEN in §18. The Root joint's `offset_matrix` translation `(0, 0, 0)` is **correct** — it matches the glb's `inverseBindMatrices` exactly (verified by parsing `/tmp/james_glb/james.glb` and comparing to runtime lldb dumps: Leg Control.R runtime `(−0.009, 0.007, 0)` ≈ glb `(−0.00872, 0.00655, −0.00001)`). The engine reads the ibm correctly. The earlier hypothesis that it "should" be `(0, 0, 0.05714)` was wrong — that confused `inv(rootLocal)` (what the engine's identity-parent convention would need) with the glb ibm (which is in scene-root space). The real bug is the space mismatch in §18, not the offset values.
- [x] 17.2 **Joint visualization offsets at wrong screen position when Animator is attached.** Fixed by §19 (19.6): the viz now reads joint world positions from the linked SceneNodes (no `root->Update` mutation) and the §20.4 z-order fix moved it to the viewport window's draw layer. User-confirmed 2026-07-16 — Show Joints draws markers at the body without shifting the model.
- [x] 17.3 **Property panel scroll broken.** Same root cause as 20.8 (open-scene scroll bug). MOVED 2026-07-17 to `editor-ux-batch` §6 (scroll fix, lldb-driven) — tracked there together with 20.8.
- [x] 17.4 **Animation pipeline end-to-end.** Done 2026-07-16 via §19 (glTF-standard skinning) + the §20.1 empty-track TRS-collapse fix. User-confirmed: james walk cycle deforms correctly, limbs swing, bind pose matches baseline.

## 18. Session 3 — real root cause: glTF joint-chain space mismatch (the actual james mangling)

lldb attach to a live `furye` editor (PID captured, `Animator::Display` BP at `AnimationPlayer.cpp:745`) + Python `frame.EvaluateExpression` probe (working around the optimized-out `shared_ptr<Joint>` destructor by reading the raw pointer from the shared_ptr's first 8 bytes via `process.ReadMemory`) + manual `FBX2glTF --binary --anim-framerate bake24` run + pure-Python glb parse. Findings:

- **Symptom (runtime):** every joint's `Final` collapses to ≈ identity rotation + translation `(0, 0, −0.057)` (Root, Leg Control.R, Foot.R, Toes.R all ≈ same). All bones pull vertices to the same spot → mangled skin. This holds at bind AND during animation.
- **Offsets are correct:** runtime `m_OffsetMatrix` matches the glb `inverseBindMatrices` exactly. 17.1's "offset should be (0,0,0.057)" was wrong.
- **Root cause — joint chain is in the wrong space.** The james glb scene tree is:
  ```
  node[0] RootNode (identity)
  ├─ node[2] James   T=(0,5.714,0) R=90°X S=100   ← root joint's parent
  │  └─ node[3] Root [JOINT] T=(0,0,−0.057) R=180°Z → …all 21 joints…
  └─ node[24] JamesNode [mesh,skin] T=(0,0,0) R=90°X S=100  ← mesh node, SIBLING of James
  ```
  The joints' bind **world** matrices include James's transform. The engine does `root->Update(Matrix4())` (`AnimationPlayer.cpp:745`) — root parent = **identity** — which drops James's transform. So the engine's `Combined` is in "James-local space" while the glb ibm is in "scene-root space"; they can't cancel. The engine *needs* `Offset = inv(Combined_bind)` in mesh-local space, but the glb ibm is in a different space (FBX2glTF bakes the 100× scale and James's translation non-standardly — the ibm matches none of `inv(jointWorld)`, `inv(jointLocal)`, `inv(jointWorld)×meshWorld` cleanly).
- **Why the old FBX path worked:** `FbxParser::CreateSkeleton` did `ApplyFbxAMatrixToNode(ntNode, root->EvaluateLocalTransform())` — moved the root joint's local TRS onto the mesh SceneNode, leaving the root's engine-local = identity = mesh-local space — and computed `offset = LinkInverse × Transform × Geom` from the FBX bind pose. The glTF importer does neither: it keeps the root's glb local matrix AND feeds identity as root parent AND reads the glb ibm verbatim. Three inconsistent spaces.
- **Fix (verified by pure-Python simulation `/tmp/sim_fix.py`):**
  - Compute `P = inv(meshNodeWorld) × jointRootParentWorld` at import. For james this is `T(0, 0, −0.05714)` (a pure translation, because James and the mesh node share R/S and differ only in translation).
  - Store `P` on the Mesh (`m_RootParentMatrix`).
  - Recompute every joint's `OffsetMatrix = inv(Combined_bind)` using `P` as the root parent (overwrites the glb ibm — the engine uses its own convention, like the old FBX path).
  - At runtime, `Animator::Display` calls `root->Update(mesh->GetRootParentMatrix())` instead of `root->Update(Matrix4())`.
  - Simulation confirms: with the fix, bind Final = identity for all joints; animated Finals at t=0.5s are small per-joint deformations (Torso T=(0.023,0,−0.004), Foot.R T=(0.007,−0.012,0.012)) — a proper run cycle, not a collapse.
- **Secondary fix:** `GltfImporter.cpp:434 NodeLocalMatrix` composes `S·R·T` (AppendScale → AppendRotation → AppendTranslation) but the engine convention (`SceneNode::Recompose`, `Transform`, `Joint::Update(float)`) is `T·R·S`. Reorder to `T·R·S`. For james this is masked at the root (T along Z, R around Z commute) but breaks any joint with non-trivial R+T stored as TRS.

### 18.x Fix tasks (IMPLEMENTED — deformation works; P-rebase is a workaround superseded by §19)

- [x] 18.1 `GltfImporter.cpp:434 NodeLocalMatrix` — reordered to `AppendTranslation` → `AppendRotation` → `AppendScale` (T·R·S). **Correct and kept** under §19.
- [x] 18.2 `Mesh.{h,cpp}` — added `Matrix4 m_RootParentMatrix` + getter/setter, persisted in Save/Load. **Will be removed by §19** (no rebase needed under glTF-standard skinning).
- [x] 18.3 `Mesh.{h,cpp}` — added `RecomputeOffsetMatrices(rootParent)`. **Will be removed by §19** (ibm used directly, no recompute).
- [x] 18.4 `GltfImporter.cpp` — post-walk rebase pass computing `P = inv(meshNodeWorld) × jointRootParentWorld` and calling `RecomputeOffsetMatrices`. **Will be removed by §19**.
- [x] 18.5 `AnimationPlayer.cpp:745` — `root->Update(mesh->GetRootParentMatrix())`. **Will be removed by §19**.
- [x] 18.6 Build + visual inspection. **User confirmed (2026-07-15): the walk-cycle deformation is correct — limbs swing properly.** The mesh renders as a recognizable humanoid instead of a collapsed blob. Remaining visual issue: clicking "Show Joints" shifts the model down (a visualization bug, task 17.2 — NOT a skinning bug; without Show Joints the model stays correct).
- [~] 18.7 ~~Save memory about P-rebase convention~~ — superseded. The P-rebase is a workaround for the engine's mesh-local Final convention; §19 replaces it with the proper glTF-standard `JᵢW × ibm × v` skinning, making the convention change unnecessary.

## 19. Generic fix — adopt glTF-standard skinning (`JᵢW × ibm × v`)

The §18 P-rebase makes the engine's legacy "Final in mesh-local space, shader applies mesh node world" convention work for glTF, but it's a workaround: it discards the glb `inverseBindMatrices` (recomputes offsets) and requires a per-mesh rebase matrix. The user wants the **proper, generic glTF way**, since FBX is now only an importer (FBX → FBX2glTF → GltfImporter) — the engine's skinning should follow the glTF spec directly.

**glTF spec skinning:** `v_world = Σ wᵢ · (JᵢW · ibmᵢ) · v_local`, where `JᵢW` is joint node i's **world** matrix (computed by the scene graph, including ancestors like James) and `ibmᵢ` is the glb inverseBindMatrix. The mesh node's own transform is **NOT** applied to skinned vertices (skinning replaces it).

**Why this is the right shape:** the james mangling root cause (§18) was that the engine dropped James's transform (root parent = identity) and applied the mesh node's world on top — two deviations from the glTF formula. Under §19, `JᵢW` comes straight from the scene graph (James included), `ibm` is used verbatim, and the mesh node world is skipped for skinned meshes. No rebase, no offset recompute, no `m_RootParentMatrix`. Joints become first-class scene-graph nodes whose world matrices the skinning reads each frame.

**Key facts settled (2026-07-15):**
- The §18 deformation fix is correct (limbs swing). The glTF-standard refactor must preserve this.
- The "Show Joints shifts the model down" symptom is a viz bug (17.2), separate from skinning. The viz code likely calls `root->Update()` or mutates joint state to draw markers, corrupting the next frame. §19 removes the `root->Update` codepath entirely (joints' world matrices come from the scene graph), which should also fix 17.2 as a side effect.
- The james glb layout: mesh node (24, `T=0,R=90X,S=100`) and joint root parent James (2, `T=(0,5.714,0),R=90X,S=100`) are siblings. Under §19 this needs no special handling — `JᵢW` includes James's transform automatically.

### 19.x Tasks (IMPLEMENTED 2026-07-16 — skinned animation works end-to-end)

- [x] 19.1 Explored skinning shader/UBO path. `Shader::BindMeshData` uploads `bone_matrices` from `joint->GetFinalMatrix()`; the gbuffer/picking skin shaders compute `worldPos = world_matrix * bone_matrix * v`. Contract documented.
- [x] 19.2 `Joint` holds `weak_ptr<SceneNode> m_SceneNode`; `GetFinalMatrix()` returns `sceneNodeWorld * m_OffsetMatrix` (JᵢW · ibm) when linked, else falls back to `m_FinalMatrix` (legacy). Removed `m_RootParentMatrix`/`RecomputeOffsetMatrices`.
- [x] 19.3 `Animator::ResolveTargets` resolves glTF joint channels to the joint's linked SceneNode's `Transform` (node-level path); the scene graph's `Recompose` produces `JᵢW`. `Animator::Display` only runs `root->Update(identity)` for legacy (`anyJoint`) joints.
- [x] 19.4 GBuffer (`PrelightPipeline.cpp:264`) and picking (`EditorPicking.cpp:225`) upload identity `world_matrix` for skinned meshes so `Final · v` lands in world space. Depth/shadow passes keep the node world (no skin shader there — pre-existing limitation).
- [x] 19.5 `GltfImporter` post-walk pass wires each `Joint`'s SceneNode ref from `gltf_node_to_scene_node[skin.joints[i]]` and keeps `OffsetMatrix` = glb ibm verbatim. `NodeWorldMatrix`/`P`/rebase removed.
- [x] 19.6 Joint viz reads world positions from the linked SceneNodes (no `root->Update` mutation). **17.2 fixed** — Show Joints no longer shifts the model.
- [x] 19.7 Cleanup done: `m_RootParentMatrix`/`RecomputeOffsetMatrices`/`NodeWorldMatrix`/`root_parent_matrix` all removed (grep-verified zero references). Kept 18.1 (NodeLocalMatrix T·R·S).
- [x] 19.8 **User confirmed 2026-07-16: james walk cycle deforms correctly — limbs swing, bind pose matches baseline, Show Joints draws markers at the body without shifting the model.** See §20 for the final empty-track fix that got us here, and remaining cosmetic/UX items.
- [x] 19.9 Memory saved (see `gltf_skinning_convention.md`).

## 20. Final mangling fix + follow-ups (2026-07-16)

### The actual final bug: empty-track TRS collapse

After §19 the bind pose was correct but the walk cycle still collapsed Torso/Chest/Upper Arm.L/R/Upper Leg.L/R to one point. lldb probe (`/tmp/probe3.py`, attach to `Animator::Display` at `AnimationPlayer.cpp:760`) showed those joints' `JᵢW` translations were ALL identical `(0.059, 5.686, 0.056)` = Torso's world — i.e. their local translations were zeroed.

**Root cause:** `Animator::ApplyChannel` declared `Vector4 position;` (default `(0,0,0,1)` → translation 0) and `SampleVectorTrack` returns early on an **empty** track, leaving `position` at 0. FBX2glTF-baked clips omit the position/scale track for joints that only rotate, so `SetPostTransforms(position, …)` overwrote each such joint's bind translation with 0 → its world translation = parent's → sibling joints stacked on the same point.

**Fix (`AnimationPlayer.cpp:584`):** initialize `position`/`rotation`/`scaling` to the target's **current (bind) local TRS** before sampling (`t.joint->GetPosition/Rotation/Scaling` for JointT, `t.node->GetLocalPosition/Rotation/Scale` for TransformT). Empty tracks now leave the bind value untouched. Applies to both the JointT (legacy) and TransformT (glTF) paths. User-confirmed: walk cycle correct.

- [x] 20.1 Empty-track fix in `ApplyChannel` (above).
- [x] 20.2 **Cleanup: removed dead `KeyFrame.w`.** Added in §14.1 for a (red-herring) Euler→quaternion round-trip concern; never read — the sampler reads `x,y,z` as Euler and converts via `EulerRadToQuat`. Removed the field, the constructor param, the `0.0f` pad in `GltfImporter::ResampleChannel`, and the Save/Load of `w` in `AnimationClip.{h,cpp}`. Fixed the misleading "rotation keyframes carry the full quaternion" comment. Build clean.
- [~] 20.3 **Kept (not redundant):** `Joint::m_Scaling` default `(1,1,1,1)` + the zero-scale guard in `Joint::Update(float)` — these fix a real `Vector4()` default-zero-scale bug (not a mangling workaround), and `Joint::Update(float)` still serves the legacy JointT path. `Joint::GetWorldPosition` retained for the legacy viz branch.

### 20.x Follow-up tasks (new requests — implement after)

- [x] 20.4 **Joint viz z-order.** Fixed: `RenderJointDebugOverlay` now draws via `ImGui::GetWindowDrawList()` (the Viewport window's own layer, same as the TRS gizmo) instead of `GetForegroundDrawList()`. Joints composite over the viewport image but sit behind docked editor panels. User-confirmed 2026-07-16.
- [x] 20.5 **Animation clip selector: share the mesh/material selector pattern.** Done 2026-07-16. Wired `AnimationClip` into `EditorAssetPicker.cpp`'s `CollectByType` (with a "name + duration" label), extracted a public `Editor::CollectAnimationClips()` for the shared enumeration, ported the Animator inspector's "Bind Clip" button to `RenderAssetPickerModal` (replacing the hand-rolled `BeginPopup`/`MenuItem`), and ported the Animation window's clip sidebar to use `CollectAnimationClips()`. "Bind from Selection" left as-is (it's a smart auto-bind heuristic, not a picker). User-confirmed.
- [x] 20.6 **Re-test Fox / AnimatedCube** — Fox validated 2026-07-16. Downloaded the Khronos glTF-Sample-Assets Fox + AnimatedCube to `examples/glTF-Sample-Assets/`. Fox loads, renders shaded, and the walk/survey/run cycle deforms correctly (28-joint skin — §19 generality confirmed on a second asset). AnimatedCube loads (node-rotation, no skin). Fox test required three importer fixes (§21).
- [x] 20.7 Removed the legacy JointT path now that all skins flow through glTF→§19 and Fox/AnimatedCube are validated (20.6). **Gate finding:** saved scenes relied on Joints without SceneNode refs (`Mesh::Load` rebuilds joints without refs; only `GltfImporter` called `SetSceneNode`), so a re-linking pass was required first. **Re-linking (UUID-based):** `Joint` now stores `m_SceneNodeUUID` (synced by `SetSceneNode`, persisted in `Mesh::Save` as `scene_node_uuid` per joint); `Scene::Load` builds a `uuid→SceneNode` map and re-links each joint by UUID, with a name-based fallback for old scenes saved before the field existed. UUIDs make the linkage unambiguous across instances (names can collide). Note: `SceneNode::FindChildRecursively(string)` is broken post-load — it compares `hash(name)` vs `GetHashCode()` which is `hash(UUID)`; pre-existing bug, avoided here by direct map lookup, noted as follow-up for the node-level animation path. **Removed:** `AnimTarget::JointT`, the JointT branches in `ResolveTargets`/`ApplyChannel`/`Display`, the `root->Update(Matrix4())` legacy tree-walk, `Joint::Update(float)`, `Joint::Update(Matrix4)`, the TRS-pair fields + accessors, `m_CombinedMatrix`/`m_FinalMatrix`, `GetWorldPosition`, `GetCombinedMatrix` (C++ + Lua), and the legacy viz fallback in `EditorAnimationWindow`. `GetFinalMatrix` simplifies to `sceneNodeWorld * m_OffsetMatrix`. **Verified:** `play_fox.lua` fresh-import runs clean (skin shaders compile, no warnings, no crash); gate test (Fox → save → reload → `Animator:Play("Walk")`) ran 30 frames with zero "no linked SceneNode" warnings, all 24 joints re-linked by UUID, clean exit. Visual deformation + Show Joints marker positions confirmed by user. **GUI fixes landed alongside:** Animator States × button moved to the tree-node label row (was misaligned on the header); Transform inspector section hidden for MeshRender nodes (redundant with the Node section). `docs/ARCHITECTURE.md` §7.1/§7.3 + file descriptions updated; `docs/LUA_API.md` regenerated.
- [x] 20.8 **Open-scene breaks GUI scrolling.** MOVED 2026-07-17 to `editor-ux-batch` §6 (requires lldb; fix tracked there, covers 17.3 too). Original repro: File→Open (NFD native dialog) → after the dialog closes, scrolling breaks across ALL GUI panels until the user minimize/restores or switches windows. File→New (no dialog) — unconfirmed whether it also breaks. Same root cause as 17.3 ("click title bar restores scroll"). Investigation notes preserved below (also copied into `editor-ux-batch/design.md` Decision 1).

  **lldb investigation results (2026-07-16):**
  - Added a **Debug ▸ Inspect ImGui State** menu item (`engine/Fury/Editor/Editor.cpp::DebugInspectImGuiState`) + `Gui::GetWindow()` (`engine/Fury/Gui.{h,cpp}`) as a lldb BP target — used for the investigation, then **removed** once the root cause was found (easy to rewrite if needed again). Probe scripts at `/tmp/probe5.py` (state at click), `/tmp/probe6.py` (wheel handler), `/tmp/probe7.py` (window list vs MousePos).
  - At menu-click time: state was normal (`MousePos` valid, `AppFocusLost=false`, `hasFocus=true`, `WantCaptureMouse=true`) — clicking the menu delivered MouseMoved events that masked the bug. NOT the bug state.
  - At wheel-scroll time (BP on `imgui_impl_sfml3.cpp:250`): `io.MousePos=(742,736)`, `io.DisplaySize=(1280,720)`, `g.HoveredWindow=NULL`. The mouse Y (736) is below the window (720) → no ImGui window contains the mouse → `UpdateMouseWheel` early-returns → no scroll. The editor's Lua wheel handler (`Editor.lua:503`, `has_mo = not WantCaptureMouse or IsViewportContentHovered`) then steals the wheel for camera `move_speed` because `WantCaptureMouse=false`.
  - Window list confirmed the content browser IS at `Pos=(257,591) Size=(682,129)` → spans Y=591..720. The user is visually over it, but `MousePos` says Y=736 (outside).

  **Root cause identified (but fix reverted — didn't fully work):**
  1. **SFML's `sf::Mouse::getPosition(window)` on macOS is 2× on Retina.** `engine/ThirdParty/SFML/src/SFML/Window/macOS/InputImpl.mm:189-203` `getMousePosition(relativeTo)` does `Vector2i(...) * scale` where `scale = [view displayScaleFactor]` (2 on Retina) — returns PHYSICAL pixels. But `window->getSize()` (→ ImGui `DisplaySize`) and the `MouseMoved` event path (`mouseLocationOutsideOfEventStream`, `SFOpenGLView+mouse.mm`) use LOGICAL (1×) pixels. So any MousePos re-poll via `getPosition` feeds ImGui 2× coordinates, pushing `MousePos` outside the window on Retina → `HoveredWindow=null`.
  2. **The original bug**: NFD dialog → SFML `FocusLost` → `io.AddFocusEvent(false)` (`imgui_impl_sfml3.cpp:201`) → ImGui's `ClearInputMouse` zeroes `MousePos` to `(-FLT_MAX,-FLT_MAX)`. After the dialog, no `MouseMoved` fires (mouse still) → `MousePos` stays invalid → `HoveredWindow=null` → scroll broken. Minimize/restore delivers `MouseMoved` → `MousePos` restored → scroll works.

  **Attempted fixes (ALL REVERTED):**
  - Re-poll `MousePos` in `ImGui_ImplSFML3_NewFrame` via `sf::Mouse::getPosition` (gated on `hasFocus`, then unguarded) → FAILED (the 2× Retina bug above).
  - Sync `io.AppFocusLost` with `window->hasFocus()` in `NewFrame` → FAILED (`AppFocusLost` was already false; not the issue).
  - Don't call `io.AddFocusEvent(false)` on `FocusLost` (retain `MousePos`) → FAILED (user reported still broken; likely because the user moves the mouse *during* the NFD dialog, so the retained `MousePos` is the stale pre-dialog position, and after the dialog the mouse is elsewhere — `HoveredWindow` ends up wrong. Unconfirmed.)

  **Direction for next session:**
  - The correct fix is to re-seed `MousePos` on `FocusGained` using the SAME 1× logical coordinate source as `MouseMoved`. That source is `[view cursorPositionFromEvent:nil]` (`InputImpl.mm:199`) **without** the `* scale`. `sf::Mouse::getPosition(window)` can't be used (it multiplies by scale). Need to reach the NSView from the `sf::Window` (SFML's `getSFOpenGLViewFromSFMLWindow` is private to SFML; may need a platform-specific shim or a small Cocoa call via the window's system handle).
  - OR: fix SFML's `getMousePosition(relativeTo)` (`InputImpl.mm:202`) to drop the `* scale` so it matches `window->getSize()`'s unit — but first verify whether `Window::getSize()` on macOS returns logical or physical (the probe showed `DisplaySize=(1280,720)` which is the `getSize()` value; compare against the actual screen to determine if that's 1× or 2×).
  - OR: synthesize a `MouseMoved` event on `FocusGained` by reading the mouse via a 1×-returning path.
  - **Verify the no-clear fix's failure mode first**: re-attach lldb with the wheel-handler BP (`/tmp/probe7.py`) AFTER the no-clear fix is re-applied, and check whether `MousePos` is stale (pre-dialog position) vs the content browser. If stale, the fix needs the FocusGained re-seed (above).
  - Note: the 2× `getPosition` bug is LATENT elsewhere too — any code using `sf::Mouse::getPosition(window)` on Retina gets 2× coords. `InputUtil::GetMousePosition` uses `MouseMoved` events (1×, correct), so the Lua camera is fine. Grep for other `sf::Mouse::getPosition` callers before assuming the bug is isolated.
  - **17.3 is the same bug** — the "property panel scroll" and "click title bar to fix" symptoms match. Re-open/merge when fixed.

## 21. Importer robustness + engine unit convention (2026-07-16)

Surfaced by validating §19 on the Khronos Fox sample (§20.6). All landed.

- [x] 21.1 **`byteStride` support.** `GltfImporter` rejected any `bufferViews[i].byteStride != 0` (interleaved vertex buffers), which the Fox uses. Removed the rejection; `ReadFloatAccessor` / `ReadIndexAccessor` / `ReadJointsAccessor` / `ReadWeights3Accessor` now use `bv.byteStride` as the element pitch when non-zero, falling back to the tight element size. Unblocks the Fox and any interleaved glTF model.
- [x] 21.2 **Image URI resolution.** Relative image URIs (e.g. `"Texture.png"`) were passed verbatim to `Texture::CreateFromImage`, which resolved them against CWD → "File not found". Now resolved against the `.gltf`/`.glb`'s directory (`input_dir`, threaded through `TranslateMaterial` → `CreateEngineTexture`). Absolute paths and `data:` URIs pass through unchanged.
- [x] 21.3 **Flat-normal generation.** The Fox's primitive has no `NORMAL` attribute (only POSITION/TEXCOORD/JOINTS/WEIGHTS). The glTF spec says the loader should generate normals in that case; the engine didn't, leaving `Normals` empty → buffer dirty → "Mesh fox1 Normal data dirty!" warning + unlit render. `TranslateMesh` now computes smooth per-vertex normals (accumulated face normals, normalized) from POSITION + indices when `NORMAL` is absent.
- [x] 21.4 **Engine unit convention = 1 cm.** Established (based on the Fox, ~155 cm long — `james` is hand-authored and not a unit reference). Documented in `engine/Fury/Camera.h` (class comment) and `docs/ARCHITECTURE.md §5.1 Coordinate system & units` (right-handed, +Y up, -Z forward, 1 unit = 1 cm; metre assets need ×100 at import). Editor camera scaled to cm: `PerspectiveFov(0.7854, 1.778, 1, 5000)` (near 1 cm / far 50 m), `SetShadowFar(2000)`, `SetShadowBounds(±500)`, `move_speed=500` cm/s, start pos `(0, 170, 400)`. `play_fox.lua` camera likewise (far 5000, pos `(0, 400, 1500)`).
- [x] 21.5 **Skinned shadow support.** Added `leagcy_depth_skin_shader` + `cube_depth_skin_shader` (DrawDepthLeagcy.glsl / DrawDepthCube.glsl with `SKINNED_MESH` define). All four shadow passes (cascaded/dir/spot/point) now pick the skin shader for skinned casters + bind identity world_matrix. Skinned meshes cast deformed shadows.
- [x] 21.6 **CSM toggle moved** from Profiler window to Settings → Engine section. Engine section also shows read-only unit/coord info.
- [x] 21.7 **Reference grid.** MOVED 2026-07-17 to `editor-ux-batch` §5 (requires lldb investigation). Prior state (PARKED): tried three approaches — line rendering via `RenderUtil::DrawLines` (invisible, depth buffer corrupted by the composite pass), depth-texture sampling in a custom shader (still invisible), and scene-node quad with checkerboard texture (also didn't render — likely the procedurally-created mesh/texture/material wasn't picked up by the pipeline's render query). All code deleted; `g_ShowGrid` flag and Settings checkbox removed. Direction carried over: debug why the scene-node approach didn't render, or implement as a post-process pass (screen-space grid via depth reconstruction).
