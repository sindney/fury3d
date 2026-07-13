## Context

The Mesh editor (`engine/Fury/Editor/EditorAssetWindows.cpp`) opens a per-asset modal window when an artist double-clicks a Mesh tile in the Content Browser. Its metadata panel currently hides LOD thresholds behind an `ImGui::TreeNode("LOD Thresholds")` (line 467) and submeshes behind two nested `TreeNode`s (`"Submeshes"` wrapping per-submesh `"Submesh <i>"`, lines 595–611). The LOD dropdown (lines 438–458) is preview-only today but only switches between LODs without an explicit "Auto" entry — the user has no way to preview a specific LOD without losing the runtime-driven behavior on reopen. The preview pane (`RenderMeshPreview`, lines 645–955) reads `io.MouseDelta` / `io.MouseWheel` for left-drag orbit / wheel zoom / right-drag pan (lines 910–952) and never inspects `ImGuiIO::MouseSource`.

The main editor scene Viewport is a dockable ImGui window (`RenderViewportWindow` in `engine/Fury/Editor/EditorWindows.cpp` lines 1650–1735). Its camera-input handling lives entirely in `examples/Editor.lua` (`on_update`, lines 443–531). Mouse-drag rotates yaw/pitch, mouse-wheel adjusts `move_speed` (not zoom), and WASD / arrows translate the flythrough. Neither the C++ preview nor the Lua flythrough reads `ImGuiIO::MouseSource`, so trackpad pinch is indistinguishable from a fast mouse-wheel spin.

Both backends expose trackpad gestures: SFML3 translates a two-finger drag into `MouseMove` events (without any button held) and a pinch into a `MouseWheelScrolled` event. The imgui_impl_sfml3 backend then sets `io.MouseSource = ImGuiMouseSource_Touchpad` when the originating `sf::Event` is a touch event (ImGui 1.92+).

## Goals / Non-Goals

**Goals:**

- Eliminate the `TreeNode` collapse on LOD Thresholds and Submeshes so artists see every value without an extra click.
- Add a per-popup "Auto" entry to the LOD dropdown so the runtime-driven LOD behavior is preserved as the default and the user-selected LOD persists across reopen.
- Detect `ImGuiMouseSource_Touchpad` in both 3D viewports and route the resulting input to existing camera handlers (orbit / zoom), gated on no-button and no-modifier so mouse-input paths are untouched.
- Add FOV-driven zoom to the main flythrough camera via trackpad pinch, without disturbing the existing mouse-wheel-move-speed behavior on non-touchpad input.

**Non-Goals:**

- Multi-touch / 3+ finger gestures.
- Touch input on Windows touchscreen (only trackpad gestures — `MouseSource == Touchpad`).
- New C++ camera-controller classes. The architecture stays Lua-on-Lua and C++-on-C++; gestures are added at the existing camera-input call sites.
- Pinch-to-zoom on the wheel-driven move-speed in `Editor.lua` (gesture-driven FOV is the new mode; wheel-driven move-speed remains for mouse wheel).

## Decisions

### Decision 1: Render the LOD Thresholds / Submeshes as flat `BeginTable` rows

- **Choice**: Replace both `TreeNode` blocks with `ImGui::BeginTable("...##popup_id", N, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)` / `TableNextRow` / `TableNextColumn` / cell content / `EndTable`. No `TreeNode`, no `CollapsingHeader`, no `TreeNodeEx`. The threshold slider and its read-only label live in separate columns of the same row; the deepest LOD row renders a disabled slider (or no slider + read-only `0.000` text).
- **Why**: ImGui 1.92.8 (bundled) supports `BeginTable`, the visual style matches the rest of the editor (which already uses tables elsewhere in Node Properties), and tables scroll automatically inside the existing `BeginChild("metadata", ...)` host (line 1000) so a 30-row submesh list doesn't overflow.
- **Alternatives considered**:
  - **Keep `TreeNode` but default-open** — doesn't satisfy "no need to fold" (still a clickable arrow that can be collapsed).
  - **`BulletText` per row** — loses alignment and column structure; no header row.

### Decision 2: LOD dropdown gets an `Auto` entry as default

- **Choice**: The existing LOD dropdown gains a leading `Auto` entry. The `Auto` selection means "do not override"; the preview picks `display_mesh` via the existing runtime path (`render_mesh->GetLodMesh(activeLod)` where `activeLod` is whatever the runtime would pick for the current camera distance). A specific LOD selection stores `i` on the per-popup state and the preview iterates `display_mesh = mesh->GetLodMesh(i)`.
- **Why**: The user wanted "when selected specific lod, the mesh render should render in the preview that specific lod for previewing" — but the existing default of always rendering LOD 0 is wrong for artists inspecting production assets (they typically want to see what the runtime sees by default). `Auto` is the natural default.
- **Per-popup state**: Add `int preview_lod_override = -1` to the `OrbitState` struct (EditorAssetWindows.cpp lines 61–76). `-1` = Auto; `[0, GetLodCount())` = override. The existing `g_OrbitState` map keyed by `popup_id` already provides the right lifetime (entries survive window reopen until the mesh is evicted from the scene).
- **Alternatives considered**:
  - **Always override, no Auto entry** — forces artists to pick LOD 0 manually every time, breaks the existing default-renders-LOD-0 contract.
  - **Per-mesh LOD override on the `Mesh` itself** — leaks preview state into the model; conflicts with the `mesh-editor-lod-preview` spec ("the Mesh's LOD chain is unchanged").

### Decision 3: Gesture detection via `ImGuiIO::MouseSource`

- **Choice**: Both viewports read `ImGui::GetIO().MouseSource` each frame. When `MouseSource == ImGuiMouseSource_Touchpad` AND no mouse buttons are pressed AND no Ctrl/Cmd modifier is held:
  - `io.MouseDelta != 0` → orbit / yaw-pitch (existing LMB-drag formula)
  - `io.MouseWheel != 0` → zoom (existing wheel formula for the preview; FOV-driven for the flythrough)
- **Why**: The bundled imgui_impl_sfml3 backend sets `MouseSource` based on the originating `sf::Event` subtype (touch events for trackpad gestures, mouse events for physical mice). This is the only reliable way to distinguish pinch-from-wheel and two-finger-pan-from-mouse-move in ImGui 1.92.
- **Why no-button gate**: SFML 3 sometimes synthesizes a transient button state during a touch sequence; gating on `ImGui::IsMouseDown(ImGuiMouseButton_*) == false` for all buttons prevents a touchpad gesture from doubling as a mouse drag.
- **Why no-modifier gate**: Many platforms send pinch with Ctrl held for legacy reasons (Windows precision touchpad historically); the modifier gate lets Ctrl+wheel continue to mean "UI scale" / "font scale" elsewhere if used.
- **Alternatives considered**:
  - **Detect touch via SFML events directly** — leaks SFML event handling into the camera-input path and duplicates what imgui_impl_sfml3 already does.
  - **Use `ImGui::IsKeyDown(ImGuiMod_Ctrl)` to gate the wheel** — only handles the modifier case; doesn't help with two-finger-drag detection.

### Decision 4: Pinch on the flythrough adjusts FOV, not move-speed

- **Choice**: In `examples/Editor.lua`'s `on_update`, after the existing wheel-move-speed block (line 503–510), add a touchpad-pinch branch that subtracts `io.MouseWheel * 5.0f` from a local `fov_deg` variable (initial value read from the camera at startup) and clamps to `[20°, 90°]`. Apply via `camera:SetPerspectiveFov(fov_deg)` (or whatever the existing Lua binding is — verify in `LuaBindings.cpp`).
- **Why**: `move_speed` already has a wheel-driven path that artists use on mouse; changing that to FOV would be surprising. FOV is the natural "zoom" semantic for a flythrough camera. Clamping to `[20°, 90°]` matches typical 3D-editor ranges (Blender / Unreal both clamp to similar bounds).
- **Alternatives considered**:
  - **Pinch adjusts move_speed** — feels like changing walking speed, not zoom.
  - **Pinch dollies the camera along forward** — duplicates WASD and breaks the orbit-around-AABB invariant.

### Decision 5: Gesture handling is local to each viewport, no shared abstraction

- **Choice**: The Mesh previewer gesture branch lives in `RenderMeshPreview` next to the existing camera block (EditorAssetWindows.cpp lines 910–952). The main viewport gesture branch lives in `Editor.lua` next to the existing camera block (lines 443–531). No new helper class, no shared utility.
- **Why**: The two viewports have very different cameras (orbit-around-AABB vs flythrough) and very different code languages (C++ vs Lua). A shared abstraction would be a 30-line header / binding for ~6 lines of input branching per call site.
- **Alternatives considered**:
  - **New C++ `TouchpadCameraInput` helper** — over-engineered for two call sites; would still need a Lua binding for the flythrough.
  - **Move all camera input into Lua** — would require moving the mesh preview's orbit state out of C++, much larger refactor than the user asked for.

## Risks / Trade-offs

- **ImGui 1.92 `MouseSource` API may be missing in the bundled version** — Risk: `ImGuiMouseSource_Touchpad` enum value not defined. Mitigation: fall back to checking `io.MouseSource == ImGuiMouseSource_Mouse` is false AND `io.MouseDelta` arrives without a button. If the enum itself is missing, the code falls through to the existing mouse paths (gesture disabled, mouse still works) and a follow-up patches the bundled ImGui.

- **Trackpad two-finger drag may arrive with a synthesized left-button** — Risk: a platform / driver sends `ImGuiMouseButton_Left` as held during the gesture, so the no-button gate rejects the gesture. Mitigation: gate is `!IsMouseDown(Left) && !IsMouseDown(Right) && !IsMouseDown(Middle)` — even one button disqualifies. If a driver is found to synthesize a button, the gate can be loosened to "no LMB-drag is in progress" (compare `MouseDragMaxAbsDelta[Left] == 0`).

- **FOV adjustment interferes with the existing FOV startup value** — Risk: artists with non-default startup FOV (e.g. 60°) may see the FOV jump if our initial read picks up the wrong value. Mitigation: store the startup FOV once at the first `on_update` and treat subsequent pinches as deltas from that initial value.

- **ImGui table styles may not match the rest of the editor** — Risk: the flat LOD thresholds / submesh tables look out of place vs the surrounding labels. Mitigation: use the same column widths and padding as the Node Properties tables (which already use `BeginTable`); allow the user to resize columns to taste.

- **Per-popup state lifetime** — Risk: re-keying `g_OrbitState` on reopen currently works because the popup_id is stable, but if the mesh editor's popup_id changes (e.g. the prefix `MeshEditor:` is renamed) the override is lost. Mitigation: the existing keying scheme is already relied on for orbit state; we just add one more field to the same struct.

## Migration Plan

- **No data migration.** The Mesh asset format (`mesh_lod_thresholds` JSON, the existing `lod_meshes` array) is unchanged. The preview override lives entirely in editor-side state.
- **Rollback**: revert the changes to `EditorAssetWindows.cpp` and `Editor.lua` (the only two source files modified). No build-system or scene-file changes.
- **Verification**:
  - Build the editor (`make editor` or the existing build target).
  - Open a mesh asset with a 4-LOD chain in the editor. Confirm LOD thresholds render as a flat table with no collapse arrow.
  - Confirm submeshes render as a flat table with index / name / indices / triangles columns.
  - Select `LOD 2` in the dropdown, confirm the preview shows the LOD-2 mesh, close + reopen, confirm `LOD 2` is still selected.
  - On macOS, use a MacBook trackpad to two-finger-drag and pinch over the preview. Confirm orbit / zoom work without holding any mouse button.
  - On macOS, two-finger-drag / pinch over the main Viewport. Confirm yaw / pitch and FOV zoom work.
  - With a physical mouse, confirm LMB drag, RMB drag, wheel, WASD all still work exactly as before.

## Open Questions

- Should the FOV-zoom gesture also work when the cursor is over the Viewport ImGui window but outside the content rect (e.g. on the title bar)? Current spec: no (gestures are gated on content rect). Confirm with the user.
- Should pinch on the mesh previewer adjust FOV (mirror the flythrough) or distance (the existing wheel behavior)? Current spec: distance (matches the existing wheel behavior so a single zoom semantic covers both wheel and pinch). Confirm.
- Should the `Auto` LOD dropdown entry be the first option or the last? Current spec: first (matches "default" position). Confirm.