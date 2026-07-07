## MODIFIED Requirements

### Requirement: The editor SHALL ship a Profiler window combining FPS, GBuffer, and shadow-buffer debug

The standalone `Profiler`, `GBuffer`, and `Shadow Buffers` windows previously emitted by `Gui::ShowDefault` SHALL be consolidated into one editor-owned **Profiler** window with three tabs (`ImGui::BeginTabBar` + `BeginTabItem`):

- **FPS** — the existing `PlotVar`-based FPS graph plus CPU/GPU memory readouts plus drawcall / triangle / mesh / light counts plus the existing checkbox toggles (`Draw Light Bounds`, `Draw Mesh Bounds`, `Draw Custom Bounds`, `Use Cascaded Shadow Map`). Also includes the Spatial section (added by the `octree-spatial` capability) and the **LOD Debug** section (added by the `mesh-lod-debug-view` capability: `LOD Debug Colors` toggle + live histogram of `MeshRender::GetActiveLod()` counts).
- **GBuffer** — the depth/normal/diffuse/light texture previews currently in `Gui.cpp`.
- **Shadows** — one preview section **per shadow-casting light** in the active scene, populated by iterating every `Light` with `GetCastShadows() == true` and pulling the per-frame shadow texture via `Pipeline::GetLastShadowTexture(*lightNode)`. When more than one shadow-casting light exists, a `BeginCombo` dropdown at the top of the tab lets the user focus on one light (the `All lights` default shows every section). See the `shadow-debug-per-light` capability for full behavior.

The window SHALL be hidden by default and toggled from `Window → Profiler`.

#### Scenario: Profiler window has three tabs

- **WHEN** the user clicks `Window → Profiler`
- **THEN** a window titled `Profiler` is visible
- **AND** the window contains a tab bar with `FPS`, `GBuffer`, and `Shadows` tabs in that order

#### Scenario: GBuffer tab shows the deferred render targets

- **WHEN** the Profiler window is visible and the user clicks the `GBuffer` tab
- **THEN** the depth, normal, diffuse, and light buffers render as ImGui images
- **AND** the textures match what `View → GBuffer` showed in the prior implementation

#### Scenario: Shadows tab iterates every shadow-casting light

- **WHEN** the Profiler window is visible and the user clicks the `Shadows` tab
- **AND** the active scene contains two shadow-casting lights (one directional `Sun`, one point `Lamp`)
- **THEN** the tab renders two sections — `Shadow — DIRECTIONAL Sun` and `Shadow — POINT Lamp`
- **AND** each section renders the live shadow texture returned by `Pipeline::Active->GetLastShadowTexture(*lightNode)`

#### Scenario: Shadows tab dropdown filters to one light

- **WHEN** the Shadows tab is visible and the user picks `POINT — Lamp` from the dropdown
- **THEN** only the `Lamp` section renders
- **AND** the `Sun` section does not render

#### Scenario: Shadows tab handles no shadow-casting lights

- **WHEN** the Profiler window is visible, the user opens the Shadows tab, and the active scene has no shadow-casting lights
- **THEN** the tab renders the placeholder `"(no shadow-casting lights)"`

#### Scenario: FPS tab shows the LOD Debug section

- **WHEN** the Profiler window is visible and the user clicks the `FPS` tab
- **THEN** the LOD Debug section renders after the Spatial section
- **AND** the section contains a `LOD Debug Colors` checkbox and a per-LOD histogram
- **AND** toggling the checkbox calls `Pipeline::Active->SetSwitch(PipelineSwitch::LOD_DEBUG_COLORS, ...)`