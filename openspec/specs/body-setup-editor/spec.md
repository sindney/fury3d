# body-setup-editor

## Purpose

The BodySetup editor window: a per-node collision authoring window that
mirrors the mesh editor's two-pane layout on the shared `Editor3DPreview`
machinery, visualizes the collision shape over the render mesh, and generates
simplified collision meshes (via `MeshSimplifier`/meshoptimizer) that are
saved as real scene mesh assets.

## Requirements

### Requirement: The BodySetup editor SHALL use the mesh-editor two-pane layout on the shared 3D preview

The window SHALL open from the BodySetup inspector section, titled
`Body Setup: <node name>`, scoped to that node (one window per node; reopening
focuses the existing window). The left pane SHALL render the node's render
mesh into the shared `Editor3DPreview` render target with orbit/zoom/pan
controls identical to the mesh editor, plus the ground grid. The right pane
SHALL edit every BodySetup field (shape type, motion type, half extents /
radius, collision mesh reference, mass, friction, restitution).

#### Scenario: Open from inspector

- **WHEN** the user clicks "Open Body Setup Editor…" in a node's BodySetup section
- **THEN** a window titled `Body Setup: <node name>` opens showing the render mesh in the 3D pane

#### Scenario: Reopening focuses the existing window

- **WHEN** the BodySetup editor for a node is already open
- **AND** the user clicks "Open Body Setup Editor…" again
- **THEN** no second window opens and the existing one is focused

### Requirement: The 3D pane SHALL visualize the active collision shape

The preview SHALL overlay a wireframe of the effective collision shape
(mesh triangles / box / sphere, in the mesh's local pose) on the render mesh,
updating live as shape type or parameters change.

#### Scenario: Switching to sphere shows the sphere wireframe

- **WHEN** the user changes shape type from `mesh` to `sphere`
- **THEN** the preview draws a wireframe sphere of the current `radius` enclosing the mesh origin
- **AND** editing `radius` rescales the wireframe on the next frame

#### Scenario: Mesh shape wireframe shows referenced collision mesh

- **WHEN** `collisionMesh` references a simplified mesh
- **THEN** the preview draws that mesh's triangles as the collision wireframe

### Requirement: The editor SHALL generate a simplified collision mesh as a scene mesh asset

A "Simplify Collision Mesh…" control SHALL open a dialog with a target
triangle-count reduction (reusing `MeshSimplifier`/meshoptimizer). Confirming
SHALL create a new `Mesh` entity in the scene's `EntityManager` with a unique
name derived from the source mesh (per the asset-unique-naming rules), assign
it to the BodySetup's `collisionMesh`, and mark the scene dirty. The
generated mesh SHALL persist with the scene save and be usable like any mesh
asset (visible in the Content Browser).

#### Scenario: Simplify produces a named mesh asset and wires it up

- **WHEN** the user simplifies the `Rock1` mesh to ~25% triangles and confirms
- **THEN** a new mesh asset (e.g. `Rock1_collision`) exists in the scene
- **AND** the BodySetup's `collisionMesh` references it
- **AND** the preview wireframe switches to the simplified mesh

#### Scenario: Simplified mesh survives a scene round-trip

- **WHEN** the scene is saved after generating a collision mesh and then reloaded
- **THEN** the collision mesh asset is still in the `EntityManager`
- **AND** the BodySetup still references it

### Requirement: Physics simulation stays out of the editor

The BodySetup editor SHALL be an authoring view only: no Jolt bodies SHALL be
created and no simulation SHALL run while it is open (consistent with the
editor-wide simulation-disabled gate).

#### Scenario: Editing fields does not move the node

- **WHEN** the user edits any BodySetup field in the editor window
- **THEN** the node's transform is unchanged by physics
