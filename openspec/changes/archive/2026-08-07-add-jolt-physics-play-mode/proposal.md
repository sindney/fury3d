# Proposal: add Jolt physics + editor Play mode with player controllers

## Why

Fury3D has no physics: scenes are static dioramas you can only inspect through
the editor camera. There is no way to experience a scene at ground level, test
walkable geometry, or watch rigid-body behavior. This change integrates
JoltPhysics (user's chosen engine) as a vendored submodule, adds an editor
**Play** flow that round-trips the current scene through a temp file into the
`fury` runtime, and ships two basic player controllers (free-fly camera and a
Jolt-driven capsule character) so a scene can be walked, jumped through, and
knocked around like a real game — verified end-to-end on a derived
`outdoor_physics` scene with the glTF Fox as the player character.

## What Changes

- **Vendor JoltPhysics** as a git submodule at `engine/ThirdParty/JoltPhysics`
  (pinned to a stable release tag), wired into `engine/CMakeLists.txt` like
  the SFML/nfd precedents (submodule presence check + `add_subdirectory` +
  link into both `fury` and `furye`).
- **New `PhysicsWorld` engine service**: owns the Jolt `PhysicsSystem`, steps
  it on the existing `Engine::OnFixedUpdate` tick (25 Hz, substeps), syncs
  body transforms back to `SceneNode`s on `Engine::OnUpdate` using
  `Engine::GetFixedTickAlpha()` interpolation — the `Animator` two-phase
  precedent. Simulation runs in the `fury` runtime only; the editor never
  simulates (Play = separate process).
- **New `BodySetup` component**: collision authoring per node. Shape type =
  render mesh (default) / box / sphere; motion type = static (default) /
  dynamic (mass, friction, restitution). Serialized in the scene, editable in
  the Node Properties inspector, registered in `SceneNode::ComponentRegistry`.
- **New BodySetup editor window**: same layout and shared 3D preview machinery
  as the mesh editor (`Editor3DPreview` orbit view), opened from the BodySetup
  inspector section. Includes meshoptimizer-based collision-mesh reduction
  (via the existing `MeshSimplifier` wrapper); the reduced mesh is saved as a
  real mesh asset in the scene and referenced by name.
- **New Play mode in the editor**: menu-bar Play button saves the active scene
  to a temp `.bin` next to the original scene file (never touches the
  original) and launches `fury Player.lua <temp.bin>` as a detached child
  process. Includes fixing the editor-camera leak: editor-only nodes (the
  `EditorCamera` nodes that already pollute `outdoor_water.bin` 4×) get an
  `editorOnly` node flag that serialization skips.
- **Two `PlayerController` components** (C++):
  - `FreeFlyController` — same control scheme as the editor camera (mouse-drag
    look, WASD/arrows move, LShift boost, wheel speed), no physics.
  - `CharacterController` — Jolt `CharacterVirtual` capsule: walk, run, jump,
    gravity, slope handling; WASD camera-relative; drives an `Animator`
    (idle/walk/run clip crossfade by speed).
  - Both bind to a **camera node** by name; the active controller's bound
    camera becomes the render camera in play mode. If a played scene has no
    enabled controller, `Player.lua` spawns a free-fly fallback so Play always
    works.
- **Camera frustum gizmo**: selecting a node with a `Camera` component draws a
  wireframe frustum overlay in the viewport (via the existing
  `RenderUtil::DrawFrustum`), making camera/player placement visual.
- **Test content**: new derived scene
  `examples/Projects/outdoor/outdoor_physics.bin` (outdoor_water.bin stays
  pristine): Fox glTF copied into the project (`Projects/outdoor/Fox/`),
  imported and placed as the player (capsule controller + `Animator` with
  Survey/Walk/Run + third-person follow camera behind it), static BodySetup
  colliders on ground/rocks/trees/fences, a few dynamic crates near spawn.
  Built by a repeatable `fury exec` setup script.

## Capabilities

### New Capabilities

- `physics-world`: Jolt submodule + build integration, `PhysicsWorld`
  lifecycle, fixed-step simulation with interpolated node sync, cm-world-unit
  convention (gravity −981 cm/s²), editor-vs-runtime simulation gating.
- `body-setup-component`: the component data model (shape/motion types,
  mesh/sphere/box, mesh reference by name, dynamic body params), serialization,
  inspector registration, runtime body/shape construction (incl.
  mesh→convex-hull fallback for dynamic bodies).
- `body-setup-editor`: the editor window (mesh-editor layout, shared 3D
  preview, shape visualization, meshoptimizer reduction → mesh asset).
- `play-mode`: Play button, temp-scene save (with editor-only node stripping),
  detached `fury` subprocess launch, `Player.lua` bootstrap, controller
  selection + free-fly fallback.
- `player-controllers`: `PlayerController` base (camera binding),
  `FreeFlyController`, `CharacterController` (capsule walk/run/jump +
  animation driving).

### Modified Capabilities

- `selection-visualization`: selected camera nodes draw a wireframe frustum
  overlay (new branch alongside light sphere/cone + mesh AABB).
- `scene-round-trip`: nodes flagged editor-only are excluded from scene
  serialization (fixes the EditorCamera leak at the root).

## Impact

- **New dependency**: JoltPhysics submodule (MIT), static lib, C++17 — matches
  the toolchain; both binaries link it (editor needs BodySetup data + editor
  window; only `fury` simulates).
- **Build**: `engine/CMakeLists.txt` — submodule check, `add_subdirectory`,
  link, new core sources in `FURY_CORE_SRC` (PhysicsWorld, BodySetup,
  controllers) + editor source (`EditorBodySetupWindow`).
- **Core engine**: `Engine::Initialize/Shutdown` gain physics init/teardown;
  `SceneNode` gains an `editorOnly` flag (default false — no format break;
  format version stays 3, unknown-field tolerant).
- **Editor**: `Editor.cpp` menu bar (Play button + F5), `EditorNodeProperties`
  (2 new inspector sections + registry entries), `EditorSelectionViz` (camera
  branch), `EditorAssetWindows` family + 1, `examples/Editor.lua` (mark editor
  camera editor-only, dedupe on load).
- **Runtime**: `examples/Player.lua` (new thin bootstrap), `fury` gains
  physics stepping in its main loop.
- **Lua**: bindings for the new components + `Physics` table;
  `docs/LUA_API.md` regenerated (`-DFURY_DOCGEN=ON`).
- **Content**: `examples/Projects/outdoor/` gains `Fox/` assets +
  `outdoor_physics.bin` + a repeatable setup script; `.gitignore` gains the
  `*.play.tmp.bin` temp pattern.
- **Non-breaking**: scenes without BodySetup/controllers load and run exactly
  as today; physics only activates when a scene contains physics components.

## Known traps accounted for (from project memory)

- **×100 scaled-ancestor trap** (outdoor `RootNode` is ×100, nested node
  scales everywhere): static bodies bake the world matrix into shape
  vertices; dynamic/character sync goes through world-matrix inverse +
  `MathUtil::Decompose`, never piecewise `GetWorld*` reads.
- **glTF skinned-mesh ×100 scale** must stay on the Fox's SceneNode (bindpose
  math is consistent in unscaled space) — the capsule derives from the world
  AABB, not raw vertex units.
- **CPU index data may be freed** (`SubMesh::DeleteRawData`) on rendered
  meshes — collision shape building must run while CPU data exists (or meshes
  referenced by BodySetup keep it); explicitly verified in tasks.
- **Jolt mesh shapes are static-only** — dynamic + mesh shape falls back to a
  convex hull (logged), matching Jolt's constraints.
