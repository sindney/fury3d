## ADDED Requirements

### Requirement: Importable source assets SHALL have one canonical sample location

Importable FBX, glTF, and related source models grouped under `examples/Projects/samples` SHALL remain the canonical source assets for the showcase scenes. Showcase projects SHALL reference or reproducibly import these assets without maintaining duplicate source copies. Generated engine scenes, imported textures, and project-specific settings SHALL live with the consuming project rather than being mixed into the source-asset directory.

#### Scenario: Locate a showcase model source

- **WHEN** a developer inspects a tank, fox, or james reference in the feature showcase
- **THEN** the corresponding importable source can be traced to `examples/Projects/samples`
- **AND** no second maintained source copy exists in another example project

### Requirement: The feature showcase SHALL contain the requested rendering and animation features

A maintained feature-showcase scene SHALL include tank geometry, at least one point light, at least one spot light, a directional light with CSM, the outdoor sample's fire and smoke particle content, and both fox and james skeletal meshes. It SHALL provide a stable inspection camera and a saved render-settings block. Its sample entry point SHALL support HDR-off and HDR-on runs and display an RmlUi game UI overlay composed over the 3D scene.

#### Scenario: Headless feature inventory passes

- **WHEN** the feature showcase is inspected through the headless CLI
- **THEN** all required mesh, light, particle, and skeletal-animation content is present
- **AND** the scene loads without a graphics context

#### Scenario: HDR variants render the same fixture

- **WHEN** the sample runs with HDR disabled and enabled
- **THEN** both runs use the same scene content and camera
- **AND** only the declared rendering configuration and associated output metadata differ

#### Scenario: Game UI overlays the rendered scene

- **WHEN** the feature-showcase capture is taken
- **THEN** the RmlUi document is visible above the 3D scene
- **AND** its layout, text, and controls render without corrupting the underlying scene

### Requirement: The canonical island SHALL combine terrain, kraut vegetation, and infinite ocean

A maintained island scene SHALL contain terrain representing an island, kraut tree vegetation placed on that terrain, and the engine's infinite-ocean rendering around it. The terrain SHALL provide the collision and walkable beach spawn required by IslandGame. Vegetation, terrain, ocean, lighting, atmosphere, and render settings SHALL be saved as project content or reproducible project setup rather than transient editor-only state.

#### Scenario: Island content survives round trip

- **WHEN** the island scene is saved and loaded into a fresh scene
- **THEN** terrain, kraut vegetation, ocean configuration, and render settings retain their semantic values
- **AND** no editor-only helper is introduced into the saved hierarchy

#### Scenario: Island renders water to the horizon

- **WHEN** the island sample is captured from its inspection camera
- **THEN** the island terrain and kraut trees are visible
- **AND** ocean rendering surrounds the land without exposing a finite water-plane edge

### Requirement: IslandGame SHALL use the canonical island project

`examples/IslandGame.lua` SHALL boot the canonical island scene and retain the existing main menu, options, pause, HUD, and FPS-player flow. Its RmlUi documents and styles SHALL be organized as reusable project assets so the feature-showcase overlay can share appropriate resources without duplicating them.

#### Scenario: Launch IslandGame

- **WHEN** `examples/IslandGame.lua` starts
- **THEN** it loads the canonical island project and displays the main menu over the live scene
- **AND** Start still enters a walkable FPS-player state

### Requirement: Example cleanup SHALL preserve reachable entry points and unique demonstrations

Example scenes and scripts SHALL be reorganized around the canonical showcases and retained feature-specific samples. Before an existing scene or script is removed, its references and unique demonstration or test coverage SHALL be inventoried and migrated. The user's grouped source assets SHALL NOT be deleted as part of duplicate-scene cleanup, and generated build outputs SHALL NOT be treated as maintained sample content.

#### Scenario: Remove a redundant sample

- **WHEN** a redundant sample is deleted or replaced by a canonical showcase
- **THEN** every repository reference is updated or removed
- **AND** its unique feature coverage is present in a retained sample or automated scenario

#### Scenario: Discover the maintained examples

- **WHEN** a developer reads the examples documentation
- **THEN** the two canonical showcases, their entry points, required assets, and retained feature-specific samples are listed with current paths
