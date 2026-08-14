## Why

The Scene Inspector and the Engine Settings window each have friction that gets in the way of day-to-day authoring:

- The Scene Inspector's tree rows only register clicks on the visible text, leaving the rest of the row dead space; there is no way to box-select or Ctrl/Shift-click multiple nodes for batch operations; and right-click context menu items ("Add Child", "Duplicate", "Rename") silently apply to whichever single node happened to be hovered, which is confusing when several nodes are highlighted.
- Picking a node in the viewport sets the editor selection, but the Inspector tree does not make the selection visible -- if the selected node is inside a collapsed subtree, the user sees nothing change and has to hunt through the tree by hand.
- The Engine Settings window opens with every section already expanded, so the first frame is an overwhelming dashboard of toggles; users want a calmer opening state.

This change makes the everyday-editor flow tighter.

## What Changes

- **Scene Inspector row hitbox.** Clicking anywhere on a row (the full row, not just the text label) selects that node. The tree-node arrow stays the only expand/collapse trigger.
- **Multi-select in the Scene Inspector.** Ctrl-click toggles a node in the selection; Shift-click selects a contiguous range between the last anchor and the clicked node. The right-click context menu only enables the **Delete** entry when more than one node is selected; the other entries (`Add Child`, `Duplicate`, `Rename`) are disabled and greyed out (with tooltips explaining why) when the selection is multi. The editor's "selected node" property is replaced with a `selected_nodes` set; existing single-node consumers (component panel, gizmo, frame-on-double-click) continue to use the anchor (the most recently clicked non-toggle node).
- **Auto-expand on viewport pick.** When the user picks a node in the viewport, the Scene Inspector walks the node's ancestor chain and force-opens every collapsed ancestor so the selected node becomes visible in the tree. The camera pose is preserved -- picking does not frame the camera or scroll the row; the existing leaf-double-click path remains the one that calls `Editor::FrameSelection`.
- **Settings window: collapse all sections by default.** On first open each collapsible section (`Camera`, `Render`, `Import`, `Postprocess`, etc.) starts collapsed. The user's per-section open/closed state is persisted so once they expand a section, it stays expanded across editor restarts.
- **Sky defaults -> `examples/Resource`.** Promote the engine-level sky assets that the outdoor project currently borrows -- the seven `Atmosphere/*.glsl` shaders, the `Terrain/cloud_noise.png` and `Terrain/moon.png` textures, and the `SkyAtmosphere` setup the Lua script applies -- to `examples/Resource/Shader/Atmosphere/` and `examples/Resource/Texture/Sky/`. The `setup_terrain_sky_scene.lua` and any new project-level setup switch to those paths. A short `examples/Resource/SKY-README.md` documents the new home so future authors can wire a sky in two lines.

## Capabilities

### New Capabilities

- `scene-inspector-multi-select`: Scene Inspector supports multi-row selection with a single-click row hitbox, Ctrl/Shift additions, and a context menu that gates by selection size.
- `inspector-reveal-on-pick`: viewport-pick automatically expands collapsed ancestors of the selected node in the Scene Inspector.
- `editor-settings-collapsed-default`: The Settings window opens with all sections collapsed and remembers per-section state across runs.
- `engine-sky-defaults`: The atmosphere shaders and sky textures are promoted into `examples/Resource/` so any project can enable a sky without copying the outdoor assets.

### Modified Capabilities

- `scene-inspector-node-operations`: replace the single selected-node assumption with a selection set (anchor + members), gate per-row context menu entries by selection size, and require the full row to be a clickable hitbox.
- `editor-frame-selection`: the frame-selection handler is unchanged -- it still fires on the selection anchor (single node) when invoked. The picker does NOT call it; the picking path only triggers the tree-reveal behaviour, so framing stays where the user expects (the Scene Inspector's leaf-double-click).
- `sky-atmosphere`: document the new engine-default location for the atmosphere shaders and the cloud/moon textures so external projects can reference them; the component itself continues to load paths from the project, but the project layout ships with the new canonical paths.
- `project-render-settings`: settings panel sections now default to collapsed and remember per-section open state.

## Impact

- Editor C++ code: `EditorWindows.cpp` (Scene Inspector tree rendering, picker integration, settings window), `EditorPicking.cpp` (notifies the inspector to reveal) -- no public API changes.
- Scene Lua bindings: no new bindings needed; the multi-select state lives in the C++ editor and is consumed via the existing `selection` accessor.
- `examples/Editor.lua`: no functional changes; the script keeps its existing scene-load callbacks. The settings-window persistence is read by the C++ editor's settings panel.
- `examples/Resource/`: receives the `Texture/Sky/` (new) directory plus a short `SKY-README.md`. No existing files are deleted.
- No external dependencies; no breaking changes to public Lua or C++ APIs.
