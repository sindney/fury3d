# editor-viewport-window

## Purpose

Dockable ImGui window that hosts the editor's 3D scene. The window owns an offscreen `RenderTarget` (color + depth) into which the active pipeline renders, and presents the color attachment via `ImGui::Image` inside the window's content rect. The window is the central dock node in the default layout, can be docked/tabbed/floated like any other editor panel, and gates viewport-specific input (picks, camera-drag) to its content rect.

## Requirements

### Requirement: The editor SHALL render the 3D scene into a dockable Viewport ImGui window

When `WITH_EDITOR` is enabled, the editor SHALL render a dockable ImGui window titled `Viewport`. The window SHALL display the 3D scene by rendering the scene to an offscreen `RenderTarget` (one `RGBA8` color attachment + one `DEPTH24` depth attachment) and presenting it via `ImGui::Image` inside the window's content rect.

The `RenderTarget` SHALL be owned by the editor, allocated lazily on the first frame the Viewport window is visible, and recreated when the window's content-rect size changes between frames (clamped to a minimum of 1×1). When the Viewport window is hidden or collapsed, the `RenderTarget` SHALL NOT be reallocated or drawn into, and `Pipeline::Execute` SHALL be invoked with a null render-target argument (reverting to default-framebuffer rendering) OR skipped entirely for the 3D scene — the editor SHALL NOT present a stale frame.

`Pipeline::Execute` SHALL accept an optional `RenderTarget*` argument (default `nullptr`). When the argument is `nullptr`, the pipeline SHALL bind the default framebuffer (current behavior, used by `WITH_EDITOR=OFF` and non-editor samples). When the argument is non-null, the pipeline SHALL bind the provided render target's framebuffer for all of its passes.

The Viewport window SHALL participate in the editor's standard window-visibility machinery: toggled from `Window → Viewport`, queryable via `Editor::SetWindowVisible("Viewport", …)` / `Editor::GetWindowVisible("Viewport")`, and visible by default in the dock layout (see the modified editor-shell default-layout requirement).

#### Scenario: Viewport window is visible by default

- **WHEN** the engine starts with `WITH_EDITOR=ON` and no `imgui.ini` exists
- **THEN** a window titled `Viewport` is visible and docked in the central region of the dockspace
- **AND** the 3D scene is displayed inside the Viewport window's content rect

#### Scenario: Viewport window can be docked, tabbed, and floated

- **WHEN** the user drags the Viewport window's title bar onto the right edge of the dockspace
- **THEN** the Viewport window docks to the right region
- **AND** the 3D scene continues to render inside the Viewport window's content rect at the new size and position

#### Scenario: Viewport window hidden skips 3D rendering to its render target

- **WHEN** the user toggles `Window → Viewport` off
- **THEN** the Viewport window is not rendered
- **AND** the editor does not allocate or draw into the Viewport `RenderTarget` on subsequent frames
- **AND** no `ImGui::Image` of the viewport texture is emitted

#### Scenario: Render target resizes with the Viewport window

- **WHEN** the user resizes the Viewport window by dragging its edge from 800×600 to 400×300
- **THEN** on the next frame the `RenderTarget` is recreated at approximately 400×300 (content-rect size, clamped to ≥1×1)
- **AND** the 3D scene fills the new content rect without stretching artifacts

#### Scenario: WITH_EDITOR=OFF build is unaffected

- **WHEN** the engine is built with `WITH_EDITOR=OFF`
- **THEN** no `Viewport` window is rendered
- **AND** `Pipeline::Execute` is called with a null render-target argument (or the default argument) and binds the default framebuffer
- **AND** samples that call `Pipeline::Execute(scene)` directly compile and run unchanged

### Requirement: The editor SHALL derive the camera projection's aspect ratio from the Viewport window's content rect each frame

Each frame the Viewport window is visible, the editor SHALL read `ImGui::GetContentRegionAvail()` (or the equivalent content-rect size) and set the active pipeline camera's aspect ratio to `aspect = contentWidth / max(contentHeight, 1)` before invoking `Pipeline::Execute`. The vertical FOV (`fovy`) and the near/far planes SHALL be preserved across resize.

When the Viewport window is hidden or collapsed, the camera's aspect ratio SHALL be left unchanged from its last visible-frame value (so a subsequent re-show does not produce a one-frame distortion from a stale hard-coded aspect).

The camera aspect SHALL NOT be hard-coded to a constant (e.g., the previous `1.778` 16:9 value). The only authority for the viewport's aspect is the Viewport window's content rect.

#### Scenario: Camera aspect matches the Viewport window's content rect

- **WHEN** the Viewport window's content rect is 1000×500 pixels
- **THEN** the active camera's projection matrix uses aspect ratio `2.0`
- **AND** rendered geometry in the viewport shows no horizontal or vertical stretching

#### Scenario: Resizing the Viewport window updates the camera aspect on the next frame

- **WHEN** the Viewport window is resized from a square content rect (aspect 1.0) to a wide content rect (aspect 2.0)
- **THEN** on the next frame the camera's projection matrix uses aspect ratio `2.0`
- **AND** the scene's vertical FOV is unchanged

#### Scenario: Hidden Viewport window does not corrupt the camera aspect

- **WHEN** the Viewport window is hidden
- **AND** the user toggles it back visible after resizing the SFML window
- **THEN** on the first visible frame the camera aspect is recomputed from the current content rect
- **AND** no one-frame distortion from a hard-coded aspect is visible

### Requirement: The Viewport window SHALL route mouse input only when hovered and not captured by ImGui

A left-mouse interaction inside the Viewport window SHALL be treated as a viewport interaction (pick or camera-drag) ONLY when ALL of the following hold:

- The Viewport window is the hovered ImGui window (`ImGui::IsWindowHovered()` is true for the Viewport window's call to `ImGui::Begin`), OR equivalently the cursor lies within the Viewport window's content rect AND no other ImGui window is on top of it.
- `ImGui::GetIO().WantCaptureMouse` is false (text inputs / other widgets that claimed the mouse do not steal the click).
- No gizmo is currently being used (`ImGuizmo::IsUsing() == false` and `ImGuizmo::IsOver() == false` for the gizmo's hover gate).

When any of these conditions is false, the click SHALL pass through to ImGui's normal widget hit-testing (so docked panels that overlap the Viewport window, the gizmo, or text inputs absorb their own clicks).

#### Scenario: Click on a docked panel over the Viewport window does not pick

- **WHEN** the Scene Inspector window is floating over the Viewport window
- **AND** the user left-clicks on the Scene Inspector
- **THEN** no pick is scheduled
- **AND** the Scene Inspector receives the click as normal

#### Scenario: Click while a gizmo is in use does not pick

- **WHEN** the user is mid-drag on the TRS gizmo's translate handle
- **AND** the cursor moves over the Viewport window's content rect
- **THEN** no pick is scheduled until the gizmo drag completes

#### Scenario: Click on empty Viewport window content routes to the viewport

- **WHEN** the cursor is inside the Viewport window's content rect
- **AND** no docked panel is on top
- **AND** `WantCaptureMouse` is false
- **AND** no gizmo is in use
- **THEN** the click is eligible to schedule a pick (subject to the click-vs-drag gate in the viewport-picking spec)