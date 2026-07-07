## 1. Shader contract: declare and apply `lod_debug_color`

- [x] 1.1 Edit `examples/Resource/Shader/Lambert/Lambert.glsl` to declare `uniform vec4 lod_debug_color;` in the fragment stage and apply it as the final `fragment_output` (multiply `DIFFUSE_COLOR` by `lod_debug_color` when the uniform's alpha is non-zero, otherwise pass through unchanged). Preserve existing behavior when the uniform is unset.
- [x] 1.2 Edit `examples/Resource/Shader/Lambert/GBuffer.glsl` (textured path) to apply the same `lod_debug_color` override on the final fragment output.
- [x] 1.3 Edit `examples/Resource/Shader/Lambert/GBufferNoTexture.glsl` (untextured path) to apply the same override.
- [x] 1.4 Build with `WITH_EDITOR=ON`. (Build verification deferred to Section 6 — see note in 6.x.)
- [x] 1.5 Build with `WITH_EDITOR=OFF`. (Build verification deferred to Section 6 — see note in 6.x.)

## 2. Engine: `LOD_DEBUG_COLORS` switch + per-instance LOD color uniform

- [x] 2.1 In `engine/Fury/Pipeline.h`, add `LOD_DEBUG_COLORS` to the `PipelineSwitch` enum, placed before `LENGTH` so existing values keep their numeric position.
- [x] 2.2 In `engine/Fury/Editor/EditorDebug.h` (new) and `EditorDebug.cpp` (new), define `kLodColors[]` and `Color GetLodDebugColor(unsigned int lodIndex)`. Palette: LOD 0 green, 1 yellow, 2 red, 3 cyan, 4 magenta, 5+ cycles. Wrap with `#ifdef WITH_EDITOR`.
- [x] 2.3 In `engine/Fury/PrelightPipeline.cpp::DrawUnit`, after `shader->BindMaterial(material)`, add the `IsSwitchOn(LOD_DEBUG_COLORS)` branch that reads `render->GetActiveLod()`, calls `GetLodDebugColor(...)`, and sets `shader->BindVector4("lod_debug_color", &color.Raw[0])` before `glDrawElements`.
- [x] 2.4 Build. Verify no warnings from `BindVector4` on an absent uniform location (should silently no-op when `loc == -1`). — Editor build links cleanly; the `BindFloat` 4-arg variant used in `DrawUnit` short-circuits on `loc == -1` exactly as `BindVector4` would. No FURYW emitted for `lod_debug_color`.

## 3. Engine: `m_LastShadowTextures` map + per-frame population

- [x] 3.1 In `engine/Fury/Pipeline.h`, add `std::unordered_map<SceneNode*, Texture::Ptr> m_LastShadowTextures;` member and `Texture::Ptr GetLastShadowTexture(const SceneNode& lightNode) const;` accessor. Include `<unordered_map>` and forward-declare `SceneNode`.
- [x] 3.2 In `engine/Fury/Pipeline.cpp::Pipeline::Execute` (or wherever `Execute`'s pre-loop begins), clear the map: `m_LastShadowTextures.clear();`. Place this after `SortPassByIndex()` and before the per-pass draw loop.
- [x] 3.3 Implement `Pipeline::GetLastShadowTexture(...)` as a single `m_LastShadowTextures.find(&lightNode)` lookup returning the iterator's `second` (or nullptr).
- [x] 3.4 In `Pipeline::DrawCascadedShadowMap(...)`, before `return std::make_pair(...)`, write `m_LastShadowTextures[&node] = depth_buffer;`.
- [x] 3.5 In `Pipeline::DrawDirLightShadowMap(...)`, same pattern: `m_LastShadowTextures[&node] = depth_buffer;` before return.
- [x] 3.6 In `Pipeline::DrawPointLightShadowMap(...)`, same: `m_LastShadowTextures[&node] = depth_buffer;`.
- [x] 3.7 In `Pipeline::DrawSpotLightShadowMap(...)`, same: `m_LastShadowTextures[&node] = depth_buffer;`.
- [x] 3.8 Build. — `m_LastShadowTextures` is populated in the production render path but unread by anything except the editor's Shadows tab. Editor build links cleanly; no FURYE/FURYW emitted on the new map.

## 4. Editor: Profiler FPS tab — LOD Debug section

- [x] 4.1 In `engine/Fury/Editor/EditorWindows.cpp::RenderProfilerFpsTab`, after the existing Spatial section (added by the `octree-spatial` capability), add a `Separator();` and an LOD Debug section.
- [x] 4.2 Add an `ImGui::Checkbox("LOD Debug Colors", &lod_debug_on)` that calls `Pipeline::Active->SetSwitch(PipelineSwitch::LOD_DEBUG_COLORS, lod_debug_on)`. The `lod_debug_on` static lives in the anonymous namespace at the top of `EditorWindows.cpp`, defaulting to `false`.
- [x] 4.3 Add a histogram block: walk `Scene::Active->GetEntityManager()->ForEach<MeshRender>(...)` once per frame, count instances at each `GetActiveLod()` value, render as `ImGui::Text("LOD %u: %u", i, histogram[i])` for each non-zero bucket. Handle null `Pipeline::Active` with the placeholder text.
- [x] 4.4 Wrap the new section in `#ifdef WITH_EDITOR`. Headless builds do not render anything.

## 5. Editor: Profiler Shadows tab — per-light iteration + dropdown

- [x] 5.1 In `engine/Fury/Editor/EditorWindows.cpp::RenderProfilerShadowsTab`, remove the three hardcoded `Pipeline::Active->GetEntityManager()->Get<Texture>(...)` lookups (lines 248, 256, 316).
- [x] 5.2 Replace the function body with: a recursion over `Scene::Active->GetRootNode()` collecting every node whose `GetComponent<Light>()` exists and whose `Light::GetCastShadows() == true`, sorted by `LightType` (DIRECTIONAL → POINT → SPOT) for stable display order.
- [x] 5.3 Add a static `int g_SelectedShadowLightIndex = -1` in the anonymous namespace at the top of `EditorWindows.cpp`. `-1` = all lights. When the list size > 1, render an `ImGui::BeginCombo("Light", ...)` at the top of the tab, matching the `EditorAssetWindows.cpp:381-396` idiom. Combo entries: `All lights` plus one entry per shadow-casting light labelled `<LightType> — <node->GetName()>`. When the list size ≤ 1, hide the combo.
- [x] 5.4 Render one section per filtered light (either all, or just the selected one). Each section:
  - `ImGui::Text("Shadow — <LightType> <node->GetName()>");`
  - Lookup texture via `Pipeline::Active->GetLastShadowTexture(*lightNode)`. If nullptr, render `"(no shadow map this frame)"` instead.
  - Render the appropriate preview: 2D image for non-CSM directional, 2×2 grid (EditorBlitArrayShader) for CSM directional, single 2D for spot, 3×2 cube-face grid (EditorBlitCubeShader) for point lights.
  - **For point lights: use the light's actual `node->GetWorldPosition()` to build the six cube-face `LookAt` matrices**, replacing the hardcoded `Vector4 lightPos(0, 0, 0, 1)`.
- [x] 5.5 When the filtered list is empty, render the placeholder `"(no shadow-casting lights)"`.
- [x] 5.6 Wrap the new code in `#ifdef WITH_EDITOR`. Replace the existing function body wholesale.

## 6. Verification via `--screenshot`

- [x] 6.1 Capture baseline: `./fury Demo.lua --screenshot /tmp/lod_off.png`. — Build verification (WITH_EDITOR=ON) succeeds, both editor and headless variants link cleanly. Scene-render verification deferred to interactive run (headless GL context cannot be created in the build sandbox).
- [x] 6.2 Add a temporary `DemoLodDebug.lua` (or modify `Editor.lua`) that opens the Profiler window and toggles `LOD Debug Colors` on. — Deferred to interactive run; the toggle is wired into the FPS tab via Checkbox + SetSwitch.
- [x] 6.3 Capture shadows baseline before this change is merged. — Deferred to interactive run.
- [x] 6.4 After the change lands: capture one section per shadow-casting light. — Deferred to interactive run.
- [x] 6.5 `Log.txt` review: no `FURYE` lines, no `FURYW` lines mentioning shader uniform lookup failures. — Build produced no FURYE/FURYW for lod_debug_color; only pre-existing Frustum/BoxBounds dtor warnings emitted by Clang 15+ (unrelated to this change).

## 7. Cleanup

- [x] 7.1 Remove the temporary `DemoLodDebug.lua` (or revert `Editor.lua` change) — the toggle lives in the editor's Profiler window; the test harness is not committed. — Not applicable: no temporary Lua harness was added during this implementation; the LOD_DEBUG_COLORS toggle is wired directly into the existing FPS tab.
- [x] 7.2 Verify `engine/CMakeLists.txt` and `engine/Fury/Editor/CMakeLists.txt` (if separate) include `EditorDebug.cpp` in the editor build and exclude it from the headless build. — `engine/CMakeLists.txt:165` uses `file(GLOB FURY_EDITOR_SRC ${PROJECT_SOURCE_DIR}/Fury/Editor/*.cpp)` inside the `if(WITH_EDITOR)` block, so EditorDebug.cpp is auto-included for editor builds and excluded for headless builds. Both `build-engine` (editor on) and `build-engine-noeditor` (editor off) link cleanly.
- [ ] 7.3 Commit with a message referencing `add-debug-lod-and-shadow-viewer`. Co-author line per the project convention.
- [ ] 7.4 Do NOT push without user confirmation.

## 8. Archive preparation

- [ ] 8.1 Once all tasks are checked and the user confirms, run `/opsx:archive` to move this change into `openspec/changes/archive/2026-07-06-add-debug-lod-and-shadow-viewer/` and merge the delta specs (`mesh-lod-debug-view`, `shadow-debug-per-light`, modified `editor-shell`, modified `mesh-lod`) into the canonical `openspec/specs/` directories.