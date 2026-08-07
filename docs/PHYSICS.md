# Physics & Play Mode

Fury3D integrates [JoltPhysics](https://github.com/jrouwe/JoltPhysics) (vendored at
`engine/ThirdParty/JoltPhysics`, static lib linked into both `fury` and `furye`).
The editor never simulates; **Play** launches the `fury` runtime on a temp copy of
the scene.

## Play mode

- **Play button** (menu bar, right side) or **F5** in `furye`:
  1. Saves the active scene to `<scene dir>/.play_<name>.tmp.bin` — the original
     file is never touched, and editor-only nodes (the editor camera) are excluded
     from the temp copy automatically (`SceneNode::SetEditorOnly`).
  2. Launches `fury Player.lua <temp.bin>` as a detached child process.
  3. Close the fury window (or press **Escape**) to end the session.
- `examples/Player.lua` contract: load scene → apply its renderSettings to the
  pipeline → activate the first enabled `PlayerController` (its bound camera
  renders) → if none exists, spawn a free-fly camera framed on the scene AABB.
- Headless verify hook: `FURY_CAM="px,py,pz,yawDeg,pitchDeg"` pins a static camera
  (no controller activation) — combine with `--screenshot out.png`.

## Player controllers

### FreeFlyController
Editor-identical flight: **LMB drag** look, **WASD/arrows** move, **LShift** ×5
boost, **wheel** adjusts base speed. Drives its bound camera node (or its own
node if unbound).

### CharacterController
Jolt `CharacterVirtual` capsule: **WASD** walk (camera-relative), **LShift** run,
**Space** jump, LMB drag orbits the third-person boom. Fields: `height`, `radius`
(auto-fit from node bounds), `walkSpeed`/`runSpeed`/`jumpSpeed`,
`cameraDistance`/`cameraHeight`, `modelYawOffset` (model forward fix, degrees),
`idleClip`/`walkClip`/`runClip` (default `Survey`/`Walk`/`Run` — crossfades an
Animator on the node or a direct child by planar speed).

Both expose `cameraNode` (scene-node name) + `enabled`. The first enabled
controller in tree order is active; extras log a warning.

## BodySetup component

Per-node collision. Default: the sibling MeshRender's mesh as a static mesh
collider. Fields: `shapeType` (mesh/box/sphere), `motionType` (static/dynamic),
`collisionMesh` (optional explicit mesh asset), `halfExtents`/`radius` (auto-fit
from the mesh AABB), `mass`/`friction`/`restitution` (dynamic).

- **Static mesh** bodies bake the node's full world matrix into the collision
  vertices (scaled ancestors like the ×100 `RootNode` are irrelevant to Jolt).
- **Dynamic** bodies bake world scale into shape dims and write their transform
  back to the node each frame (interpolated, via parent-inverse + Decompose).
- Dynamic + mesh shape falls back to a convex hull (Jolt mesh shapes are
  static-only; logged once).
- **BodySetup editor**: "Open Body Setup Editor…" in the inspector — mesh-editor
  layout with orbit preview, live shape wireframe, and *Simplify Collision Mesh…*
  (meshoptimizer) which saves the reduced mesh as a real scene mesh asset named
  `<mesh>_collision[_N]` and references it.

## Units & stepping

1 engine world unit = 1 cm; Jolt runs directly in these units — gravity defaults
to `(0, -981, 0)` (per-scene override via the `"physics"` block, sibling of
`renderSettings`). Physics steps on the engine fixed tick (25 Hz × 2 substeps)
and node transforms interpolate with the engine's fixed-tick alpha.

## Lua

`Physics.SetEnabled/GetEnabled/Step(n)/SetGravity/GetGravity`,
`scene:SetPhysicsGravity/GetPhysicsGravity`, `BodySetup` / `FreeFlyController` /
`CharacterController` usertypes (`Create()`, fields, `AutoFitFromMesh` /
`AutoFitFromNode`), `PlayerController.FindFirstEnabled(root)` /
`PlayerController.ActivateFirst(root)`, `node:SetEditorOnly/IsEditorOnly`,
`node:FindChildRecursively(name)` (by NAME — fixed; the old hashcode path never
matched), `scene:GetTexture(name)` + `Texture:SetFilePathAndSRGB`.
Full list: `docs/LUA_API.md`.

## Test assets

`Projects/outdoor/outdoor_physics.bin` — derived from `outdoor_water.bin`
(fox player: CharacterController + Animator + third-person camera, 17 static mesh
colliders, 3 dynamic crates). The .bin is the committed artifact; the one-off
setup script that generated it is not committed. Headless checks:
`tests/lua/physics_smoke.lua`, `tests/lua/editor_only_nodes.lua`.

## Traps (learned during implementation)

- **Lazy body creation**: components attach mid-`Scene::Load` when world matrices
  are stale (`AddChild` doesn't recompose). `PhysicsWorld` creates bodies on the
  first fixed tick after a root `Recompose(true)` — never in `OnAttaching`.
- **`SceneNode::FindChild(name)`** used to hash the name and compare against UUID
  hashes — could never match. Compares names directly now.
- **Camera serialization**: `Camera` historically persisted nothing (empty
  components array). It now round-trips projection params + shadow settings.
- **Texture resolution in play mode**: bare texture paths resolve via
  `Scene::Path` (scene working dir) at load — `Player.lua` creates its scene with
  the scene file's own directory as working dir. Get this wrong and every texture
  stays `dirty` (never uploads; `Shader::BindTexture` skips the bind, leaving the
  previous unit's texture bound).
- **`SubMesh::DeleteRawData`** has no callers — CPU mesh data always survives, so
  runtime collision-shape building from render meshes is safe.
