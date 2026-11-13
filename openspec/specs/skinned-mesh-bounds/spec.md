# skinned-mesh-bounds

## Purpose

Keeps a skinned mesh's axis-aligned bounding box in sync with the current animated pose. Without this, culling, LOD selection, shadow-caster collection, and the `Draw Mesh Bounds` debug overlay all use the bind-pose AABB, which can cull deformed meshes at frustum edges or report stale bounds to the artist.

## Requirements

### Requirement: Skinned meshes SHALL keep a reference bound and a posed bound as separate quantities

A mesh asset SHALL hold a reference bound covering its own geometry in the mesh's own space, and the engine SHALL compute the posed bound of a skinned mesh separately, in world space, from the joint-deformed vertex positions. The two SHALL NOT be stored in the same box: a box that mixes mesh-space and world-space extents cannot be interpreted by any consumer, and mixing them makes the mesh-local half reappear at the world origin once the box is converted back to model space.

The posed bound SHALL be derived from the deformed vertices themselves, so it covers exactly the geometry that is drawn. The reference bound SHALL remain the mesh asset's own property and SHALL NOT be overwritten by posing.

#### Scenario: The asset's reference bound survives posing

- **GIVEN** a skinned mesh with a stored reference bound
- **WHEN** the mesh is posed by an animation
- **THEN** the stored reference bound is unchanged and still describes the mesh's geometry in mesh space

#### Scenario: The posed bound covers the deformed geometry

- **GIVEN** a skinned mesh whose animation moves vertices outside its reference bound
- **WHEN** the pose is applied
- **THEN** the posed bound covers the deformed vertices and no longer the reference extent

### Requirement: The renderable component SHALL own the posed bound of a skinned mesh

The component that renders a skinned mesh SHALL own its posed bound and SHALL publish it to the owning `SceneNode`, so that `SceneNode::GetWorldAABB()` equals the posed world bound. The animation system SHALL be the party that detects that the pose moved; it SHALL NOT compute or write the bound itself.

A `SceneNode`'s world bound is derived from its model bound, which for a skinned mesh is the posed world bound converted into the node's space. The conversion SHALL be redone whenever the node's transform changes, so the node's world bound keeps equalling the posed world bound.

#### Scenario: The renderable publishes the posed bound

- **GIVEN** a skinned mesh whose pose has been applied
- **WHEN** the owning `SceneNode` is queried for its world AABB
- **THEN** the result equals the posed world bound of the mesh

### Requirement: A skinned mesh's bound SHALL NOT follow the mesh node's transform

A skinned mesh renders with an identity model matrix: its vertices reach world space through the joint matrices, so the mesh node's own transform does not move the rendered geometry. The node's world bound SHALL therefore not move with the node's transform either.

#### Scenario: Moving the mesh node leaves the bound on the geometry

- **GIVEN** a skinned mesh with a posed bound
- **WHEN** the mesh node's transform changes while the joints stay put
- **THEN** the node's world AABB is unchanged, because the rendered geometry did not move

### Requirement: Skinned meshes SHALL recompute their bound from the current joint pose when the pose changes

Whenever an `Animator` applies an updated pose to the joints of a skinned mesh (animation playing, blending, or an editor scrub), the engine SHALL recompute that mesh's posed bound from the joint-deformed vertex positions within the same frame, and publish it to the owning `SceneNode`.

The update SHALL be gated on the pose actually changing: a stopped, paused, or otherwise non-advancing `Animator` SHALL NOT trigger the recompute, so static skinned meshes cost nothing per frame.

#### Scenario: Walking Fox keeps tight bounds

- **GIVEN** a skinned Fox mesh playing its walk cycle
- **WHEN** the pose advances between frames
- **THEN** the Fox's world bound is recomputed from the posed joints before the frame's culling / LOD / shadow passes run

#### Scenario: Stopped animator skips the recompute

- **GIVEN** a skinned mesh whose `Animator` has no enabled state
- **WHEN** a frame elapses
- **THEN** the posed bound is not recomputed for that mesh

### Requirement: Updated skinned bounds SHALL be visible to all world-AABB consumers

After the posed bound lands on the owning `SceneNode`, every consumer of node bounds SHALL observe the new bounds for the current frame: frustum/visibility queries (octree or the active scene manager), LOD screen-coverage selection, shadow-caster collection, the selection box, and the `Draw Mesh Bounds` debug overlay. If the scene manager caches node placement (octree membership), the bounds update SHALL propagate through the same invalidation path a transform change uses, and SHALL target the scene manager the node is registered with rather than whichever scene happens to be active.

#### Scenario: Deformed mesh is not culled at the frustum edge

- **GIVEN** a skinned mesh whose reference bound sits inside the frustum but whose animated pose extends outside it
- **WHEN** the camera views the bind-pose location edge-on
- **THEN** visibility decisions use the posed bound (the mesh is culled only when the POSED bounds leave the frustum)

#### Scenario: Draw Mesh Bounds tracks the pose

- **GIVEN** the Profiler → Perf → Debug Overlays `Draw Mesh Bounds` overlay is enabled
- **WHEN** a skinned mesh plays an animation
- **THEN** the drawn box tracks the deformed pose rather than remaining at the bind pose
