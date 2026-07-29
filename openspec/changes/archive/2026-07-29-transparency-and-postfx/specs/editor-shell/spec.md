# editor-shell (delta)

## ADDED Requirements

### Requirement: Editor window visibility SHALL persist across restarts

The editor SHALL persist every built-in window's visibility flag (Settings, Profiler, Scene Inspector, Node Properties, Console, Content Browser, Viewport, and any future `g_Show*` window) in the `[FuryEditor]` section of `imgui.ini` via the existing `ImGuiSettingsHandler`. On startup the flags SHALL be restored before the first frame so that each visible window is `Begin()`'d every frame — which is what allows ImGui to record and restore that window's dock node and position. `Window → Reset Layout` SHALL keep its existing behavior of closing the Settings and Profiler windows.

#### Scenario: Docked Settings window restores

- **WHEN** the user opens the Settings window, docks it next to the Viewport, quits, and relaunches from the same working directory
- **THEN** the Settings window reopens docked in the same position

#### Scenario: Closed window stays closed

- **WHEN** the Settings window is closed (not visible) when the engine quits
- **THEN** it remains closed after relaunch

#### Scenario: Reset Layout still closes floating windows

- **WHEN** the user clicks `Window → Reset Layout` with Settings visible
- **THEN** Settings and Profiler close, and that visibility state persists on next launch

### Requirement: Viewport toolbar separates overlays from buffer debug views

The viewport toolbar SHALL carry two combos on its right side: a multi-select **overlays** combo (Show Grid, bounds draws, LOD debug colors — independent toggles drawn on top of the scene) and a single-select **debug view** combo (None / SSAO / SSR) that replaces the viewport image with the effect's raw debug output. The gizmo snapping toggle SHALL live in Settings → Editor next to the snap step sizes, not in the toolbar.

#### Scenario: Selecting a debug view

- **WHEN** the user picks SSAO in the toolbar's debug-view combo
- **THEN** the viewport presents the raw AO term (white = unoccluded) until the combo is set back to None, independent of whether SSAO is enabled in the postprocess chain

#### Scenario: Profiler hidden by default

- **WHEN** the editor starts with no persisted window state
- **THEN** the Profiler window is hidden and opens via the Window menu

### Requirement: GBuffer previews SHALL be legible for packed formats

The Profiler's GBuffer tab SHALL blit previews through a shader that forces alpha to 1 and replicates depth to RGB, so packed channels (metallic in `gbuffer_diffuse.a`, roughness in `gbuffer_normal.a`) never blend a preview into invisibility and depth never displays red-only.

#### Scenario: Diffuse preview in HDR

- **WHEN** the HDR pipeline is active and the user opens Profiler → GBuffer
- **THEN** the diffuse preview shows the scene's albedo even though `gbuffer_diffuse.a` holds metallic ≈ 0

