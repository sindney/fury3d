## Why

The Profiler's **Shadows** tab is hardcoded: it looks up exactly three shadow textures by string key (`1024*1024*0*depth24*2d`, `512*512*0*depth24*cube`, `1024*1024*4*depth24*2d_array`) and renders them with a cube blit whose `lightPos` is hardcoded to `(0,0,0,1)` (`EditorWindows.cpp:287-294`). With multiple shadow-casting lights in a scene — one sun + several points/spots — only the last drawn light's shadow map shows up under each fixed-name lookup, and for a point-light cube blit the camera direction is wrong whenever the light isn't at the origin. There is no LOD debug overlay anywhere: the mesh-lod work landed active-LOD selection but no way to see which LOD is currently selected per instance, which is what artists need to tune thresholds against.

This change adds two new editor debug views and keeps both verifiable headlessly via the engine's existing `--screenshot` mode (`openspec/specs/screenshot-debug`).

## What Changes

- Add a **`LOD_DEBUG_COLORS`** `PipelineSwitch` and a per-instance LOD color override applied during `PrelightPipeline::DrawUnit`. When the switch is on, the bound mesh's draw is tinted by a deterministic palette keyed on `MeshRender::GetActiveLod()`: LOD 0 = green, LOD 1 = yellow, LOD 2 = red, LOD 3 = cyan, LOD 4 = magenta, then cycle. A small `Editor.cpp`-side toggle in the **Profiler → FPS** tab flips the switch; the LUT lives in `engine/Fury/Editor/EditorDebug.cpp` so the shader path stays engine-only.
- Add a **Profiler → LOD** debug section (inside the FPS tab, per the precedent set by the Spatial section in `2026-06-26-auto-size-octree-and-debug-view`): counts of `MeshRender`s currently at each LOD (0..N), plus a `Max LOD chain` readout. Lets artists see the live distribution of LOD picks, not just colors.
- Refactor **`RenderProfilerShadowsTab`** in `EditorWindows.cpp` to walk every shadow-casting light in the active scene (filter `Light::GetCastShadows() == true`) and render one section per light. Each section shows the light's name + type, the live shadow map texture for that light, and (for point lights) the six cube-face previews rebuilt with the light's actual world position rather than the hardcoded origin.
- Add a dropdown above the per-light sections (matching the existing `EditorAssetWindows.cpp:381-396` `BeginCombo` idiom used by the LOD dropdown) that lets the user pick which shadow-casting light's debug info to display when more than one exists. "All lights" remains the default.
- The shadow map currently looked up by fixed string keys is replaced: shadow textures are now tracked per-frame in a `Pipeline`-owned `m_LastShadowTextures[SceneNode*]` map (keyed by the light `SceneNode*`), populated by `DrawDirLightShadowMap` / `DrawPointLightShadowMap` / `DrawSpotLightShadowMap` / `DrawCascadedShadowMap` and consumed by the Shadows tab. The map is cleared at the top of each `Pipeline::Execute`.
- All new editor-only behavior sits behind `#ifdef WITH_EDITOR`. Headless builds (`WITH_EDITOR=OFF`) are unchanged.

## Capabilities

### New Capabilities

- `mesh-lod-debug-view`: The LOD color override + per-instance draw tint. Covers the `PipelineSwitch::LOD_DEBUG_COLORS` enum, the deterministic LOD→color LUT, the integration in `PrelightPipeline::DrawUnit`, and the FPS-tab toggle in the editor. The mesh-lod runtime selection (`Mesh::GetLodCount`, `MeshRender::GetActiveLod`) already exists in the `mesh-lod` capability; this capability adds the debug-visualization layer on top.
- `shadow-debug-per-light`: The per-light iteration and dropdown. Covers the `m_LastShadowTextures` map on `Pipeline`, the per-frame population hooks in the four `Draw*LightShadowMap` methods, and the refactored `RenderProfilerShadowsTab` that walks every shadow-casting light and offers an `ImGui::BeginCombo` selector. Replaces the hardcoded `EntityManager::Get<Texture>` lookups.

### Modified Capabilities

- `editor-shell`: The `Profiler → Shadows` tab description is updated from "2D shadow map + cube map + 2D-array shadow-map previews" to "per-light shadow-map previews, one section per shadow-casting light, with a dropdown to focus on one light when several exist". This is a requirements-level change because the tab's content shape changes.
- `mesh-lod`: Add a single requirement that `Mesh::GetLodCount()` and `MeshRender::GetActiveLod()` are stable per-frame accessors suitable for debug reads from the editor. (Already true today; the change is making the contract explicit.)

## Impact

- Code:
  - `engine/Fury/Pipeline.h` — new `PipelineSwitch::LOD_DEBUG_COLORS` enum entry; new `m_LastShadowTextures` map; new accessor `GetLastShadowTexture(const SceneNode& lightNode)`.
  - `engine/Fury/Pipeline.cpp` — clear the map at the top of `Execute` (or via a `Pipeline::ClearShadowMapCache()` virtual); extend the four `Draw*LightShadowMap` methods to write to the map.
  - `engine/Fury/PrelightPipeline.cpp` — in `DrawUnit`, when `IsSwitchOn(LOD_DEBUG_COLORS)`, set the LOD color uniform on the bound shader before `glDrawElements`.
  - `engine/Fury/Editor/EditorWindows.cpp::RenderProfilerShadowsTab` — full rewrite: iterate shadow-casting lights, render per-light section, add the dropdown. Remove the three hardcoded `EntityManager::Get<Texture>` calls and the hardcoded `(0,0,0,1)` cube blit origin.
  - `engine/Fury/Editor/EditorDebug.cpp` (new, or appended into `EditorWindows.cpp`) — the LOD→color LUT, `kLodColors[]` table, and helper `Color GetLodDebugColor(unsigned int lodIndex)`.
  - `engine/Fury/Editor/Editor.cpp` — `LOD_DEBUG_COLORS` toggle in the FPS tab Spatial section (sibling of "Draw OcTree Bounds").
  - `examples/main.cpp` — no change. Existing `--screenshot` mode (`screenshot-debug` spec) is used to verify visually.
- Public C++ API:
  - New enum value `PipelineSwitch::LOD_DEBUG_COLORS` (additive, before `LENGTH`).
  - New `Pipeline::GetLastShadowTexture(const SceneNode&)` returning `Texture::Ptr` (or nullptr when not yet drawn this frame).
  - No Lua-side API surface change. Editors drive the toggles directly via C++; Lua scripts that want to query LOD counts can already walk `Scene::Active->GetEntityManager()` and call `ForEach<MeshRender>` reading `GetActiveLod()`.
- Behavior:
  - Editor only. With `WITH_EDITOR=OFF` or with `LOD_DEBUG_COLORS=false` and no Profiler Shadows tab open, the change has zero observable effect on rendering.
  - The Shadows tab goes from "shows at most one hardcoded shadow map per type" to "shows the live shadow map for every shadow-casting light, correctly positioned for cube blits."
- Verification path:
  - `./fury Demo.lua --screenshot /tmp/lod_off.png` (LOD_DEBUG off, baseline).
  - Add a temporary script or auto-toggle that flips `LOD_DEBUG_COLORS` on, then `./fury Demo.lua --screenshot /tmp/lod_on.png`.
  - Diff the two PNGs; meshes in `lod_on.png` should have a green/yellow/red overlay tint while `lod_off.png` does not.
  - For shadows: open the Shadows tab (cannot be tested via screenshot since it lives inside the ImGui window and the engine captures the post-pass ImGui overlay — see "Verification limits" in design.md), so the shadow refactor is verified by code review + a small screenshot baseline showing the existing (pre-refactor) shadow-previews still match the post-refactor "first light" view.

## Verification limits

The Shadows tab is rendered by ImGui on top of the final composite, and the screenshot path captures the GL back-buffer after the ImGui pass — so the Shadows tab IS visible in screenshots if the Profiler window is open and on the Shadows tab. The intended verification:

1. `git log` of the existing Profiler Shadows tab pre-change → save `/tmp/shadows_before.png` via `Editor.lua` opening the Profiler + Shadows tab + `--screenshot`.
2. Apply the change; same script, same camera, same scene.
3. Save `/tmp/shadows_after.png` — should show per-light sections with correctly positioned cube blits.
4. Visual diff for "does each shadow-casting light in the scene appear exactly once, with the cube faces oriented around its world position".

The LOD color overlay is captured directly in the scene render, so it's trivially verifiable via two screenshots with the toggle on/off.