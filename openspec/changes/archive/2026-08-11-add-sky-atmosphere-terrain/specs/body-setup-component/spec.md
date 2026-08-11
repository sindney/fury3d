# body-setup-component (delta)

## ADDED Requirements

### Requirement: BodySetup SHALL support a heightfield shape sourced from a sibling Terrain

`ShapeType` SHALL gain `HeightField = 3` (appended; existing int-serialized
values unchanged). When shape type is HeightField, `BodySetup` SHALL build a
`JPH::HeightFieldShape` from the sibling `Terrain` component's loaded heights
(same node), sampling at the heightmap resolution with the terrain's world
spacing and height scale, baking the node's world transform per the existing
static-shape convention (matrix math, never piecewise `GetWorld*` reads).
HeightField SHALL be static-only: combining it with dynamic motion SHALL log
a warning and skip body creation. If no sibling `Terrain` with loaded heights
exists, body creation SHALL fail with a logged error and `HasBody` stays
false.

#### Scenario: Character walks on the terrain

- **WHEN** a terrain node has a `BodySetup` with shape type HeightField and a `CharacterController` capsule spawns above it in play mode
- **THEN** the capsule lands on and walks across the terrain surface without falling through or floating above it (height error within one heightmap texel)

#### Scenario: Heightfield without terrain fails cleanly

- **WHEN** a `BodySetup` with shape type HeightField is attached to a node with no `Terrain` component
- **THEN** no body is created, a clear error is logged, and the scene otherwise simulates normally

#### Scenario: Legacy shape values unchanged

- **WHEN** scenes using shape types 0/1/2 (mesh/box/sphere) are loaded
- **THEN** their bodies build exactly as before this change
