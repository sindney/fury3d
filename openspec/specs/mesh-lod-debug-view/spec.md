# mesh-lod-debug-view

## Purpose

Editor visualization for the runtime LOD selection: tints each mesh draw with a deterministic per-LOD color when an editor toggle is on, so artists can see which LODs the renderer is actually picking for a given camera position without having to inspect `MeshRender::GetActiveLod()` programmatically.

## Requirements

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

When `IsSwitchOn(LOD_DEBUG_COLORS)` is false, the pipeline SHALL explicitly reset `lod_debug_color` to `vec4(0)` on the bound shader. OpenGL program objects retain their last-set uniform values indefinitely, so a "do nothing" here would leave the previous frame's green baked into the program. The shader's `lod_debug_color.a > 0.0` gate treats alpha = 0 as "no override" and passes the diffuse through unchanged.

#### Scenario: Shader receives the LOD color when on

- **WHEN** the switch is on and a `MeshRender` reports active LOD 2
- **THEN** the bound shader has `lod_debug_color == (1.0, 0.0, 0.0, 1.0)` (red) at the point of `glDrawElements`

#### Scenario: Uniform is reset to vec4(0) when switch is off

- **WHEN** the switch is off
- **THEN** the bound shader has `lod_debug_color == (0.0, 0.0, 0.0, 0.0)` at the point of `glDrawElements`
- **AND** the diffuse pass-through is unaffected (the shader's alpha > 0 gate skips the tint)

#### Scenario: MeshRender without a chain stays green

- **WHEN** the switch is on and a `MeshRender` has `GetMesh()->GetLodCount() == 1`
- **THEN** the active LOD is `0` (per the `mesh-lod` spec)
- **AND** the draw is tinted green

### Requirement: The bound static-mesh and skinned-mesh shaders SHALL honour `lod_debug_color`

The shaders used for static meshes and skinned meshes (looked up by `pass->GetShader(...)` in `DrawUnit`) SHALL accept a `vec4 lod_debug_color` uniform, gated on the GLSL preprocessor define `WITH_EDITOR`. When the editor build is active and the uniform is set with alpha > 0, the fragment output SHALL be the diffuse multiplied by `lod_debug_color.rgb`. When the alpha is 0 OR the build is headless (`WITH_EDITOR` undefined and the uniform removed), the shader SHALL fall back to its current shading. The `WITH_EDITOR` define is injected by `Shader::Compile` when the C++ build flag is set, so headless production shaders never see the uniform at all.

#### Scenario: Switch off produces the unchanged fragment

- **WHEN** the editor build does not set `lod_debug_color` on the shader (alpha = 0)
- **THEN** the fragment shader output equals its pre-change value

#### Scenario: Switch on with red produces a red fragment

- **WHEN** `lod_debug_color = vec4(1.0, 0.0, 0.0, 1.0)`
- **THEN** the fragment output's RGB equals `diffuse.rgb * (1.0, 0.0, 0.0)` regardless of the material's diffuse color or texture

#### Scenario: Headless build omits the uniform

- **WHEN** the engine is built with `WITH_EDITOR=OFF`
- **THEN** the GLSL source has no `lod_debug_color` declaration or override branch (gated by `#ifdef WITH_EDITOR`)
- **AND** the runtime never queries a uniform location for `lod_debug_color`

### Requirement: The editor SHALL provide a "LOD Debug Colors" toggle in the Profiler FPS tab

Inside `EditorWindows.cpp::RenderProfilerFpsTab`, the editor SHALL render a **Debug Overlays** multi-select combo (`ImGui::BeginCombo` with `ImGuiSelectableFlags_DontClosePopups`) that includes `LOD Debug Colors` as one of the entries alongside `Draw Light Bounds`, `Draw Mesh Bounds`, `Draw Custom Bounds`, and `Draw OcTree Bounds`. The combo's preview text SHALL show `(none)` when nothing is selected, the single entry name when exactly one is selected, or `N overlays selected` otherwise. The toggle's static state SHALL default to `false`. Toggling the entry calls `Pipeline::Active->SetSwitch(PipelineSwitch::LOD_DEBUG_COLORS, lod_debug_on)`.

When `WITH_EDITOR` is OFF, neither the combo nor the toggle exist.

#### Scenario: Toggling the entry flips the engine switch

- **WHEN** the user clicks `LOD Debug Colors` in the Debug Overlays combo
- **THEN** `Pipeline::Active->IsSwitchOn(PipelineSwitch::LOD_DEBUG_COLORS)` returns the new value on the next frame

#### Scenario: Combo preview reflects selection state

- **WHEN** zero overlays are selected
- **THEN** the combo's preview text reads `(none)`

- **WHEN** exactly one overlay is selected
- **THEN** the preview text reads that overlay's name (e.g. `Draw Light Bounds`)

- **WHEN** multiple overlays are selected
- **THEN** the preview text reads `N overlays selected` where N is the selection count

#### Scenario: Default state is off

- **WHEN** the editor starts up with no prior session
- **THEN** `lod_debug_on` defaults to `false`
- **AND** `LOD_DEBUG_COLORS` is off in the pipeline (no green tint in the viewport)