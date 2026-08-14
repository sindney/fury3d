## Context

The current editor lives in `engine/Fury/Editor/`. The Scene Inspector is rendered inside `EditorWindows.cpp` (the tree view) and `EditorNodeProperties.cpp` (the right-hand component panel). The Settings window is also rendered in `EditorWindows.cpp`, alongside the Inspector, with section headers produced in the same per-frame `ImGui::BeginChild` pass.

The current Scene Inspector renders one row per node with `ImGui::TreeNodeEx` plus a small `ImGui::Selectable` for the row body. The two controls overlap, so a click on the text selects and a click on the arrow toggles. Because the selectable is roughly the width of the row's text, the rest of the row is dead space. Selection is a single `SceneNode*` held on the editor; tree expansion is a `std::set<SceneNode*>` of open nodes, also on the editor.

Viewport picks land in `EditorPicking.cpp`. Each successful pick calls `Editor::SetSelectedNode` (the single-node API). There is no current path connecting the pick to the inspector's tree-expansion state, and the camera is not framed on a pick -- that is the Scene Inspector's leaf-double-click path's job.

The Settings window is built via a sequence of `ImGui::CollapsingHeader` calls in `EditorWindows.cpp`. Each section's open state is regenerated every frame from a single bit on the editor. There is no persisted state.

The atmosphere shaders are already at `examples/Resource/Shader/Atmosphere/` (they were promoted alongside the SkyAtmosphere implementation). The cloud and moon textures are still in the project's `Terrain/` folder; this change moves them into `examples/Resource/Texture/Sky/` so any project can wire a sky without copying them.

## Goals / Non-Goals

**Goals:**

- Make the Scene Inspector row entirely clickable; the existing arrow keeps its toggle-only behaviour.
- Replace the single selected-node with a selection set carrying an anchor; preserve the existing single-node consumers by routing them to the anchor.
- Wire Ctrl-click / Shift-click multi-select rules and gate the per-row context menu by selection size.
- On viewport pick, walk the picked node's ancestor chain via `SceneNode::GetParent()` and force-open every collapsed ancestor so the node becomes visible. Do NOT scroll, do NOT move the camera.
- Add a per-section collapsed-default + persisted-state for the Settings window.
- Move the cloud / moon textures to `examples/Resource/Texture/Sky/` and document the new layout in a `SKY-README.md`.

**Non-Goals:**

- Reorganising `Projects/outdoor` into subfolders -- this change deliberately leaves the existing project layout alone.
- Migrations of other example projects (`Projects/skin`, `Projects/water`, etc.) -- they have no sky assets and no scattered layout.
- A new dockable inspector window or a new dock layout -- the inspector stays in its current window.
- Replacing the per-row hover buttons or the drag-and-drop reparenting flow.
- Calling `Editor::FrameSelection` from the picker -- the picker only does selection + tree reveal; framing stays on the Scene Inspector's leaf-double-click path.
- New public Lua bindings for the selection set; the existing `selection` accessor returns the array and that is enough for now.

## Decisions

### D1. Scene selection model: `SceneNodeSet` + anchor

Add a typed `SceneNodeSet` to the editor (small struct holding `std::vector<SceneNode*> members` and `SceneNode* anchor`), accessible via `Editor::GetSelectionSet()`. The legacy `Editor::SetSelectedNode(node)` / `Editor::GetSelectedNode()` are kept as thin wrappers that map to the anchor (the wrapper sets members = {node}, anchor = node). Lua continues to call `Editor.GetSelectedNode()` if it wants the single anchor; the new `Editor.GetSelection()` returns the array for scripts that want the full set.

**Why a struct, not a pair of vectors**: the anchor and the members are not separate concepts -- the anchor is by definition one of the members. A single struct with explicit invariants is harder to corrupt than two parallel data structures.

**Why keep the legacy wrapper**: the existing call sites (component panel, gizmo, frame-on-double-click, Lua scripts) read the single-node accessor and there is no behavioural reason to migrate them. The wrapper keeps the diff to the rest of the codebase minimal.

### D2. Per-row selection via `TreeNodeEx` with row hitbox + arrow-only toggle

The row layout uses `ImGui::TreeNodeEx` with `OpenOnArrow` so the arrow is the only toggle. The row hitbox covers the full row via the `SpanAvailWidth` / `SpanAllColumns` flag combination chosen in the implementation; the implementation choice is documented in the code, not pinned by this design.

The arrow drawn by `TreeNodeEx` is the only toggle. The double-click on the row is what triggers frame-selection (the existing path). The hover buttons (`+` and `...`) continue to render at the right edge after the row text, sized to not overlap the trailing blank space.

**Why one control, not two**: the previous design used a separate `Selectable` overlay to handle Ctrl/Shift modifiers. The new implementation routes modifiers through the row's click handler directly, so a single control is enough.

### D3. Reveal-on-pick: walk parents only, no scroll, no camera

The reveal path lives in `EditorPicking.cpp` (the same function that already sets the selection). After a successful pick resolves to a non-null node, the picker walks the picked node's parent chain via `SceneNode::GetParent()` and force-opens every collapsed ancestor through the same `Editor::SetNodeOpen` helper the hover-to-expand-on-drag path uses. The reveal does NOT scroll the inspector and does NOT move the camera -- both belong to other gestures (Scene Inspector leaf-double-click for camera framing).

**Why no scroll on pick**: scrolling on every pick disorients the user when they are navigating the scene. They want the camera pose preserved AND their inspector scroll position preserved; the only thing that should change is which row is highlighted.

**Why no camera frame on pick**: the user is exploring the scene. Surprise reframes break the spatial mental model. The Scene Inspector's leaf-double-click path remains the single source of camera framing.

**Why force-open via `SetNodeOpen` instead of expanding the tree node, then rendering**: the visible tree state is a `std::set<SceneNode*>` on the editor -- the same data the inspector reads to decide whether to render children. Force-opening by adding to the set guarantees the next render pass will show the children.

### D4. Settings window: per-section open flag, persisted

The Settings window's section list is moved to a small registry inside `Editor::Settings` (paired name + default-collapsed flag + current open flag). The renderer iterates the registry and emits one `CollapsingHeader` per section. Each section's open flag is persisted into the editor's existing settings-store (`Editor::LoadSettings` / `Editor::SaveSettings`) keyed by a stable identifier string (`render`, `camera`, `import`, `postprocess`, `sky`).

**Why a registry, not a free function with switch/case**: the engine already loads the editor's settings via a typed-key store; adding a sub-key for each section costs nothing and makes the persistence path uniform.

**Why default-collapsed**: the existing first-open experience is overwhelming -- the user opens the Settings window and sees every toggle the engine ships. Folding them lets the user pick the section they care about without scanning noise.

### D5. Sky defaults: copy textures, document the new home

The cloud / moon textures are copied (not moved) from the project's `Terrain/` folder to `examples/Resource/Texture/Sky/` so the per-project copies still exist for projects that reference them directly. The `setup_terrain_sky_scene.lua` script flips its `SetCloudNoisePath` / `SetMoonTexturePath` calls to the new location (without the `Terrain/` prefix). The atmosphere shader files are already at `examples/Resource/Shader/Atmosphere/` from the original SkyAtmosphere change -- no shader move is needed; the design only documents the canonical location.

**Why copy, not move**: the project-level textures are still useful for the project; future scripts/projects can compare them to the engine defaults. The engine-default files are byte-identical to the originals, so any project can adopt the new path with zero visual diff.

**Why redirect the Lua script rather than the engine**: the engine treats `SetCloudNoisePath` / `SetMoonTexturePath` as user-supplied relative paths that the project picks. The engine currently has no concept of a "default path" for sky assets. The change is to make the project's Lua script adopt the engine-default path as the new canonical location; the engine does not need to special-case the project folder.

### D6. `SKY-README.md` documentation

A short README:

- `examples/Resource/SKY-README.md` -- lists the engine-default files, gives the minimal Lua snippet, documents the override setters. ASCII-only.

**Why a separate sky README**: the sky assets live next to the engine shaders / pipelines, so the README belongs in the same directory. Authors setting up a new project will find it next to the other engine-default assets.

## Risks / Trade-offs

- **[Risk] The `Selectable`-based row layout shifts the render rect by one frame.** -> Mitigation: keep the existing `TreeNodeEx` flags intact; the new code path is exercised by the spec's row hitbox scenarios.
- **[Risk] Multi-select sets introduce a new "anchor" concept that the component panel and gizmo don't read.** -> Mitigation: keep the legacy single-node accessor as a wrapper that returns the anchor; existing consumers do not need to migrate.
- **[Risk] Persisted section state diverges from the section layout when the editor is rebuilt.** -> Mitigation: ignore section keys that are no longer in the registry (the registry drops them on load). New sections default to collapsed.
- **[Risk] The reveal path forces open ancestors the user just collapsed.** -> Mitigation: the path is one-shot per pick request (cleared after the inspector consumes it); the next frame the user's manual collapse is preserved.
- **[Risk] The sky textures are copied (not moved) -- the project's `Terrain/` copies still ship and could drift from the engine defaults.** -> Mitigation: document this in `SKY-README.md`; a future cleanup PR can remove the project copies if the project migrates.

## Migration Plan

1. Land the C++ editor changes (selection set, row hitbox, reveal-on-pick, settings persistence) on a topic branch. Build and run the editor interactively; tests are the manual checks described in the spec scenarios.
2. Land the sky-defaults promotion in a second commit: copy the textures, update `setup_terrain_sky_scene.lua`, add `SKY-README.md`.
3. Roll back is a `git revert` of the relevant commit. The sky commit is independent of the editor commit; revert can be done in either order. The C++ editor commit is functionally additive (the legacy single-node accessor stays), so a revert restores the old behaviour without breaking the rest of the editor.

## Open Questions

- Should the per-row hover buttons (`+` and `...`) be disabled when the selection is multi-select, or only the `+` button? The spec says `+` is disabled and `...` still opens the menu. Confirm this is the intended UX once the multi-select PR is reviewed.
- The persisted section state assumes a stable identifier per section. If the editor allows third-party projects to register new sections, the registration API should also accept a stable identifier; the design notes this but does not implement it.
- The sky textures are copied (not moved) to keep the project original. If a follow-up change wants to migrate the project to the engine-default paths permanently, the project-level copies can be removed in a separate cleanup PR.
