# editor-ux-batch

## Why

A batch of editor UX gaps and one rendering correctness issue has accumulated: two hard problems were parked in `add-animation-system` (reference grid rendering, NFD-dialog scroll breakage — both need lldb-driven investigation), glTF/FBX imports in metres land 100× too small against the engine's 1-unit = 1-cm convention with no guardrail, the Content Browser has no way to narrow a growing asset list, and skinned meshes keep their bind-pose AABB forever so animation can carry geometry outside the bounds used for culling / LOD / shadows.

## What Changes

- **Moved in from `add-animation-system`** (tasks 21.7, 20.8 + 17.3 — investigation notes carried over; both require active lldb debugging):
  - **Reference grid rendering** — parked after three failed approaches (line rendering, depth-sampling shader, scene-node quad). Re-attempt as a screen-space grid via depth reconstruction (post-process-style pass) after debugging why prior attempts were invisible.
  - **GUI scroll breakage after native dialogs** — root cause identified (ImGui `MousePos` cleared on focus loss + SFML macOS Retina 2× coordinate mismatch); fix reverted, needs the FocusGained re-seed using the 1× logical coordinate source.
- **Import unit-scale detection**: after File→Open / File→Import of a `.gltf`/`.glb`/`.fbx`, compute the imported scene's world AABB; if its max dimension is under 1 m (100 engine units), queue a Yes/No confirm (reusing `EditorConfirmDialog.h`) offering to auto-scale the imported root(s). Proposed scale starts at 100× and escalates by powers of 100 until the scaled bounds clear 1 m, derived from the measured bounds. New Settings → Import checkbox enables/disables the detection (follows the `auto_default_sun` import-flag pattern).
- **Content Browser filters**: type filter (All / Mesh / Material / Texture / AnimationClip) plus a fuzzy-search text filter over asset names, applied to the tile grid.
- **Profiler tab rename**: the Profiler window's `FPS` tab becomes `Perf` (it carries much more than FPS: memory, drawcalls, debug overlays, LOD histogram).
- **Settings window default collapse**: every Settings section starts collapsed by default except **Editor** (Import loses its `DefaultOpen`; Engine is already collapsed).
- **Viewport top toolbar**: a toolbar row at the top of the Viewport window. The gizmo controls (Translate/Rotate/Scale radios + Snap checkbox + snap-step settings, currently the "Gizmo" section of Node Properties) move to the toolbar's **left**; the Debug Overlays multi-select combo (currently in the Profiler Perf tab) moves to the toolbar's **right**, separated by a spring spacer. Both are removed from their original locations (Node Properties "Gizmo" section, Profiler Perf tab). Snap-step drags collapse into a `Snap▾` popup so the bar stays one line.
- **Skinned mesh bounds**: every frame a skinned mesh's pose changes, recompute its AABB from the joint-driven deformed pose and push it to the owning `SceneNode` (`SetModelAABB`) so culling, LOD selection, and shadow passes see the true bounds.
- **Import normal-generation option** (added 2026-07-20): `GltfImporter::Options::NormalGen` — **Smooth (default)** welds vertices by exact position before accumulating area-weighted face normals (exporter-split meshes like the Fox shade smoothly; the 21.3-era generator only smoothed across shared indices), **Flat** assigns per-face normals. Optional `normal_mode` arg on `Importer.LoadGltf` / `LoadFbx` / `LoadScene`, surfaced as a Settings → Import `Normal Gen` combo (`normals_smooth` flag).

## Capabilities

### New Capabilities

- `import-unit-scale`: post-import scene-bounds measurement, under-1m detection, confirm-dialog-driven auto-scale, and the Settings → Import enable/disable flag.
- `skinned-mesh-bounds`: per-frame AABB recompute for skinned meshes from the current joint pose.
- `editor-reference-grid`: toggleable reference grid rendered in the editor viewport.

### Modified Capabilities

- `editor-shell`: (a) Profiler window's `FPS` tab renamed to `Perf` and the Debug Overlays combo removed from it; (b) Content Browser gains a type filter and a fuzzy name-search filter; (c) Settings sections default to collapsed except Editor; (d) the gizmo mode/snap controls requirement moves from the Node Properties window to the Viewport top toolbar.
- `gltf-importer`: normal generation for NORMAL-less primitives becomes mode-selectable (smooth-with-position-welding default, flat optional) with a Lua `normal_mode` argument and a Settings → Import combo.
- `editor-viewport-window`: gains a top toolbar row hosting the gizmo controls (left) and the Debug Overlays combo (right).
- `mesh-lod-debug-view`: the `LOD Debug Colors` toggle moves with the Debug Overlays combo to the Viewport top toolbar (requirement header + body updated).
- `octree-spatial`: requirement header rename only — the Spatial readout lives in the renamed `Perf` tab.

## Impact

- **Code**: `engine/Fury/Editor/EditorWindows.cpp` (Content Browser filters, Settings → Import checkbox + default-collapse, Perf tab rename, overlays combo relocation), `engine/Fury/Editor/EditorNodeProperties.cpp` (gizmo section removal), `engine/Fury/Editor/Editor.cpp` (import flag), `examples/Editor.lua` (post-import bounds check + dialog + scale application), `engine/Fury/LuaBindings.cpp` (scene AABB + confirm-dialog bindings), `engine/Fury/AnimationPlayer.cpp` / `MeshRender.cpp` / `SceneNode.cpp` (skinned bounds push), `engine/ThirdParty/ImGui/backends/imgui_impl_sfml3.cpp` + SFML macOS input (scroll fix), pipeline/render passes (grid).
- **Docs**: `docs/LUA_API.md` regeneration if Lua surface grows; `docs/ARCHITECTURE.md` unit-convention note already covers the ×100 rationale.
- **Third-party**: scroll fix may touch vendored SFML (`InputImpl.mm`) — change must be minimal and annotated.
- **Debugging**: grid + scroll items are lldb-driven; repro scripts noted in design.md.
