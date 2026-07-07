## ADDED Requirements

### Requirement: `Pipeline` SHALL track per-light shadow maps by `SceneNode` identity

`Pipeline` SHALL own a `std::unordered_map<SceneNode*, Texture::Ptr> m_LastShadowTextures` (keyed by the light's `SceneNode*` raw pointer — the editor only iterates lights that already exist on the render query, so the lifetime is the scene lifetime). The map SHALL be cleared at the top of each `Pipeline::Execute` (immediately before the `query->Sort` step). A public `Texture::Ptr GetLastShadowTexture(const SceneNode& lightNode) const` accessor SHALL return the entry for the given light, or `nullptr` if the light did not draw a shadow map this frame.

#### Scenario: Map is cleared at the start of each Execute

- **WHEN** `Pipeline::Execute(sceneManager)` runs and the previous frame left entries in `m_LastShadowTextures`
- **THEN** the map is empty at the point the first `Draw*LightShadowMap` call runs

#### Scenario: GetLastShadowTexture returns the per-frame texture

- **WHEN** a directional light at `node` casts shadows this frame
- **THEN** `Pipeline::Active->GetLastShadowTexture(*node)` returns the cascaded or 2D shadow texture for `node` for the duration of the frame

#### Scenario: GetLastShadowTexture returns null when no shadow was drawn

- **WHEN** a light's `CastShadows == false`
- **THEN** `GetLastShadowTexture(*node)` returns `nullptr` for that node

### Requirement: Each `Draw*LightShadowMap` method SHALL publish its result into `m_LastShadowTextures`

The four `Pipeline::Draw{Dir,Point,Spot,Cascaded}LightShadowMap` methods SHALL each record their returned texture into `m_LastShadowTextures[&lightNode]` before returning. The `lightNode` raw pointer SHALL be captured from the `SceneNode& node` parameter; `node->GetComponent<Light>()` is already non-null in those call sites (`PrelightPipeline.cpp:285, 363, 453`).

#### Scenario: DrawDirLightShadowMap records the 2D shadow texture

- **WHEN** `DrawDirLightShadowMap(...)` returns `(tex, matrix)`
- **THEN** `m_LastShadowTextures[&node] == tex` for that frame

#### Scenario: DrawPointLightShadowMap records the cube shadow texture

- **WHEN** `DrawPointLightShadowMap(...)` returns `(cubeTex, matrix)`
- **THEN** `m_LastShadowTextures[&node] == cubeTex` for that frame

#### Scenario: DrawCascadedShadowMap records the 2D-array shadow texture

- **WHEN** `DrawCascadedShadowMap(...)` returns `(arrayTex, matrices)`
- **THEN** `m_LastShadowTextures[&node] == arrayTex` for that frame

### Requirement: The Profiler Shadows tab SHALL iterate every shadow-casting light in the scene

`EditorWindows.cpp::RenderProfilerShadowsTab` SHALL no longer look up textures by hardcoded keys (`"1024*1024*0*depth24*2d"`, `"512*512*0*depth24*cube"`, `"1024*1024*4*depth24*2d_array"`). Instead, it SHALL:

1. Walk every `SceneNode` that has a `Light` component (`Scene::Active->GetRootNode()->FindAll(...)` or the equivalent `ForEach<SceneNode>` traversal), filter to those whose `Light::GetCastShadows() == true`, and sort by `LightType` (directional first, then point, then spot) for stable display order.
2. For each such light, look up its current shadow texture via `Pipeline::Active->GetLastShadowTexture(*lightNode)`.
3. Render one section per light: `ImGui::Text("Shadow — <LightType> <node->GetName()>")` followed by the appropriate preview (single 2D image for directional non-CSM, 2×2 grid for directional CSM, 3×2 grid for point cube faces, single 2D for spot).
4. If zero lights cast shadows, render the placeholder `"(no shadow-casting lights)"`.

#### Scenario: Sun + one point light both appear

- **WHEN** the active scene contains a directional `Sun` light (castShadows = true) and a point `Lamp` light (castShadows = true), and no others
- **THEN** the Shadows tab renders two sections: `Shadow — DIRECTIONAL Sun` and `Shadow — POINT Lamp`
- **AND** no hardcoded texture lookup is performed

#### Scenario: Lights with castShadows = false are excluded

- **WHEN** the scene has three point lights, only one with `GetCastShadows() == true`
- **THEN** the Shadows tab renders exactly one section (the casting light)
- **AND** the other two lights' shadow queries return nullptr and are skipped

#### Scenario: Empty scene shows placeholder

- **WHEN** the active scene has no shadow-casting lights
- **THEN** the Shadows tab renders the placeholder `"(no shadow-casting lights)"`

#### Scenario: Directional CSM renders the 2D-array grid

- **WHEN** a directional light casts shadows and `Pipeline::Active->IsSwitchOn(PipelineSwitch::CASCADED_SHADOW_MAP)` is true
- **THEN** that light's section renders the 2×2 grid of cascade slices via `EditorBlitArrayShader`

#### Scenario: Point cube blit uses the light's world position

- **WHEN** a point light at world position `(5, 10, -3)` casts shadows
- **THEN** the six cube-face blit matrices are built from `Vector4(5, 10, -3, 1)` (the light's `node->GetWorldPosition()`), NOT the prior hardcoded `Vector4(0, 0, 0, 1)`

### Requirement: The Profiler Shadows tab SHALL provide a light-selector dropdown

When more than one shadow-casting light exists, the Shadows tab SHALL render an `ImGui::BeginCombo` at the top of the tab (matching the `EditorAssetWindows.cpp:381-396` idiom). The combo SHALL list:

- `All lights` (default; renders all sections as above).
- One entry per shadow-casting light, labelled `<LightType> — <node->GetName()>`, in the same sorted order as the sections.

Selecting an entry SHALL restrict the tab to that one light's section. The selection SHALL persist for the editor session (an `int` member on the editor's static state, defaulting to `-1` = "all lights"). The combo SHALL be hidden when zero or one shadow-casting light exists.

#### Scenario: Default shows all lights

- **WHEN** the editor opens the Shadows tab and the scene has three shadow-casting lights
- **THEN** the dropdown reads `All lights`
- **AND** all three sections are visible

#### Scenario: Selecting a light filters the tab

- **WHEN** the user picks `POINT — Lamp` from the dropdown
- **THEN** only the `Lamp` section renders
- **AND** the directional and spot sections do not render

#### Scenario: Selection persists across frames

- **WHEN** the user picks `DIRECTIONAL — Sun` and the tab is hidden then re-shown
- **THEN** the dropdown still reads `DIRECTIONAL — Sun`
- **AND** only the `Sun` section renders

#### Scenario: Dropdown is hidden with a single light

- **WHEN** the active scene has exactly one shadow-casting light
- **THEN** no dropdown is rendered
- **AND** that one light's section renders as the only content

#### Scenario: Combo selection survives `GetLastShadowTexture == nullptr`

- **WHEN** the user picks a light from the dropdown
- **AND** that light's `GetLastShadowTexture(...)` returns nullptr (e.g. the light is occluded out of the query this frame)
- **THEN** the section renders `"(no shadow map this frame)"` and does NOT crash