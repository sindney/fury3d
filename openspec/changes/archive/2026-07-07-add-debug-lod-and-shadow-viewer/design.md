## Context

The mesh-LOD runtime (`mesh-lod` capability, merged 2026-07-06) gives every `MeshRender` a per-instance `m_ActiveLod` updated from the camera's screen-coverage of the model's AABB (`MeshRender.cpp:262-340`). The selection is correct but invisible: an artist tuning thresholds has no way to see which LOD is currently picked for a given mesh. The Shadows debug tab in the editor (`EditorWindows.cpp::RenderProfilerShadowsTab`, lines 240-363) is hardcoded: it looks up exactly three shadow textures by string key in the `EntityManager` and, for the cube-shadow blit, builds the six face directions from a hardcoded `Vector4 lightPos(0, 0, 0, 1)` (line 288). Any scene with more than one shadow-casting light shows only the most-recently-drawn light under each fixed key, and a point light whose world position is not the origin produces a wrongly-oriented cube preview.

Two pieces of work belong together because they share the editor's debug-visualization surface, the `PipelineSwitch` enum, and the verification path (engine CLI `--screenshot` mode per `screenshot-debug`). Both are purely additive to the engine API; both are gated behind `WITH_EDITOR`. The implementation must respect two non-obvious constraints:

- The mesh shaders are global GLSL assets loaded from `Resource/Shader/` (or inline in `Shader.cpp`). Adding a `lod_debug_color` uniform means every mesh-shader variant needs the new uniform's declaration; otherwise ImGui-style "set it and the GLSL just ignores unknown uniforms" is **not** the case — GLSL requires declaration. The cheapest contract is: the engine declares the uniform via `Shader::BindVector4("lod_debug_color", ...)`; the shader's `loc` lookup returns `-1` if the uniform is absent, and `glUniform4fv(loc, 1, ...)` on `-1` is silently ignored by GL. Adding a declaration to every mesh-shader variant is straightforward but a separate change to each file.
- The shadow-map-to-light association in the profiler today is "lookup by texture name"; the replacement is "lookup by light `SceneNode*`". The lifetime concern: `SceneNode*` is owned by `Scene::Active`, which outlives the editor's one-frame render pass. Raw pointer keys in the `m_LastShadowTextures` map are safe as long as we clear the map every frame BEFORE the scene is mutated (a `Scene::Clear` mid-frame would dangle, but the pipeline already runs synchronously on the main thread).

## Goals / Non-Goals

**Goals:**

- A `LOD_DEBUG_COLORS` `PipelineSwitch` that tints every mesh draw by the instance's `GetActiveLod()`, using a documented green/yellow/red/cyan/magenta palette that wraps for chains longer than 6.
- A `mesh-lod-debug-view` editor section inside the Profiler FPS tab: a toggle for the switch + a live histogram of `MeshRender::GetActiveLod()` counts.
- A `shadow-debug-per-light` refactor of the Profiler Shadows tab: iterate every `Light` with `GetCastShadows() == true`, pull the per-frame shadow texture from a `Pipeline`-owned `m_LastShadowTextures` map keyed by `SceneNode*`, render one section per light with a dropdown to filter.
- The cube-shader blit uses the light's actual `node->GetWorldPosition()` (no more `(0,0,0,1)`).
- Verification via the existing `--screenshot` mode: `--screenshot /tmp/lod_off.png` and `--screenshot /tmp/lod_on.png` should differ only by the LOD color overlay; the Shadows tab is verifiable by capturing with the Profiler window open and on the Shadows tab.
- All new code lives behind `#ifdef WITH_EDITOR`. Headless builds are byte-equivalent in render output (modulo the LOD color path, which is opt-in).

**Non-Goals:**

- Adding a per-light, per-cascade LOD-for-shader-toggle (no relationship between LOD chain and shadow map).
- Replacing the global `EntityManager` shadow-texture lookup in any path OTHER than the Shadows tab. `EditorBlitCubeShader` / `EditorBlitArrayShader` are scoped to the Shadows tab and are the only consumers; production lighting shaders don't read these textures.
- Serializing the LOD-debug toggle or the Shadows-tab dropdown across editor sessions. v1 keeps them session-local, matching how the existing `draw_light_bounds` / `use_csm` flags are handled (`EditorWindows.cpp:127-167`).
- A "freeze LOD" debug action (locking every MeshRender to LOD N for a frame). That's a separate feature.
- New Lua bindings. The LOD-debug toggle is driven from C++ (`Editor.cpp`), not Lua; `Editor.lua` already opens the Profiler window via `Editor.SetWindowVisible("Profiler", true)`.
- Refactoring the `DrawUnit` pipeline beyond what's required to inject the LOD color uniform.

## Decisions

### Decision 1: Per-instance LOD color via a `lod_debug_color` shader uniform

In `PrelightPipeline::DrawUnit`, after `shader->BindMaterial(material)` and before `glDrawElements`, the pipeline calls:

```cpp
if (m_Switches.test((size_t)PipelineSwitch::LOD_DEBUG_COLORS))
{
    auto lodColor = GetLodDebugColor(render->GetActiveLod());
    shader->BindVector4("lod_debug_color", &lodColor.Raw[0]);
}
```

The shader's fragment stage applies `lod_debug_color` as the final color override (replacing the lit color). When the switch is off, the uniform is not set; `Shader::BindVector4` is the only consumer of the `loc` lookup, and a `loc == -1` is silently skipped.

**Why a uniform and not a material override or a vertex-color path:**

- Material override (`Material::SetDiffuseColor`) would mutate the asset globally — every MeshRender using the same material would inherit the color, defeating the per-LOD distinction. The uniform is per-draw.
- A vertex-color path would require mesh data to carry debug colors, which doesn't survive LOD simplification (meshoptimizer strips vertex attributes).
- The uniform is one line in `DrawUnit`, costs nothing when the switch is off (the `IsSwitchOn` check is a single bitset test), and aligns with how the engine's existing shaders consume per-instance state (`shader->BindMatrix(Matrix4::WORLD_MATRIX, ...)` at `PrelightPipeline.cpp:252`).

**Shader-side contract:** each mesh-shader variant (static-mesh, skinned-mesh, and their texturing variants) declares `uniform vec4 lod_debug_color;` and applies it as the final fragment color. The uniform's default (zero-init) keeps the fragment unchanged because the existing fragment output multiplies `DIFFUSE_COLOR` by `lod_debug_color` only when the latter is non-zero — OR — the shader checks for the uniform's presence via `#ifdef LOD_DEBUG_COLOR` and applies it unconditionally. The shader patch is part of this change; the precise GLSL edit is enumerated in the tasks doc.

**Alternative considered:** use `gl_FragColor = vec4(lod_debug_color.rgb, 1.0)` unconditionally inside an `#ifdef LOD_DEBUG_COLOR_OVERRIDE` block, with the override enabled by the editor's toggle. Rejected — `#ifdef` toggling is a shader recompile, not a runtime switch; the engine doesn't have a per-frame recompile path.

**Alternative considered:** drive the override via a vertex attribute (`gl_VertexID`-derived LOD index, packed as a vertex color). Rejected — it conflicts with vertex-color data the mesh might already carry, and it forces every mesh to grow an extra vertex stream.

### Decision 2: `m_LastShadowTextures` keyed by `SceneNode*`, cleared at the top of `Pipeline::Execute`

```cpp
// Pipeline.h
std::unordered_map<SceneNode*, Texture::Ptr> m_LastShadowTextures;
Texture::Ptr GetLastShadowTexture(const SceneNode& lightNode) const;
```

`Pipeline::Execute` clears the map immediately after `SortPassByIndex()` (before any `Draw*LightShadowMap` call). Each `Draw*LightShadowMap` writes its returned texture into `m_LastShadowTextures[&node]` before `return std::make_pair(...)`.

**Why raw `SceneNode*` keys:**

- `Pipeline::Execute` runs synchronously on the main thread, between the prior frame's editor draw and the current frame's `DrawDebug`. `Scene::Active` cannot be mutated mid-`Execute` — there is no re-entry path that swaps scenes.
- The map's lifetime is one frame: cleared at the start, populated during the shadow-map pass, read by the editor after `Pipeline::Execute` returns and before the next `Execute`. After the next `Execute` starts, the prior frame's entries are gone (cleared). So the prior frame's `SceneNode*` is irrelevant — only the current frame's nodes, which are valid for the duration of the call.
- A `weak_ptr<SceneNode>` key would require `.lock()` per lookup and add a layer of indirection the editor doesn't need.

**Alternative considered:** store a `std::vector<std::pair<SceneNode*, Texture::Ptr>>` and search linearly. Rejected — at most a few shadow-casting lights per scene, but the map is the same code shape, fewer lines, and idiomatic for "small typed lookup" use cases in the engine.

**Alternative considered:** add a `m_LastShadowTexture` field to `Light` itself. Rejected — it conflates component state with frame-scoped render state, would survive scene loads that should drop it, and conflicts with the engine's existing pattern of using `Texture::GetTemporary` for transient per-frame textures (`Pipeline.cpp:360, 470, 544, 634`).

### Decision 3: Cube-shader blit origin = the light's world position

The hardcoded `Vector4 lightPos(0, 0, 0, 1)` at `EditorWindows.cpp:288` becomes `Vector4 lightPos(node->GetWorldPosition().x, node->GetWorldPosition().y, node->GetWorldPosition().z, 1)`. The six `LookAt` calls then build correct view matrices around the light's actual world position, matching what `DrawPointLightShadowMap` already does at `Pipeline.cpp:567-573` (the production render path already uses `lightPos` correctly — the editor's debug preview was the broken one).

**Why fix it here, not earlier:** `DrawPointLightShadowMap` produces a depth cube map oriented around the light's world position; the editor's debug blit must mirror that orientation for the preview to be visually meaningful. The current `(0,0,0,1)` produces a wrong preview for any non-origin point light — but it was never visible because the cube map itself was looked up by the fixed name `"512*512*0*depth24*cube"`, which only ever held one texture (the last-drawn point light's). Once the lookup switches to per-light, the wrong blit becomes user-visible — fixing it is unavoidable.

### Decision 4: Shadows tab iterates `SceneNode`s directly, not `RenderQuery::lightNodes`

`RenderQuery::lightNodes` is frustum-culled (`RenderQuery.cpp:68` includes the `Light::GetCastShadows()` filter). The Shadows tab needs ALL shadow-casting lights so the dropdown can offer a complete list even when a light is currently behind the camera. The Shadows tab walks `Scene::Active->GetRootNode()` recursively, finds every node with a `Light` component, and filters by `GetCastShadows() == true`.

**Why not use `RenderQuery`:** frustum culling excludes lights that are off-screen; the dropdown should still offer those so the user can flip a light on and see its shadow map the next frame. The cost is one recursive walk per frame of a scene that already has hundreds of nodes — measured in microseconds.

**Alternative considered:** cache the shadow-casting light list in `Pipeline` and update it once per frame in `Execute`. Adds a new public API surface (`Pipeline::GetShadowCastingLights()`) that the editor would consume; the same recursion would still run. Simpler to walk in the editor itself.

### Decision 5: Dropdown selection lives in `EditorWindows.cpp`-scope static state, not `imgui.ini`

The Shadows-tab light selector persists for the editor session, not across restarts. Pattern matches `EditorWindows.cpp:127-167` (FPS-tab static flags like `draw_light_bounds`). A new static `int g_SelectedShadowLightIndex = -1` lives in the anonymous namespace at the top of `EditorWindows.cpp`. `-1` = "All lights"; non-negative indices index into the sorted shadow-casting light list.

**Why session-local:** the dropdown's contents change every frame (depends on scene), so persisting across restarts would mean a stale index pointing to a different light (or no light). The cost of re-selecting after a restart is one click; the benefit of any persistence is zero.

**Alternative considered:** persist by light node name in `imgui.ini`. Rejected for the same reason: nodes are renamed when scenes are re-imported, and a stale name in `imgui.ini` produces silent failures.

### Decision 6: Editor-side histogram walks the `EntityManager::ForEach<MeshRender>` once per frame

The LOD Debug section's per-LOD histogram reads `render->GetActiveLod()` for every `MeshRender` in the active scene and increments `histogram[lod]`. Single pass, O(n) where n is the `MeshRender` count (typically <1000). The result is displayed as `ImGui::Text("LOD %u: %u", i, histogram[i])` per non-zero bucket.

**Why not sample or aggregate by node:** the histogram's job is to show the artist where LODs are landing. Aggregating by node would obscure duplicate meshes (10 tanks all at LOD 1 should show "LOD 1: 10", not "LOD 1: 1"). The walk happens in `RenderProfilerFpsTab`, gated by `WITH_EDITOR`.

### Decision 7: Use the existing `--screenshot` mode for verification, no new CLI

The `screenshot-debug` capability already provides `--screenshot <path>` and `--screenshot-frame <N>`. The verification path:

1. `./fury Demo.lua --screenshot /tmp/lod_off.png` with the new toggle off.
2. Toggle on (via editor — or via a temporary script `DemoLodDebug.lua` that calls `Pipeline.SetSwitch(LOD_DEBUG_COLORS, true)` if we add a Lua surface; otherwise the toggle is flipped via Editor.lua opening the Profiler window).
3. `./fury Demo.lua --screenshot /tmp/lod_on.png`.

The two PNGs SHOULD differ exactly by the LOD color overlay. Comparison: read both with `stb_image`-equivalent (or `sips` on macOS), check pixel histograms for the new colors.

For the Shadows tab: capture with the Profiler window open on the Shadows tab via `--screenshot`. The Shadows tab IS captured because the ImGui overlay is rendered before the back-buffer read (per `Engine.cpp:329-341`). Save before/after the refactor; the "after" image should show one section per shadow-casting light with the cube blit oriented correctly.

**No new CLI surface is needed.** The existing `Engine::screenshot_path` + `Engine::screenshot_frame` fields are the verification path; this change does not extend them.

## Risks / Trade-offs

- **Risk: The `lod_debug_color` uniform is added to every mesh-shader variant, but a future shader variant (added by another change) might forget the declaration.** → Mitigation: `Shader::BindVector4` is the only consumer of the `loc` lookup; a `loc == -1` (uniform not declared) silently no-ops in OpenGL. The fragment falls through to its previous behavior, which is the "switch off" path. Worst case: the new variant shows no debug color, but doesn't break. Document the requirement in the shader-source header comments.

- **Risk: The `m_LastShadowTextures` map dangles if `Scene::Active` is swapped mid-frame.** → Mitigation: `Pipeline::Execute` runs synchronously on the main thread; no other thread mutates the scene; no re-entry from `Scene::Clear`. If a future change introduces scene-swap during `Execute`, the map's clear-at-top would happen AFTER the new scene's render, producing wrong lookups for one frame — but the next frame is correct. Not worse than the current `Texture::GetTemporary` lifetime.

- **Risk: Walking `Scene::Active->GetRootNode()` recursively on every frame for the Shadows tab iteration is wasteful when there are 1000 lights and the tab is hidden.** → Mitigation: the iteration happens INSIDE `RenderProfilerShadowsTab`, which only runs when the Profiler window is visible AND the Shadows tab is the active tab. If neither, the cost is zero. The hidden cost when the tab is active is ~1µs per light — orders of magnitude less than the existing GBuffer / shadow previews it triggers.

- **Risk: The new `PipelineSwitch::LOD_DEBUG_COLORS` enum value shifts `LENGTH` and breaks ABI if anyone serializes Pipeline switches to disk.** → Mitigation: the existing `Pipeline::Load` / `Save` paths persist individual switch flags by name (`"cascaded_shadow_map"` etc.) — `LOD_DEBUG_COLORS` is NOT persisted; `IsSwitchOn` defaults to `false` on load, matching the runtime default. The `bitset<LENGTH>` size grows by one bit; that's transparent for on-disk serialization.

- **Trade-off: Histogram recomputes every frame even when the camera is static and the LOD distribution cannot change.** → Acceptable: LOD selection depends on camera position and orientation, both of which the user can change. A "freeze" button is non-goal. The walk is O(n) and n is small.

- **Trade-off: Per-light shadow section rendering allocates six temporary textures per point light per frame (for the cube blit).** → Match the existing pattern at `EditorWindows.cpp:257-262` (six `Texture::GetTemporary` statics). The temp textures are reused; the cost is bounded.

- **Trade-off: The new editor code (`EditorDebug.cpp` or appended into `EditorWindows.cpp`) is included in the editor build only.** Headless builds are unchanged. Documented in the spec via the `WITH_EDITOR` gate.

## Migration Plan

1. **Land shader changes first.** Edit each mesh-shader GLSL file to declare `uniform vec4 lod_debug_color;` and apply it as the final fragment color (using `#ifdef` or runtime check). Verify the engine builds and `Demo.lua` still renders identically with the uniform unset.
2. **Land engine-side (`engine/Fury/Pipeline.{h,cpp}`, `engine/Fury/PrelightPipeline.cpp`):**
   - Add `PipelineSwitch::LOD_DEBUG_COLORS` before `LENGTH`.
   - Add `m_LastShadowTextures` member + `GetLastShadowTexture` accessor.
   - Clear the map at the top of `Pipeline::Execute`.
   - Add the LOD color uniform set in `PrelightPipeline::DrawUnit`.
   - Make each `Draw*LightShadowMap` record its result into the map.
3. **Land editor changes (`engine/Fury/Editor/EditorWindows.cpp`, `engine/Fury/Editor/Editor.cpp`, optional `engine/Fury/Editor/EditorDebug.{h,cpp}`):**
   - Add `kLodColors[]` and `GetLodDebugColor(...)` helper.
   - Add the LOD Debug section to `RenderProfilerFpsTab`.
   - Rewrite `RenderProfilerShadowsTab` to iterate shadow-casting lights + render the dropdown.
4. **Verify:**
   - Build cleanly with `WITH_EDITOR=ON` (default).
   - Build cleanly with `WITH_EDITOR=OFF`.
   - `./fury Demo.lua --screenshot /tmp/baseline.png` — capture baseline.
   - Open the Profiler window in `Editor.lua`, enable `LOD Debug Colors`, capture `/tmp/lod_on.png`.
   - Compare: `lod_on.png` shows the color overlay; `baseline.png` does not.
   - Open the Profiler on the Shadows tab with multiple shadow-casting lights in the scene; capture; verify one section per light with correctly oriented cube blits.
   - Commit; do not push without confirmation.

No deploy, rollback, or migration complexity. Single-PR, single-commit revert if needed.

## Open Questions

- **Q: Should the LOD color override also apply to the lights' own volumetric draws (`PrelightPipeline::DrawPointLight` / `DrawDirLight` / `DrawSpotLight`)?** Current plan: NO — those are light volume meshes, not the lit scene meshes. Coloring them would obscure which mesh is at which LOD. Lights keep their existing `Light::GetColor()` tint. If user feedback later asks for light volumes to also be tinted (e.g. as a "world is at LOD 3" indicator), it's a one-line change in `DrawPointLight` to read the same switch — defer to user feedback.
- **Q: Should the histogram include meshes that have no chain (always LOD 0)?** Current plan: YES — they're at LOD 0 by definition (`mesh-lod` spec: "the source mesh is always LOD 0"). Bucketing them as "LOD 0: N" gives a correct total. The histogram is the truth, no exclusions.
- **Q: When the dropdown filters to one light, should the cube blit use a wider view (so the user can see all 6 faces at once)?** Current plan: KEEP the existing 3×2 face grid; it's compact and matches the pre-refactor layout. If the user wants a single large cube preview, follow-up.
- **Q: Should `LOD_DEBUG_COLORS` survive a scene reload (`File → New` then `Open`)?** Current plan: NO — session-local, matches `draw_light_bounds`. A reload resets the toggle. Acceptable: the toggle is a single click.
- **Q: What's the fallback if the shader's `lod_debug_color` uniform is declared but the fragment stage doesn't use it (e.g. an out-of-tree shader added later)?** The fragment still ignores the uniform — the override just doesn't render. No crash. Document in the shader-side contract that adding a new shader variant requires declaring the uniform.