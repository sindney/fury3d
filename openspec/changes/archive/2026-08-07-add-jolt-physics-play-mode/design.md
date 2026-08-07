# Design: add Jolt physics + editor Play mode with player controllers

## Context

Fury3D is a C++17 OpenGL engine with an ImGui editor (`furye`) and a runtime
(`fury`) built from one CMake configure. Both binaries output to `examples/`;
`fury` sits next to `furye`. The runtime is **Lua-bootstrapped**:
`fury <script.lua> [args...]` — `Editor.lua` treats `arg[1]` as the startup
scene path. Scenes are rapidjson documents (`.bin` = LZ4-compressed JSON,
format version 3); all assets live in a per-scene `EntityManager`, referenced
by name. Components subclass `fury::Component` (`Clone`/`Load`/`Save`), are
instantiated by `SceneNode::ComponentRegistry`, and subscribe to engine
signals for per-frame work — there is no virtual `Update`.

Existing machinery this design reuses:

- `Engine::OnFixedUpdate` (25 Hz, `GetFixedDt()=0.04`, `MAX_FRAMESKIP=5`) +
  `Engine::OnUpdate(dt)` + `Engine::GetFixedTickAlpha()` — the `Animator`
  "advance on fixed, display interpolated" pattern (`AnimationPlayer.cpp`).
- `MeshSimplifier.SimplifyMesh(mesh, opts)` (wraps vendored meshoptimizer) —
  the editor LOD "Generate" flow already simplifies rendered meshes and stores
  results as assets (`EditorAssetWindows.cpp:575`, `tests/lua/gen_lod.lua`).
- `Editor3DPreview` helpers (`OrbitFor`/`EnsureRT`/`ComputeViewProj`/
  `DrawGroundGrid`/`ApplyCameraInput`) — the shared 3D view used by the mesh
  and particle editors; `EditorParticleWindow` is the clean copy-template.
- `RenderUtil::BeginDrawLines`/`DrawFrustum`/`DrawBoxBounds`/`EndDrawLines` —
  used by `EditorSelectionViz::DrawSelectionOverlay` (light sphere/cone +
  AABB today).
- `FbxConverter`'s `posix_spawn`/`CreateProcess` subprocess pattern and its
  file-static `GetExecutablePath()` (to be lifted into a shared util).
- Editor fly-cam scheme in `examples/Editor.lua:787-856` (LMB-drag look,
  WASD/arrows, LShift ×5 boost, wheel speed) — the freefly reference.

Constraints discovered:

- Engine world unit = **1 cm**; outdoor scene inner `RootNode` is ×100 with
  non-uniform node scales throughout (e.g. `Feu` scl ≈ (17,17,83)).
- `outdoor_water.bin` already contains **4 stray `EditorCamera` nodes** —
  `Editor.lua` parents the editor camera into the scene root and saves leak it.
- `SubMesh::DeleteRawData()` can free CPU index data on rendered meshes.
- Jolt `MeshShape` is static-only; dynamic bodies need convex/primitive shapes.
- `Signal::Connect` requires `shared_ptr` + member-function pointers (no
  lambdas); `SceneNode::AddComponent` allows one component per type.
- The glTF importer does NOT attach `Animator` to skinned nodes; Fox clips
  arrive named `Survey`/`Walk`/`Run`.

## Goals / Non-Goals

**Goals:**
- Vendor JoltPhysics; both `fury` and `furye` link it; macOS + Windows build.
- Runtime physics simulation (static + dynamic bodies, capsule character) in
  `fury`; editor stays simulation-free.
- Editor Play flow: temp-scene save → detached `fury Player.lua <temp.bin>`.
- BodySetup component + mesh-editor-style authoring window with meshoptimizer
  reduction that yields a saved mesh asset.
- Two C++ player controllers with camera binding; camera frustum gizmo.
- Derived `outdoor_physics.bin` test scene with the Fox as a playable
  third-person character, built by a repeatable script.

**Non-Goals:**
- Play-in-editor (simulating inside `furye`'s viewport) — Play is a separate
  `fury` process by design.
- Physics debug rendering (Jolt `DebugRenderer`), constraints/joints, ragdoll,
  vehicles, raycast-based gameplay APIs, Lua-exposed physics queries.
- Character root-motion (animation moves the capsule) — capsule drives, clips
  are cosmetic.
- Kinematic bodies, CCD tuning UI, per-body collision layers beyond the
  static/dynamic split, networking/determinism guarantees.
- Migrating existing scenes: no BodySetup → zero behavior change.

## Decisions

### D1: Jolt vendored as a submodule, built via its own CMake like SFML/nfd

Add `engine/ThirdParty/JoltPhysics` → `https://github.com/jrouwe/JoltPhysics.git`,
pinned to the latest stable release tag at apply time. In
`engine/CMakeLists.txt`: presence check (`ThirdParty/JoltPhysics/Jolt/Jolt.h`,
same FATAL_ERROR style as the other submodules), then
`add_subdirectory(ThirdParty/JoltPhysics/Build)` with Jolt's consumer targets
disabled (`TARGET_UNIT_TESTS/HELLO_WORLD/SAMPLES/VIEWER/PERFORMANCE_TEST` OFF)
and link `Jolt` into `FURY_PLATFORM_LIBS` (both binaries). Defaults kept:
float precision, static lib, `JPH_DEBUG_RENDERER` off. Includes via SYSTEM
isolation per the ThirdParty convention.

*Alternative considered*: wrapping Jolt sources in our own glob+static-lib
stanza (the meshoptimizer pattern) — rejected: Jolt ships a maintained CMake
with per-platform SIMD/flag handling (`Build/CMakeLists.txt`); replicating it
invites MSVC/ARM drift. *Also considered*: physics as an optional
`FURY_PHYSICS` CMake option — rejected for v1: the user picked Jolt as THE
physics engine; an off-switch adds untested build permutations. (The option is
trivial to add later if a headless-CI footprint issue appears.)

### D2: 1 Jolt unit = 1 cm (engine world unit), constants scaled

Jolt is tuned for meters; the engine world is centimeters. We run Jolt **in
engine world units directly** and scale the handful of unitful constants:
gravity `(0, -981, 0)` cm/s², penetration slop ~2 cm → `2.0`, character
radius/height/speeds authored in cm (Fox ≈ 155 cm long). Physics settings
(gravity, and a future units field) live in a new optional `"physics"` block
on the scene root (sibling of `renderSettings`), defaulting to cm-tuned
values when absent.

*Alternative considered*: scale ×0.01 at the Jolt boundary (positions in,
transforms out) — rejected: every body sync, shape build, velocity read, and
character query crosses that boundary; under the ×100-node nesting trap a
second ×100 conversion is where bugs breed. Keeping Jolt in world units makes
sync identity-mapping, and Jolt officially supports rescaling its constants.

### D3: `PhysicsWorld` singleton owns Jolt; step on fixed tick, sync on update

New `Fury/PhysicsWorld.{h,cpp}` — a `Singleton` in the `RenderUtil`/`InputUtil`
style, created in `Engine::Initialize`, torn down in `Engine::Shutdown`
(Jolt's `RegisterTypes`/`UnregisterTypes` symmetry). Owns: `TempAllocator`,
`JobSystemThreadPool`, broadphase/object layers (two-layer v1: `NON_MOVING` /
`MOVING`), `JPH::PhysicsSystem`. Stepping: `PhysicsWorld::TickFixed` subscribes
to `Engine::OnFixedUpdate` → `PhysicsSystem::Update(0.04, /*collisionSteps*/2)`
(50 Hz effective integration). Sync: `TickUpdate(dt)` on `Engine::OnUpdate`
writes interpolated (`GetFixedTickAlpha`) body transforms to owning nodes.
`Signal::Connect` gets a `shared_ptr<PhysicsWorld>` receiver — fine.

*Alternative considered*: dedicated 60 Hz physics accumulator — rejected for
v1: duplicates the engine's existing fixed-step machinery; 25 Hz with 2
substeps is a documented Jolt usage and matches the `Animator` physics path
already shipping.

### D4: Simulation gated by `PhysicsWorld::SetSimulationEnabled`; fury on, furye off

`PhysicsWorld` checks the flag before creating bodies or stepping. Default is
compile-gated: `fury` enables, `furye` disables (set in `main.cpp` via the
existing `WITH_EDITOR` split). CLI subcommands (`exec`/`convert`) never enter
`Engine::Run`, so no stepping occurs there regardless; a Lua
`Physics.SetEnabled(bool)` binding lets headless scripts opt in explicitly.
Editor Play therefore cannot disturb the editing session: simulation only ever
runs in the child `fury` process. `BodySetup::OnAttaching` defers body
creation to the world's "build from scene" pass when simulation is enabled —
so a script that enables physics after load still gets bodies.

### D5: Body↔node transforms: statics bake the world matrix into the shape; dynamics write back via matrix inverse + Decompose

- **Static bodies**: collision vertices (or box/sphere dims) are transformed
  by the node's **world matrix** at build time; the Jolt body sits at
  identity. This makes the ×100 `RootNode` and non-uniform node scales
  physically irrelevant — Jolt sees plain world-space geometry.
- **Dynamic bodies**: shapes are built in node-local space with **scale baked
  into the shape dimensions** (box extents × world scale, radius × max world
  scale); the body holds world position/rotation. Each frame the body transform
  is written back as: `localMatrix = parentWorld.inverse() * bodyWorld` →
  `MathUtil::Decompose` → `SetLocalPosition/SetLocalRoattion` +
  `Recompose(false)` — never piecewise `GetWorld*` reads (the
  scale-pollution trap). Dynamic nodes with non-uniform scale are out of
  contract (log once, bake uniform max-scale).
- **Character**: `CharacterVirtual` tracks its own world position; the
  character's SceneNode receives the same matrix-inverse write-back. The
  player node is authored at scene root in the test scene (no scaled
  ancestor), but the write-back path is the correct general one.

### D6: Shape construction & CPU-data lifetime

BodySetup shape sources:
- **Mesh (default)** — positions+indices from the referenced mesh (defaults to
  the sibling `MeshRender`'s base mesh, else `collisionMesh` name reference).
  Multi-submesh meshes concatenate submesh index arrays with vertex offsets
  (the `MeshSimplifier.cpp:225-232` both-layouts pattern). Static →
  `JPH::MeshShapeSettings`; dynamic → `JPH::ConvexHullShapeSettings` fallback +
  one log line (Jolt constraint, not a choice).
- **Box** — `JPH::BoxShape` from editable half-extents, auto-fit from the
  mesh's local AABB on first add. **Sphere** — `JPH::SphereShape` likewise.

CPU-data trap: `SubMesh::DeleteRawData()` frees index data on rendered static
meshes. Mitigations: (1) `PhysicsWorld` builds all shapes at scene-load /
simulation-enable time, before the first render pass; (2) a mesh referenced by
a BodySetup gets a keep-CPU-data flag checked by the deletion path; (3) the
simplified collision mesh assets generated by the editor are never rendered,
so their CPU data survives naturally. Task 2.x verifies (1)'s ordering claim
in code before relying on it.

### D7: BodySetup component model

Fields (serialized): `shapeType` (`mesh`|`box`|`sphere`, default `mesh`),
`motionType` (`static`|`dynamic`, default `static`), `collisionMesh` (name,
empty = use sibling MeshRender mesh), `halfExtents` (Vector4, auto-fit),
`radius` (float, auto-fit), `mass` (10), `friction` (0.5), `restitution`
(0.1), plus computed-at-attach body handle (not serialized). Registered as
`"BodySetup"` in `SceneNode::ComponentRegistry`; inspector section in
`ComponentRenderTable` with an "Open Body Setup Editor…" button (the
`EditorNodeProperties.cpp:412` particle-editor precedent) — removable via Add
Component rules like any non-Transform component.

### D8: BodySetup editor window — particle-window pattern, mesh-editor layout

New `Fury/Editor/EditorBodySetupWindow.{h,cpp}`: popup-id-keyed open set
(`"BodySetupEditor:<node uuid>"`), `OpenBodySetupEditor(node)` +
`RenderAllOpenBodySetupEditors()` called from `RenderAllOpenAssetEditors()`.
Left ~70%: `Editor3DPreview` orbit view rendering the node's render mesh
(`GetSimpleLambertShader`) + collision shape wireframe overlay
(`RenderUtil::DrawMesh`/lines + `DrawGroundGrid`). Right ~30%: shape/motion
combos, dims (auto-fit on shape switch), mass/friction/restitution, collision
mesh row, and **"Simplify Collision Mesh…"** modal (lod-count reduction UI
precedent) → `MeshSimplifier::SimplifyMesh` with a single-level target → new
`Mesh` added to the scene `EntityManager` under a unique name
(`asset-unique-naming` rules, `<mesh>_collision` suffix) → assigned to
`collisionMesh`. Saving the scene persists it like any mesh asset.

### D9: Play mode — temp scene beside the original, detached spawn, thin Player.lua

- **Editor-only leak fix (root cause)**: `SceneNode` gains `editorOnly`
  (default false). `SceneNode::Save` skips flagged subtrees. `Editor.lua`
  flags its camera at creation and, on scene load, removes stale root
  children named `EditorCamera` (cleans the 4 existing strays on next save).
  Lua binding `SceneNode:SetEditorOnly(bool)`.
- **Play flow** (menu-bar button, right-aligned before the scene-name status,
  F5 shortcut; disabled while no scene is loaded):
  1. Save active scene to `<scene dir>/.play_<name>.tmp.bin`
     (`FileUtil::SaveCompressedFile`); never saved scene →
     `std::filesystem::temp_directory_path()` fallback + console warning
     (relative texture paths may not resolve). `.gitignore` gains the
     `.play_*.tmp.bin` pattern; editor deletes stale temp files on startup.
  2. Resolve sibling `fury` binary via the lifted `GetExecutablePath` util.
  3. Detached spawn `fury Player.lua <temp.bin>`: POSIX `posix_spawn` with
     `signal(SIGCHLD, SIG_IGN)` auto-reap; Windows `CreateProcess` +
     `CloseHandle`. Editor stays fully interactive; closing the fury window
     ends play.
- **`examples/Player.lua`** (new, ~60 lines): load `arg[1]` scene → find the
  first enabled `PlayerController` in the node tree → it activates its bound
  camera via `Pipeline.SetCurrentCamera` → if none found, create a freefly
  controller node framed on the scene AABB. No editor-camera creation, no
  editor calls.

*Alternative considered*: reuse `Editor.lua` in fury (its `Editor.*` calls
no-op) — rejected: it would drag the editor camera node + frame-selection
logic into play sessions and blur the runtime contract. *Also considered*:
saving temp into the system temp dir always — rejected as default because
scene-relative asset resolution (`working_dir`, texture paths) is safest next
to the original file.

### D10: Player controllers as C++ components

- `PlayerController` (abstract base, `Component`): fields `cameraNode` (name,
  resolved at attach/play start), `enabled` (default true). On play start
  (simulation enabled), resolves the camera node and calls
  `Pipeline::SetCurrentCamera`; first enabled controller in tree order wins,
  extras log a warning. Registered names: `"FreeFlyController"`,
  `"CharacterController"`.
- `FreeFlyController`: subscribes `Engine::OnUpdate`; mirrors Editor.lua —
  LMB-drag yaw/pitch (±89° clamp), WASD/arrows translate in the camera basis,
  LShift ×5 boost, wheel adjusts base speed. If no camera bound, drives its
  own node (which may itself carry a Camera).
- `CharacterController`: owns a Jolt `CharacterVirtual` (capsule: `height`,
  `radius` fields auto-fit from the node's world AABB). On `OnFixedUpdate`:
  reads WASD (camera-relative when a camera is bound, else node-relative),
  walk/run speeds (LShift run), Space jump with grounded check, gravity from
  the scene physics block; `ExtendedUpdate` for slope/stair handling. On
  `OnUpdate`: matrix-inverse write-back (D5) + optional animation driving —
  fields `idleClip`/`walkClip`/`runClip` (default `Survey`/`Walk`/`Run`) and a
  planar-speed threshold crossfade (`Animator::CrossFade`), plus
  `modelYawOffset` for model-forward fixes. Movement yaw follows velocity;
  camera node stays a child/offset of the player (third-person rig authored in
  the scene; mouse-drag orbits yaw around the player).

*Alternative considered*: Lua controllers (the engine's only camera controller
today is Lua) — rejected: `CharacterVirtual` lifetime, fixed-step input
sampling, and body sync belong in C++; Lua bindings for components still ship
for scripting. Unity's CharacterController/Camera-parenting relation is the
reference model (controller moves the body; camera is a bound observer).

### D11: Camera frustum gizmo = one new branch in the existing selection overlay

`EditorSelectionViz::DrawSelectionOverlay` gains
`else if (auto cam = node->GetComponent<Camera>())` →
`BeginDrawLines(editorCam)` + `DrawFrustum(cam->GetFrustum(), kSelectionColor)`
+ `EndDrawLines()` — drawn instead of the AABB box, clipped to a sane far
(min(far, farCap) with a `GetFrustum(near, far)` overload that already
exists). Uses the existing overlay RT binding and depth-off convention.

### D12: Test scene derived by a repeatable `fury exec` script

`examples/Projects/outdoor/setup_physics_scene.lua` (run via
`fury exec outdoor_water.bin setup_physics_scene.lua`): imports
`Projects/outdoor/Fox/Fox.gltf` (files copied from glTF-Sample-Assets:
`Fox.gltf`+`Fox.bin`+`Texture.png` — subfolder keeps the generic texture name
collision-free), places the Fox at a flat spot, adds `Animator` (Survey
looping) + `CharacterController` (capsule fit to fox) + child camera node
behind/above (bound via `cameraNode`), adds static BodySetup to Grid/rocks/
trees/fences (skipping grass billboards), drops a few dynamic cubes
(`MeshUtil.CreateCube` + BodySetup dynamic) near spawn, saves
`outdoor_physics.bin` (`FileUtil.SaveCompressedFile`). Deterministic and
re-runnable — never edits `outdoor_water.bin`.

## Risks / Trade-offs

- **Jolt build friction on MSVC** (SIMD flags, /W4 noise through non-SYSTEM
  includes) → Jolt's own CMake handles per-platform flags; keep includes
  SYSTEM; verify Windows build in tasks (the user runs macOS; Windows is
  best-effort compile, matching repo convention).
- **25 Hz step + 2 substeps may feel coarse for jumping** → collisionSteps is
  a one-line knob; CharacterVirtual is robust at low rates; revisit only if
  the user feels it.
- **CPU data freed before shape build** (DeleteRawData) → D6 mitigations +
  explicit verification task; worst case we flip the keep-flag default for
  collision-referenced meshes.
- **Temp scene + relative asset paths** → temp file written next to the
  original scene (D9); fallback path warns loudly.
- **Fox forward axis may not match movement yaw** → `modelYawOffset` field +
  visual check task (the skinning-debug memory's "Show Joints" overlay is the
  diagnostic tool if animation looks wrong).
- **Mesh colliders on dense trees/rocks** → Jolt cooks `MeshShape` once at
  load; counts here are tiny (15 meshes); the simplify flow exists if a scene
  gets heavy.
- **Zombie/orphan child processes** → `SIGCHLD` ignore / handle close; fury
  window close = normal process exit.
- **Non-uniform-scaled dynamic bodies** → out of contract (logged), statics
  unaffected (world-baked).

## Migration Plan

No data migration: scenes without the new components are untouched; the
`physics` block is optional with cm-tuned defaults; format version stays 3.
The `editorOnly` flag is additive; old scenes load with flag=false (their
stray `EditorCamera` nodes are cleaned by the new `Editor.lua` dedupe on next
save). Rollback = revert the change; Jolt submodule removal restores the
exact prior build.

## Open Questions

- Exact Jolt release tag to pin (resolve at apply time: latest stable).
- Whether `Physics.SetEnabled(true)` in `fury exec` scripts should also force
  a fixed number of steps for headless determinism tests (nice-to-have; v1
  may add `Physics.Step(n)` if tests need it).
