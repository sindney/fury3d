# editor-shell (delta)

## RENAMED Requirements

- FROM: `### Requirement: The editor SHALL ship a Settings window with Camera, Import, and Themes sections`
- TO: `### Requirement: The editor SHALL ship a Settings window with Editor, Import, and Engine sections`

- FROM: `### Requirement: The Node Properties window SHALL host the gizmo mode + snap controls`
- TO: `### Requirement: The Viewport top toolbar SHALL host the gizmo mode + snap controls`

## MODIFIED Requirements

### Requirement: The editor SHALL ship a Profiler window combining FPS, GBuffer, and shadow-buffer debug

The standalone `Profiler`, `GBuffer`, and `Shadow Buffers` windows previously emitted by `Gui::ShowDefault` SHALL be consolidated into one editor-owned **Profiler** window with three tabs (`ImGui::BeginTabBar` + `BeginTabItem`):

- **Perf** — the existing `PlotVar`-based FPS graph plus CPU/GPU memory readouts plus drawcall / triangle / mesh / light counts plus the **LOD Debug** section (added by the `mesh-lod-debug-view` capability: live histogram of `MeshRender::GetActiveLod()` counts) plus the OcTree Spatial readout (added by the `octree-spatial` capability). The tab is named `Perf` (not `FPS`) because it carries performance counters and debug sections well beyond the FPS graph. The **Debug Overlays multi-select combo formerly in this tab is removed** — it now lives in the Viewport top toolbar (see the `editor-viewport-window` capability); the `LOD Debug Colors` toggle moves with it.
- **GBuffer** — the depth/normal/diffuse/light texture previews currently in `Gui.cpp`.
- **Shadows** — the **Use Cascaded Shadow Maps** checkbox plus one preview section **per shadow-casting light** in the active scene, populated by iterating every `Light` with `GetCastShadows() == true` and pulling the per-frame shadow texture via `Pipeline::GetLastShadowTexture(*lightNode)`. When more than one shadow-casting light exists, a `BeginCombo` dropdown at the top of the tab lets the user focus on one light (the first shadow-casting light is selected by default; there is no "All lights" entry). See the `shadow-debug-per-light` capability for full behavior.

The window SHALL be hidden by default and toggled from `Window → Profiler`.

#### Scenario: Profiler window has three tabs

- **WHEN** the user clicks `Window → Profiler`
- **THEN** a window titled `Profiler` is visible
- **AND** the window contains a tab bar with `Perf`, `GBuffer`, and `Shadows` tabs in that order

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

#### Scenario: Perf tab shows the LOD Debug section without the overlays combo

- **WHEN** the Profiler window is visible and the user clicks the `Perf` tab
- **THEN** the LOD Debug section renders after the Spatial section with its per-LOD histogram
- **AND** no `Debug Overlays` combo is rendered in the tab

### Requirement: The Viewport top toolbar SHALL host the gizmo mode + snap controls

The gizmo mode + snap controls SHALL render in the Viewport window's top toolbar (see the `editor-viewport-window` capability), NOT in the Node Properties window. The Node Properties window SHALL NOT render a "Gizmo" section. The toolbar's left group SHALL contain:

1. A 3-button row (Translate / Rotate / Scale) implemented as `ImGui::RadioButton` driven by `g_GizmoOp`.
2. A single `Snap` checkbox driven by `g_SnapEnabled`. The snap-step sizes (`g_SnapTranslate`, `g_SnapRotate`, `g_SnapScale`) SHALL NOT live in the toolbar — they SHALL render as three `DragFloat` widgets in the Settings → Editor section instead (visible regardless of the Snap toggle state).

The gizmo SHALL always operate in world space. The Local/World concept is NOT surfaced as a toolbar toggle — surfacing it produced confusing UX (SCALE silently forced LOCAL; LOCAL TRANSLATE/ROTATE drags along the node's rotated axes which most users don't expect by default). The persisted `g_GizmoSpace` value is retained for forward-compat with imgui.ini files and is exposed through `Editor::SetGizmoSpace` for scripts that want to opt in.

The "Node" section of the Node Properties window SHALL render a Local / World radio at its top that switches the position/rotation/scale read-out:

- **Local** (default) — the position/rotation/scale widgets are bound to `node->GetLocal*` / `node->SetLocal*` (the canonical state).
- **World** — the position/rotation/scale widgets are bound to `node->GetWorld*` and rendered read-only (`ImGui::BeginDisabled` / `EndDisabled`). Editing world transforms when a parent has non-uniform scale produces shear that the local TRS slot cannot represent, so the cleanest UX is to expose World as inspect-only.

The toolbar's gizmo group SHALL render whenever the Viewport window is visible, independent of selection (the gizmo doesn't appear without a selection, but the user can still configure mode / snap ahead of selecting). The Node section's Local/World radio MAY only render when a node is selected — it edits node-bound state.

#### Scenario: Mode buttons reflect and update state

- **WHEN** the user clicks the `Rotate` radio button in the Viewport toolbar
- **THEN** `g_GizmoOp == ImGuizmo::ROTATE` on the next frame
- **AND** the active gizmo (if a node is selected) renders rotation handles

#### Scenario: Snap-step widgets live in Settings

- **WHEN** the user opens Settings → Editor
- **THEN** three snap-step `DragFloat` widgets (Translate / Rotate / Scale Step) render in the section
- **AND** editing them updates `g_SnapTranslate` / `g_SnapRotate` / `g_SnapScale` for subsequent gizmo drags

#### Scenario: Gizmo mode persists across selections

- **WHEN** the user selects node A, picks ROTATE in the toolbar, then selects node B
- **THEN** the toolbar still shows ROTATE
- **AND** the gizmo on node B is in ROTATE mode

#### Scenario: Node Properties has no Gizmo section

- **WHEN** the Node Properties window renders with any selection state
- **THEN** no "Gizmo" `CollapsingHeader` is present

#### Scenario: Node section Local/World toggle switches the readout

- **WHEN** a node is selected with `Local` chosen in the Node section
- **THEN** the position/rotation/scale widgets show `node->GetLocal*` and edits commit through `SetLocal*`

### Requirement: The editor SHALL ship a Settings window with Editor, Import, and Engine sections

The Settings window SHALL be hidden by default and is opened via `File → Settings`. When visible, the window SHALL render three collapsible sections (`ImGui::CollapsingHeader`) in this order:

- **Editor** — the project-supplied camera controls (sliders for move speed and mouse sensitivity via `Editor::SetCameraSettings(...)`, or the `"(no camera settings registered)"` placeholder), the `Show Grid` toggle (see the `editor-reference-grid` capability), the gizmo snap-step sizes (three `DragFloat` widgets for translate / rotate / scale step — the `Snap` toggle itself lives in the Viewport toolbar), and the theme `Combo` listing all 12 themes from the registry (`Dark`, `Forest Green`, `Amethyst`, `Sapphire`, `AmberYellow`, `Dracula`, `CatppuccinMocha`, `GruvboxHard`, `CrimsonVesuvius`, `RoseQuartz`, `Cyberpunk`, `PaperAndInk`). Selecting a theme SHALL apply the corresponding `Setup<Theme>Style()` function immediately and persist the choice across restarts.
- **Import** — toggles for import-time options: the `auto_default_sun` flag and the `auto_scale_detect` flag (see the `import-unit-scale` capability), each as a checkbox, plus the `Normal Gen` combo (`Smooth (default)` / `Flat`) backed by the `normals_smooth` flag (see the `gltf-importer` capability).
- **Engine** — the read-only unit/coordinate info and the **Cascaded Shadow Map (CSM)** checkbox driving `PipelineSwitch::CASCADED_SHADOW_MAP`.

**Only the Editor section SHALL carry `ImGuiTreeNodeFlags_DefaultOpen`**; the Import and Engine sections SHALL render collapsed on first use (no prior imgui.ini state). Section open-state thereafter follows ImGui's normal persisted tree state.

The selected theme SHALL be persisted via an ImGui custom settings handler (`ImGui::AddSettingsHandler`) so it ends up in the same `imgui.ini` file as window layout / open-state. On startup the editor SHALL apply the persisted theme before rendering any window.

#### Scenario: Settings window opens via File menu

- **WHEN** the user clicks `File → Settings`
- **THEN** the Settings window becomes visible (floating, by default)
- **AND** clicking it again toggles it closed

#### Scenario: Only Editor is expanded on first use

- **WHEN** the Settings window opens with no prior persisted imgui.ini state
- **THEN** the Editor section is expanded
- **AND** the Import and Engine sections are collapsed

#### Scenario: Themes section applies styles immediately

- **WHEN** the user opens Settings and selects `Forest Green` from the theme combo
- **THEN** the next frame's UI uses the Forest Green palette (`ImGuiCol_WindowBg ≈ {0.06,0.09,0.06,1.0}`)
- **AND** the selection is stored in `imgui.ini`

#### Scenario: Theme persists across restart

- **WHEN** the user picks `Dracula` and restarts the engine
- **THEN** the next launch starts up rendered with the Dracula palette before any window draws

## ADDED Requirements

### Requirement: The Content Browser SHALL filter its tile grid by asset type and fuzzy name search

The Content Browser SHALL render a filter toolbar row above the tile grid containing:

- A **type filter** combo with one entry per asset category the browser enumerates (`All`, `Mesh`, `Material`, `Texture`, `AnimationClip`). Selecting a category SHALL restrict the grid to tiles of that type; `All` SHALL show every category.
- A **text filter** input with a hint (`Search…`). While non-empty, the grid SHALL be restricted to tiles whose asset name matches the text by case-insensitive subsequence (fuzzy) match — every character of the filter text appears in the name in order, not necessarily contiguously.

The two filters SHALL compose (a tile must pass both). Filter state SHALL be window-local (no persistence across editor sessions). When the active filters match no assets, the grid area SHALL show the same `"(no assets in active scene)"` placeholder used for an empty scene. Filtering SHALL NOT affect selection state of hidden tiles and SHALL NOT re-order the grid (name sort is preserved).

#### Scenario: Type filter narrows the grid

- **GIVEN** a scene with 3 meshes and 5 materials
- **WHEN** the user picks `Material` in the type filter
- **THEN** exactly 5 tiles render and none of them is a Mesh tile

#### Scenario: Fuzzy text matches non-contiguous subsequences

- **GIVEN** a mesh named `Sponza_Atrium`
- **WHEN** the user types `spz` in the search box
- **THEN** the `Sponza_Atrium` tile remains visible

#### Scenario: Filters compose

- **GIVEN** meshes `Rock_A`, `Rock_B` and a material `Rock_Mat`
- **WHEN** the type filter is `Mesh` and the search text is `rock`
- **THEN** only `Rock_A` and `Rock_B` render

#### Scenario: No matches shows the placeholder

- **WHEN** the search text matches no asset in the active scene
- **THEN** the grid area shows `"(no assets in active scene)"`
