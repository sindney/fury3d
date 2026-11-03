# game-ui-demo Specification

## Purpose
TBD - created by archiving change add-rmlui-game-ui. Update Purpose after archive.
## Requirements
### Requirement: The demo SHALL boot into the island scene behind a main menu

Running the demo bootstrap (an `examples/IslandGame.lua` in the spirit of `Player.lua`) SHALL load `Projects/ocean/ocean_island.bin`, present a slow cinematic drift camera over the island, and show the main menu document with Start / Options / Exit entries and a free cursor. Start SHALL hide the menu, spawn/enable the FPS player, and grab+capture the cursor. Exit SHALL close the window via `Window.Close` (never `os.exit`).

#### Scenario: Boot to menu

- **WHEN** the demo launches
- **THEN** the island scene renders behind a styled main menu
- **AND** the camera drifts cinematically, the cursor is free, and no player input is active

#### Scenario: Start enters the game

- **WHEN** the user activates Start
- **THEN** the menu hides, the FPS player spawns at the beach spawn point
- **AND** the cursor is grabbed and hidden so mouse look works

### Requirement: The demo SHALL provide an ESC pause menu

In game, pressing ESC SHALL release the cursor, cut player input, and show the pause menu with Continue / Options / Exit to Main Menu / Exit to Desktop. Continue SHALL return to the grabbed-cursor game state. Exit to Main Menu SHALL disable the player, restore the drift camera, and show the main menu. Exit to Desktop SHALL close the window. The engine has no time-scale, so the living world (ocean, sky) MAY keep animating while paused; the player state SHALL NOT change while paused.

#### Scenario: Pause and continue

- **WHEN** the user presses ESC in game, then activates Continue
- **THEN** the cursor is released at pause and re-grabbed on continue
- **AND** the player position is unchanged across the pause

#### Scenario: Exit to main menu and re-enter

- **WHEN** the user pauses, chooses Exit to Main Menu, then Start again
- **THEN** the demo returns to the drift-camera menu state and can re-enter the game cleanly

### Requirement: The demo SHALL have an FPS player that walks the island

The player SHALL be a 1.8 m Jolt capsule (`CharacterController`, no visual mesh) with camera at ~1.7 m eye height, WASD camera-relative movement, Shift run, Space jump, gravity, and collision against the terrain heightfield statics already in `ocean_island.bin`, spawned on the beach above the 400 cm waterline. Mouse look SHALL use a grabbed cursor with per-frame delta (yaw on the body, pitch clamped on the camera, no drag required) and a configurable sensitivity.

#### Scenario: Walk the beach

- **WHEN** the user moves with WASD and looks with the mouse
- **THEN** the player walks/runs/jumps on the terrain with correct ground collision
- **AND** the view yaws and pitches with the mouse without any button held

#### Scenario: Spawn is dry

- **WHEN** the player spawns
- **THEN** the spawn point is above the waterline and the player does not fall through or start underwater

### Requirement: The options menu SHALL expose real engine settings with persistence

One shared options document SHALL be reachable from both the main and pause menus, SHALL apply settings live, and SHALL persist them to a plain-text key=value file (Lua `io`, `Projects/ocean/game_settings.cfg`) that is loaded at boot. Minimum set: fps cap (60/90/144/uncapped), vsync (on/off), windowed resolution (1280x720 / 1920x1080 / 2560x1440 / 3840x2160 via `setSize` with camera-aspect + render-surface update), post-process master (on/off), SSAO (on/off), SSR (on/off, including ocean SSR), FXAA (on/off), shadows CSM (on/off), shadow quality (map size), shadow far, FOV (slider), mouse sensitivity (slider). The engine's lack of planar reflections SHALL be represented by the SSR toggles; fullscreen SHALL NOT be offered (deferred: window recreate loses the GL context).

#### Scenario: Change fps cap live

- **WHEN** the user sets the fps cap to 60 in Options
- **THEN** the window's frame limit updates without restart
- **AND** the HUD FPS counter settles at the new cap
- **AND** the setting is present in the settings cfg file after leaving Options

#### Scenario: Change resolution live

- **WHEN** the user picks 2560x1440
- **THEN** the window resizes, the camera aspect and render surface track it, and the scene renders undistorted

#### Scenario: Settings survive a restart

- **WHEN** the user changes settings, exits, and relaunches the demo
- **THEN** the persisted settings are applied at boot and shown as the current values in Options

### Requirement: The demo SHALL show a minimal in-game HUD

In game, the HUD document SHALL show a center crosshair and an FPS counter, and SHALL be hidden in menus.

#### Scenario: HUD appears in game only

- **WHEN** the user starts the game
- **THEN** the crosshair and FPS counter are visible
- **AND** both are hidden again when the pause or main menu opens

### Requirement: Menu styling SHALL follow a simple modern AAA direction

Documents SHALL share one RCSS stylesheet: dark translucent panels over the live scene, a thin accent bar, one clean sans font face (shipped with the demo), uppercase labels, hover highlight and pressed states via pseudo-classes, and short fade/slide transitions on menu open/close via RCSS animations. Panels needing borders MAY use ninepatch; the skin SHALL NOT depend on heavy bitmap art.

#### Scenario: Visual pass

- **WHEN** a `gui shot` screenshot of the main menu and options panel is captured
- **THEN** the menus show the shared style (translucent dark panels, accent bar, hover state demonstrable on a captured `mouseover` frame)

### Requirement: The demo flow SHALL be verifiable through the gui CLI

All demo documents SHALL load and behave under `furye-cli gui` subcommands: the menu structure SHALL be dumpable via `gui tree`, element layout/style via `gui inspect`, menu transitions (e.g. clicking Start, opening Options) SHALL be drivable via `gui event`, and `gui shot` SHALL produce representative screenshots of every menu state and the HUD.

#### Scenario: Agent-driven menu walk

- **WHEN** an agent runs `gui tree`, `gui event '#start' click`, `gui tree` on the demo
- **THEN** the second tree shows the in-game state (HUD visible, menu hidden)
- **AND** `gui shot` images exist for main menu, options, pause menu, and HUD

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

