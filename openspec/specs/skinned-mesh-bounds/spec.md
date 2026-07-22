# skinned-mesh-bounds

## Purpose

Keeps a skinned mesh's axis-aligned bounding box in sync with the current animated pose. Without this, culling, LOD selection, shadow-caster collection, and the `Draw Mesh Bounds` debug overlay all use the bind-pose AABB, which can cull deformed meshes at frustum edges or report stale bounds to the artist.

## Requirements

### Requirement: Skinned meshes SHALL recompute their AABB from the current joint pose every frame the pose changes

Whenever an `Animator` applies an updated pose to the joints of a skinned mesh (animation playing, blending, or an editor scrub), the engine SHALL recompute that mesh's model-space AABB from the joint-deformed vertex positions (the existing `Mesh::CalculateAABB()` skinned branch, which blends vertices by `Joint::GetFinalMatrix()`) within the same frame, and push the result to the owning `SceneNode` via `SetModelAABB`.

The update SHALL be gated on the pose actually changing: a stopped, paused, or otherwise non-advancing `Animator` SHALL NOT trigger the recompute, so static skinned meshes cost nothing per frame.

#### Scenario: Walking Fox keeps tight bounds

- **GIVEN** a skinned Fox mesh playing its walk cycle
- **WHEN** the pose advances between frames
- **THEN** the mesh's model AABB is recomputed from the posed joints before the frame's culling / LOD / shadow passes run

#### Scenario: Stopped animator skips the recompute

- **GIVEN** a skinned mesh whose `Animator` has no enabled state
- **WHEN** a frame elapses
- **THEN** `Mesh::CalculateAABB()` is not invoked for that mesh

### Requirement: Updated skinned bounds SHALL be visible to all world-AABB consumers

After `SetModelAABB` lands on the owning `SceneNode`, every consumer of node bounds SHALL observe the new bounds for the current frame: frustum/visibility queries (octree or the active scene manager), LOD screen-coverage selection, shadow-caster collection, and the `Draw Mesh Bounds` debug overlay. If the scene manager caches node placement (octree membership), the bounds update SHALL propagate through the same invalidation path a transform change uses.

#### Scenario: Deformed mesh is not culled at the frustum edge

- **GIVEN** a skinned mesh whose bind-pose AABB sits inside the frustum but whose animated pose extends outside it
- **WHEN** the camera views the bind-pose location edge-on
- **THEN** visibility decisions use the posed AABB (the mesh is culled only when the POSED bounds leave the frustum)

#### Scenario: Draw Mesh Bounds tracks the pose

- **GIVEN** the Profiler → Perf → Debug Overlays `Draw Mesh Bounds` overlay is enabled
- **WHEN** a skinned mesh plays an animation
- **THEN** the drawn box tracks the deformed pose rather than remaining at the bind pose
