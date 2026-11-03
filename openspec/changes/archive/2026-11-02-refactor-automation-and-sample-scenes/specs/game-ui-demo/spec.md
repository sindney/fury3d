## ADDED Requirements

### Requirement: IslandGame SHALL retain its complete flow on the rebuilt canonical island

The rebuilt `Projects/ocean/ocean_island.bin` SHALL preserve the `DefaultSun`, `Sky`, `Ocean`, `Terrain`, `MainCamera`, and `PlayerSpawn` contracts consumed by `examples/IslandGame.lua`. The main menu, options, pause menu, HUD, FPS spawn, terrain collision, HDR/CSM controls, and persisted settings SHALL behave as specified by the existing demo requirements. Rebuilding the scene SHALL NOT require transient editor nodes or one-off local setup steps.

#### Scenario: Rebuilt island supports menu-to-player flow

- **WHEN** IslandGame boots the rebuilt island and the user activates Start
- **THEN** the main menu displays over the live island before Start
- **AND** the FPS player spawns above the waterline on walkable terrain with the HUD visible

#### Scenario: Rebuilt island supports rendering options

- **WHEN** the user changes HDR, CSM, postprocess, resolution, or other existing options
- **THEN** the current demo behavior and persistence contract remain intact

### Requirement: Demo UI styling SHALL be shared without sharing the IslandGame state machine

The responsive RmlUi theme and appropriate font resources SHALL live in a shared example-resource location usable by both IslandGame and the feature showcase. The feature showcase SHALL provide its own small overlay and callbacks rather than loading IslandGame's main-menu, pause, options, or player state machine. Both demos SHALL resolve the shared resources from their respective scene working directories.

#### Scenario: Feature overlay reuses the shared theme

- **WHEN** FeatureGame and IslandGame initialize their RmlUi documents
- **THEN** both resolve the same responsive shared stylesheet and required font resources
- **AND** FeatureGame displays only its feature/HDR overlay without activating IslandGame menus or player input

#### Scenario: Shared theme remains responsive

- **WHEN** either demo runs at a supported non-1280x720 window size
- **THEN** its documents use the responsive theme without introducing a second fixed-resolution stylesheet copy
