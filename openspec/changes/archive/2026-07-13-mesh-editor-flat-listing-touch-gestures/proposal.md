## Why

The Mesh editor's metadata panel hides LOD thresholds and submeshes behind collapsible `TreeNode` headers, which forces two extra clicks to inspect a mesh's structure — and the LOD dropdown only switches which LOD the runtime *would* pick, not what the preview actually renders. Separately, neither the Mesh previewer nor the main scene Viewport recognize trackpad gestures: macOS / Windows-precision users fall back to mouse-drag-orbit and scroll-wheel-zoom even when their natural input is a two-finger pan or pinch. This change flattens the listings, makes the LOD dropdown preview the chosen LOD, and adds touchpad gesture input to both 3D viewports.

## What Changes

- **Flat LOD threshold listing** — In the Mesh editor's metadata panel, render the LOD thresholds inline as a table (no `TreeNode` / no collapsible header). Each row shows `LOD i` + threshold + slider; the deepest LOD threshold is locked at `0.0`.
- **Flat submesh listing** — In the Mesh editor's metadata panel, render the submeshes inline as a table (no `TreeNode`). Columns: index, name (if any), indices, triangles. Header summarizes total vertices / indices / triangles / submeshes count.
- **LOD preview override** — When the LOD dropdown selects a non-zero LOD, the preview pane MUST render that specific LOD (`mesh->GetLodMesh(i)`), not whatever the runtime would auto-select. Selecting "Auto" (the existing default) restores runtime-driven LOD selection. The choice is local to the popup (reopening the editor restores the previous selection).
- **Touchpad gestures in the Mesh previewer** — When the cursor is over the preview pane AND `ImGuiIO::MouseSource == ImGuiMouseSource_Touchpad`, a two-finger drag (positive `io.MouseDelta`, no buttons pressed) orbits the camera (same as left-mouse-drag), and a pinch (`io.MouseWheel` ≠ 0, no buttons pressed, no Ctrl) zooms (same as the wheel). The mouse-input paths remain unchanged.
- **Touchpad gestures in the main scene Viewport** — When the cursor is over the Viewport content rect AND `ImGuiIO::MouseSource == ImGuiMouseSource_Touchpad`, a two-finger drag rotates the flythrough camera (same as the existing left-mouse-drag yaw / pitch) and a pinch zooms by adjusting the camera's FOV (clamped to `[20°, 90°]`). Mouse input is unchanged.

No breaking API removals. Existing mouse + WASD + wheel behaviors continue to work.

## Capabilities

### New Capabilities
- `mesh-editor-flat-listing`: flat (non-collapsible) listing of LOD thresholds and submeshes in the Mesh editor's metadata panel.
- `mesh-editor-lod-preview`: LOD dropdown in the Mesh editor overrides which LOD the preview pane renders, with "Auto" restoring runtime-driven selection.
- `editor-touchpad-gestures`: two-finger-drag and pinch gesture support for the Mesh editor's preview pane and the main editor scene Viewport, gated on `ImGuiIO::MouseSource == ImGuiMouseSource_Touchpad`.

### Modified Capabilities
- `asset-editor-windows`: add requirements for the flat LOD-thresholds listing, flat submesh table, and touchpad-gesture camera input in the Mesh editor's preview pane.
- `editor-viewport-window`: add requirements for touchpad-gesture camera input (two-finger drag → yaw/pitch; pinch → FOV zoom) in the main editor scene Viewport.

## Impact

- `engine/Fury/Editor/EditorAssetWindows.cpp` — flatten the LOD Thresholds / Submeshes sections (around lines 467 and 595); route the LOD dropdown selection into `RenderMeshPreview`'s `display_mesh` override; add the touchpad input branch in `RenderMeshPreview`'s camera block (around lines 910–952).
- `engine/Fury/Editor/EditorAssetWindows.h` — may add an `int preview_lod_override = -1` (or similar) field on the preview state struct if needed; otherwise unchanged.
- `examples/Editor.lua` — add the touchpad input branch in `on_update` for the flythrough camera (after the existing LMB-drag / WASD / wheel sections, around line 510).
- `engine/ThirdParty/ImGui/imgui.h` — verify `ImGuiMouseSource` enum exists in the bundled ImGui 1.92.8 (no code change expected; the enum was added in 1.92).
- No public C++ API changes; no new Lua bindings required (existing `ImGui::GetIO()` access in `EditorAssetWindows.cpp` is sufficient).