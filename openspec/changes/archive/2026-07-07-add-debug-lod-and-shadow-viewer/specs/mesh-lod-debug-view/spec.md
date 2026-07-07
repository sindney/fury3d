## ADDED Requirements

### Requirement: `PipelineSwitch::LOD_DEBUG_COLORS` SHALL drive a per-instance LOD color overlay

The engine SHALL add `LOD_DEBUG_COLORS` to the `PipelineSwitch` enum (placed before `LENGTH` so the existing enum values keep their numeric position). When the switch is on, every mesh draw in `PrelightPipeline::DrawUnit` SHALL be tinted with the LOD→color for that instance's active LOD (`MeshRender::GetActiveLod()`). When the switch is off, the mesh draws unchanged.

#### Scenario: Switch off preserves the unaltered draw

- **WHEN** `Pipeline::Active->IsSwitchOn(PipelineSwitch::LOD_DEBUG_COLORS)` returns false
- **THEN** the bound shader runs its standard color path
- **AND** no LOD color uniform is set on the shader before `glDrawElements`

#### Scenario: Switch on tints every LOD-0 mesh green

- **WHEN** the switch is on
- **AND** a `MeshRender` reports `GetActiveLod() == 0`
- **THEN** the draw that frame writes pixels using the LOD-0 color (green)

#### Scenario: Switch on tints deeper LODs with distinct colors

- **WHEN** the switch is on
- **AND** a `MeshRender` reports `GetActiveLod() == 1`
- **THEN** that draw uses the LOD-1 color (yellow)
- **WHEN** the same `MeshRender` reports `GetActiveLod() == 2`
- **THEN** that draw uses the LOD-2 color (red)

#### Scenario: Enum insertion does not change prior values

- **WHEN** the new enum value is added before `LENGTH`
- **THEN** `static_cast<int>(PipelineSwitch::CASCADED_SHADOW_MAP)` retains its prior numeric value
- **AND** `static_cast<int>(PipelineSwitch::OCTREE_BOUNDS)` retains its prior numeric value

### Requirement: The engine SHALL define a deterministic LOD → color LUT

A `kLodColors[]` table in editor code (`engine/Fury/Editor/EditorDebug.cpp` or appended into `EditorWindows.cpp`) SHALL map LOD indices to colors:

| LOD | Color |
| --- | --- |
| 0 | green (≈ `(0.0, 1.0, 0.0, 1.0)`) |
| 1 | yellow (≈ `(1.0, 1.0, 0.0, 1.0)`) |
| 2 | red (≈ `(1.0, 0.0, 0.0, 1.0)`) |
| 3 | cyan (≈ `(0.0, 1.0, 1.0, 1.0)`) |
| 4 | magenta (≈ `(1.0, 0.0, 1.0, 1.0)`) |
| 5+ | cycle (palette repeats) |

A helper `Color GetLodDebugColor(unsigned int lodIndex)` SHALL return `kLodColors[lodIndex % IM_ARRAYSIZE(kLodColors)]`. The function SHALL be available from any editor translation unit that includes the new header.

#### Scenario: LOD 0 maps to green

- **WHEN** `GetLodDebugColor(0)` is called
- **THEN** the returned color's RGB components equal `(0.0, 1.0, 0.0)` (within float epsilon)

#### Scenario: Palette wraps for chains longer than 6

- **WHEN** `GetLodDebugColor(6)` is called
- **THEN** the returned color equals the LOD-0 color (green) — palette index 6 % 6 == 0

### Requirement: `PrelightPipeline::DrawUnit` SHALL apply the LOD color when the switch is on

In `PrelightPipeline::DrawUnit` (after `shader->BindMaterial(material)`, before `glDrawElements`), when `IsSwitchOn(PipelineSwitch::LOD_DEBUG_COLORS)` is true, the pipeline SHALL:

1. Resolve the active LOD for the `MeshRender` (the same call already used to pick `active` mesh).
2. Compute the color via `GetLodDebugColor(activeLod)`.
3. Set a `lod_debug_color` uniform on the bound shader (the engine's existing static-mesh / skinned-mesh shaders SHALL accept this uniform — when unused, it's ignored; see the shader-uniform contract below).

When `IsSwitchOn(LOD_DEBUG_COLORS)` is false, the pipeline SHALL NOT set the uniform.

#### Scenario: Shader receives the LOD color when on

- **WHEN** the switch is on and a `MeshRender` reports active LOD 2
- **THEN** the bound shader has `lod_debug_color == (1.0, 0.0, 0.0, 1.0)` (red) at the point of `glDrawElements`

#### Scenario: Uniform is untouched when switch is off

- **WHEN** the switch is off
- **THEN** `lod_debug_color` on the bound shader is not modified by `DrawUnit`

#### Scenario: MeshRender without a chain stays green

- **WHEN** the switch is on and a `MeshRender` has `GetMesh()->GetLodCount() == 1`
- **THEN** the active LOD is `0` (per the `mesh-lod` spec)
- **AND** the draw is tinted green

### Requirement: The bound static-mesh and skinned-mesh shaders SHALL honour `lod_debug_color`

The shaders used for static meshes and skinned meshes (looked up by `pass->GetShader(...)` in `DrawUnit`) SHALL accept a `vec4 lod_debug_color` uniform. When the uniform is set (and the editor's LOD debug switch is on), the fragment output SHALL be `lod_debug_color.rgb` regardless of the material's `DIFFUSE_COLOR` / textures / lighting. When the uniform is unset / zero / not bound, the shader SHALL fall back to its current shading (uniform missing ⇒ treat as identity override; the runtime sets the uniform to white when the switch is off, preserving pre-change behavior).

#### Scenario: Switch off produces the unchanged fragment

- **WHEN** the editor does not set `lod_debug_color` on the shader
- **THEN** the fragment shader output equals its pre-change value (modulo a `1.0` multiplier on `DIFFUSE_COLOR`, which is a no-op)

#### Scenario: Switch on with red produces a red fragment

- **WHEN** `lod_debug_color = vec4(1.0, 0.0, 0.0, 1.0)`
- **THEN** the fragment output's RGB is `(1.0, 0.0, 0.0)` regardless of the material's diffuse color or texture

### Requirement: The editor SHALL provide a "LOD Debug Colors" toggle and LOD-distribution readout in the Profiler FPS tab

Inside `EditorWindows.cpp::RenderProfilerFpsTab`, after the existing "Spatial" section (added by the `octree-spatial` capability), the editor SHALL render a "LOD Debug" section with:

1. An `ImGui::Checkbox("LOD Debug Colors", &lod_debug_on)` that calls `Pipeline::Active->SetSwitch(PipelineSwitch::LOD_DEBUG_COLORS, lod_debug_on)`. The flag's static state SHALL persist for the editor session.
2. A read-only `ImGui::Text` readout showing the count of `MeshRender` instances currently at each LOD (0..max), summed across all `MeshRender`s in the active scene. The histogram SHALL be recomputed once per frame from a single walk of the `EntityManager`.

When `WITH_EDITOR` is OFF, neither the toggle nor the readout exist.

#### Scenario: Toggling the checkbox flips the engine switch

- **WHEN** the user clicks the `LOD Debug Colors` checkbox in the Profiler FPS tab
- **THEN** `Pipeline::Active->IsSwitchOn(PipelineSwitch::LOD_DEBUG_COLORS)` returns the new value on the next frame

#### Scenario: Histogram reflects active LOD distribution

- **WHEN** the active scene has 12 `MeshRender`s, of which 5 are at LOD 0, 4 at LOD 1, 3 at LOD 2
- **THEN** the readout shows `LOD 0: 5`, `LOD 1: 4`, `LOD 2: 3`, `LOD 3+: 0`

#### Scenario: Section is hidden with no active pipeline

- **WHEN** `Pipeline::Active` is null
- **THEN** the LOD Debug section renders the placeholder `"(no active pipeline)"` and does NOT crash