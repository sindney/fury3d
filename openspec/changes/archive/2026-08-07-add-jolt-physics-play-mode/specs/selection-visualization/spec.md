# selection-visualization (delta)

## ADDED Requirements

### Requirement: The selection overlay for a Camera component SHALL be a wireframe frustum

When the selected `SceneNode` has a `Camera` component, the editor SHALL draw
a wireframe frustum via `RenderUtil::DrawFrustum` (between `BeginDrawLines` /
`EndDrawLines`) in `kSelectionColor`, replacing the mesh-AABB box for that
node. The frustum SHALL come from `Camera::GetFrustum()` with the far plane
capped to a visually useful distance, and SHALL follow the Camera inspector's
FOV/near/far edits on the next frame. This makes camera placement (e.g. for
player-controller bindings) possible without a picture-in-picture preview.

#### Scenario: Selecting a camera node draws its frustum

- **WHEN** the user selects a `SceneNode` with a `Camera` component (perspective FOV 45°)
- **THEN** a wireframe frustum matching that camera's FOV, aspect, near, and (capped) far is drawn in `kSelectionColor` over the scene
- **AND** no AABB box is drawn for that node

#### Scenario: Editing the camera FOV updates the frustum live

- **WHEN** a camera node is selected and the user widens its FOV in Node Properties
- **THEN** the wireframe frustum widens on the next frame

#### Scenario: Third-person camera placement

- **WHEN** the user selects the camera node bound to a CharacterController and translates it with the gizmo
- **THEN** the frustum overlay shows exactly what the play-session camera will frame
