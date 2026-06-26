## ADDED Requirements

### Requirement: The editor SHALL render an in-viewport TRS gizmo on the selected SceneNode

When `WITH_EDITOR` is enabled and `Editor::GetSelectedSceneNode()` is non-null, the editor SHALL render an ImGuizmo-driven TRS (translate / rotate / scale) gizmo over the selected node's world position. The gizmo SHALL be drawn into ImGui's draw lists during `Editor::Tick` (before `Gui::Render`), inside an invisible full-viewport ImGui window with `ImGuizmo::SetRect` clamped to the central dock node's rect (so it interacts only over the 3D viewport area, not over docked panels).

The gizmo SHALL receive:
- `view = inverse(camera_node->GetWorldMatrix())` — the active pipeline camera's view matrix.
- `projection = camera->GetProjectionMatrix()` — already in OpenGL column-major layout via `Matrix4::Raw[16]`.
- `matrix = node->GetWorldMatrix()` — the selected node's world transform.
- `operation` — TRANSLATE, ROTATE, or SCALE per `g_GizmoMode`.
- `mode` — LOCAL or WORLD per `g_GizmoSpace`.
- `snap` — `&snap_value` when `g_SnapEnabled == true` (snap_value = `g_SnapTranslate` for TRANSLATE, `g_SnapRotate` for ROTATE, `g_SnapScale` for SCALE), else `nullptr`.

When the gizmo reports a change (`ImGuizmo::IsUsing()`), the editor SHALL:

1. Compute the new local matrix as `Ml = inverse(Mp) * Mw`, where `Mp` is the selected node's parent's world matrix (identity when no parent) and `Mw` is the gizmo-modified world matrix.
2. Decompose `Ml` into translation `t`, rotation `r` (Quaternion), scale `s` (Vector4).
3. Write back ONLY the components that the current operation affects:
   - TRANSLATE → `node->SetLocalPosition(t)`.
   - ROTATE → `node->SetLocalRoattion(r)`.
   - SCALE → `node->SetLocalScale(s)`.
4. Call `node->Recompose(false)` so world transforms re-derive on the next frame.

The gizmo SHALL be hidden (skip rendering) when:
- `Editor::GetSelectedSceneNode() == nullptr`, OR
- `Pipeline::Active == nullptr`, OR
- `Pipeline::Active->GetCurrentCamera() == nullptr`.

#### Scenario: Gizmo appears on the selected node

- **WHEN** the user clicks a node in the Scene Inspector (or in the viewport)
- **AND** the engine has an active pipeline + camera
- **THEN** an ImGuizmo TRS gizmo is rendered at the node's world position on the next frame

#### Scenario: Gizmo follows the selected node when its transform changes

- **WHEN** a node is selected and the user edits its `Local Position X` to `5.0` in the Node Properties panel
- **THEN** the gizmo reanchors to the new world position on the next frame

#### Scenario: Translate drag updates the local position only

- **WHEN** the gizmo mode is TRANSLATE and the user drags the gizmo's X handle by 2 world units
- **THEN** `node->GetLocalPosition()` reflects the new position
- **AND** `node->GetLocalRoattion()` is unchanged
- **AND** `node->GetLocalScale()` is unchanged

#### Scenario: Rotate drag updates only the rotation

- **WHEN** the gizmo mode is ROTATE and the user rotates 45 degrees around the Y axis
- **THEN** `node->GetLocalRoattion()` reflects the new rotation
- **AND** `node->GetLocalPosition()` is unchanged
- **AND** `node->GetLocalScale()` is unchanged

#### Scenario: Drag a child node — only local transform writes

- **WHEN** a node has a non-identity parent transform
- **AND** the user drags the gizmo (in WORLD space) to a new world position
- **THEN** the new local position is computed as `inverse(parent_world) * new_world`
- **AND** the parent's world transform is NOT modified

#### Scenario: Snap toggle quantizes drag deltas

- **WHEN** `g_SnapEnabled == true` with `g_SnapTranslate == 1.0`
- **AND** the user drags the translate gizmo
- **THEN** the resulting local position changes only in 1.0-unit increments

#### Scenario: Gizmo hidden when no node is selected

- **WHEN** `Editor::GetSelectedSceneNode() == nullptr`
- **THEN** no ImGuizmo manipulator is drawn on the next frame

#### Scenario: Gizmo only interacts inside the central viewport region

- **WHEN** the user moves the cursor over a docked panel (Scene Inspector / Console / etc.)
- **THEN** ImGuizmo's hover state for the gizmo is false
- **AND** clicking the panel does not start a gizmo drag

### Requirement: The editor SHALL maintain gizmo mode, space, and snap state with persistence

The editor SHALL maintain the following editor-global state:

```cpp
GizmoMode  g_GizmoMode  = GizmoMode::Translate;   // Translate | Rotate | Scale
GizmoSpace g_GizmoSpace = GizmoSpace::World;      // Local | World
bool  g_SnapEnabled  = false;
float g_SnapTranslate = 1.0f;
float g_SnapRotate    = 15.0f;   // degrees
float g_SnapScale     = 0.1f;
```

The state SHALL be persisted via the existing FuryEditor `imgui.ini` settings handler (a single line `Gizmo=mode,space,snap_enabled,snap_t,snap_r,snap_s` appended to the `[FuryEditor][Editor]` block). On startup the persisted values SHALL be applied before any window renders.

The editor SHALL expose:

```cpp
void Editor::SetGizmoMode(const char* name);   // "translate" | "rotate" | "scale"
void Editor::SetGizmoSpace(const char* name);  // "local" | "world"
void Editor::SetSnapEnabled(bool enabled);
const char* Editor::GetGizmoMode();
const char* Editor::GetGizmoSpace();
bool        Editor::GetSnapEnabled();
```

Unknown name strings SHALL be silently ignored (no exception, no log entry — same convention as `Editor::SetWindowVisible`).

The Lua bindings SHALL expose `Editor.SetGizmoMode(name)`, `Editor.SetGizmoSpace(name)`, `Editor.SetSnapEnabled(bool)`. These are optional — Editor.lua does NOT require updates for v1; the C++ defaults plus user UI suffice.

#### Scenario: Mode change is reflected by the gizmo on the next frame

- **WHEN** `Editor::SetGizmoMode("rotate")` is invoked
- **THEN** the next frame's gizmo renders rotation handles instead of translate handles

#### Scenario: Mode persists across editor restarts

- **WHEN** the user picks ROTATE from the Node Properties panel and closes the editor
- **AND** the editor is launched again from the same working directory
- **THEN** the gizmo starts in ROTATE mode

#### Scenario: Snap state persists across editor restarts

- **WHEN** the user enables snap with translate = 0.5 and closes the editor
- **AND** the editor is launched again
- **THEN** snap is still enabled with translate = 0.5

#### Scenario: Unknown mode name is ignored

- **WHEN** `Editor::SetGizmoMode("rocket")` is invoked
- **THEN** `g_GizmoMode` is unchanged
- **AND** no log entry is emitted

### Requirement: The Node Properties window SHALL host the gizmo mode + snap controls

The Node Properties window SHALL render an additional "Gizmo" section above the existing Node / Light sections. The section SHALL contain:

1. A 3-button row (Translate / Rotate / Scale) implemented as `ImGui::RadioButton` driven by `g_GizmoMode`.
2. A 2-button row (Local / World) implemented as `ImGui::RadioButton` driven by `g_GizmoSpace`.
3. A `Snap` checkbox driven by `g_SnapEnabled`.
4. When `g_SnapEnabled` is true: three `DragFloat` widgets for `g_SnapTranslate`, `g_SnapRotate`, `g_SnapScale`. When false: those widgets SHALL be hidden (or disabled — implementation choice).

The section SHALL render even when no node is selected (the gizmo doesn't appear, but the user can still configure mode / snap ahead of selecting).

#### Scenario: Mode buttons reflect and update state

- **WHEN** the user clicks the `Rotate` radio button in the Node Properties panel
- **THEN** `g_GizmoMode == GizmoMode::Rotate` on the next frame
- **AND** the active gizmo (if a node is selected) renders rotation handles

#### Scenario: Snap controls are gated by the Snap checkbox

- **WHEN** `g_SnapEnabled == false`
- **THEN** the snap-step `DragFloat` widgets are hidden (or rendered disabled)

- **WHEN** the user toggles Snap on
- **THEN** the snap-step widgets become visible (or enabled)
- **AND** subsequent gizmo drags use the displayed step values

#### Scenario: Gizmo section persists across selections

- **WHEN** the user selects node A, picks ROTATE, then selects node B
- **THEN** the Node Properties panel still shows ROTATE
- **AND** the gizmo on node B is in ROTATE mode

## MODIFIED Requirements

### Requirement: The editor SHALL ship a Scene Inspector window rendering the active scene as a tree

The Scene Inspector window SHALL render the active scene's node hierarchy as an ImGui tree (`ImGui::TreeNodeEx` with `ImGuiTreeNodeFlags_OpenOnArrow | OpenOnDoubleClick | DefaultOpen` for the root). Each node SHALL display its name (`SceneNode::GetName()`); selecting a node (single click) SHALL update the shared selection (`g_SelectedSceneNode`).

The tree data SHALL be obtained via the registered `Editor::SetSceneTreeProvider` Lua callback when one is registered; otherwise it SHALL walk `Scene::Active->GetRootNode()` directly.

The window SHALL render the selected row with `ImGuiTreeNodeFlags_Selected`. Selection state is shared with viewport picking and the Node Properties panel — clicking in the viewport SHALL also update the highlighted row in the Inspector, and clicking in the Inspector SHALL also drive the gizmo's render position.

The window SHALL be hidden by default; it is enabled by default in the dock layout but the `Window → Scene Inspector` toggle controls visibility.

#### Scenario: Scene Inspector mirrors the active scene tree

- **WHEN** Editor.lua opens `Resource/Scene/scene.bin`
- **AND** the Scene Inspector window is visible
- **THEN** the window renders a tree node for the scene root
- **AND** the root contains child nodes for each top-level node in `scene.bin` (the tank, grass, etc.)

#### Scenario: Selection survives across frames

- **WHEN** the user clicks a node in the tree
- **THEN** subsequent frames render that node with the selected highlight (`ImGuiTreeNodeFlags_Selected`)
- **AND** `Editor::GetSelectedSceneNode()` returns the matching `SceneNode` pointer

#### Scenario: Selection clears when the underlying node is removed

- **WHEN** the user has selected a node and `Scene::Active->Clear()` is called (via `File → New`)
- **THEN** `Editor::GetSelectedSceneNode()` returns null
- **AND** the inspector renders only the (now-empty) root

#### Scenario: Viewport pick syncs the Inspector highlight

- **WHEN** the user left-clicks a renderable node in the central viewport
- **AND** the picking pass resolves to that node
- **THEN** the Scene Inspector renders that node's row with the selected highlight on the next frame
- **AND** `Editor::GetSelectedSceneNode()` returns the picked node

#### Scenario: Inspector click is respected by the gizmo

- **WHEN** the user clicks a node in the Scene Inspector
- **THEN** the gizmo (if visible) renders at that node's world position on the next frame
