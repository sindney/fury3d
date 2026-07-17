## ADDED Requirements

### Requirement: Vendored ImSequencer and ImCurveEdit

The engine SHALL vendor `ImSequencer.h`/`ImSequencer.cpp` and `ImCurveEdit.h`/`ImCurveEdit.cpp` from upstream ImGuizmo (same upstream commit as the existing vendored ImGuizmo, MIT license) into `engine/ThirdParty/ImGuizmo/`, and add the two `.cpp` files to `FURY_EDITOR_SRC` in `engine/CMakeLists.txt` (alongside the existing `ImGuizmo.cpp` entry at line 159). The headers SHALL be includable as `#include "ImSequencer.h"` and `#include "ImCurveEdit.h"` from editor sources.

#### Scenario: Headers compile alongside existing ImGuizmo
- **WHEN** the editor is built
- **THEN** `ImSequencer.cpp` and `ImCurveEdit.cpp` compile without modifying the vendored ImGuizmo core, and `furye` links successfully

### Requirement: Animation editor window

The editor SHALL provide an `Animation` window registered through the existing pattern at `Editor.cpp:557-563` (a `g_ShowAnimation` bool, a menu item under `View`, a `RenderAnimationWindow(&g_ShowAnimation)` function, and a `SetWindowVisible("Animation", bool)` Lua API entry). The window SHALL be editor-only (`#ifdef WITH_EDITOR`).

#### Scenario: Window opens from menu
- **WHEN** the user opens the `View` menu and selects `Animation`
- **THEN** the Animation window appears

#### Scenario: Window toggled from Lua
- **WHEN** a script calls `Editor.SetWindowVisible("Animation", true)`
- **THEN** the Animation window appears

### Requirement: Clip selection and timeline scrubbing via ImSequencer

The Animation window SHALL list every `AnimationClip` in `Scene::Active->GetEntityManager()` and allow the user to select one. When a clip is selected, the window SHALL render an ImSequencer timeline with one track per channel (`positions` / `rotations` / `scalings` per named channel) and a movable playhead. Dragging the playhead SHALL scrub the clip: it sets `AnimationState.time` on the selected `Animator` (or a scratch `Animator` if none selected) and the viewport re-poses in real time.

#### Scenario: Playhead scrub updates viewport
- **WHEN** the user drags the ImSequencer playhead to time `t` while a clip is selected
- **THEN** the bound `Animator`'s `AnimationState.time` is set to `t` and the viewport shows the posed mesh within the same frame

#### Scenario: Empty state when no clips
- **WHEN** no `AnimationClip` is registered in the `EntityManager`
- **THEN** the window shows a placeholder message and no timeline

### Requirement: Keyframe curve editing via ImCurveEdit

When a channel track is selected in the ImSequencer timeline, the Animation window SHALL render an ImCurveEdit widget below the timeline showing the keyframe values over time for that channel (one curve per component: X/Y/Z for positions and scalings, X/Y/Z/W for rotations). The user SHALL be able to drag keyframes to edit their value or time, and the underlying `AnimationClip` SHALL be marked dirty (so the scene save flow prompts to save). Curve edits SHALL apply on the next `Display` call so the viewport reflects edits live.

#### Scenario: Drag a keyframe to change its value
- **WHEN** the user drags a keyframe point in the ImCurveEdit view vertically
- **THEN** the corresponding `KeyFrame.{x,y,z}` value is updated, the clip is marked dirty, and the viewport re-poses on the next frame

#### Scenario: Switch channel tracks
- **WHEN** the user selects a different track in the ImSequencer
- **THEN** the ImCurveEdit view reloads with the new channel's curves

### Requirement: Animator component inspector entries

The Node Properties inspector (`EditorNodeProperties.cpp`) SHALL render an `Animator` section when the selected node has an `Animator` component, exposing: bound clip list, currently playing clip name, play/stop/crossfade buttons, wrap mode dropdown, speed slider, and a "Scrub" slider bound to `AnimationState.time`. The inspector SHALL use the existing `ComponentRegistry` dispatch (SceneNode.cpp:19-25) so the section appears automatically once the component is registered.

#### Scenario: Inspector shows Animator section
- **WHEN** a node with an `Animator` component is selected
- **THEN** the Node Properties window shows an `Animator` section with playback controls

#### Scenario: Play button starts playback
- **WHEN** the user clicks the `Play` button in the Animator section
- **THEN** the bound clip begins playing and the button label toggles to `Stop`

### Requirement: Animation window respects editor pause state

When the editor is paused (existing editor pause flag), the Animation window SHALL NOT auto-advance the playhead from `OnUpdate`; scrubbing and manual `time` setting SHALL still work. When unpaused, playback SHALL resume from the current `AnimationState.time`.

#### Scenario: Pause halts playhead advance
- **WHEN** the editor is paused and a clip is playing
- **THEN** the playhead stays at its current position and the viewport holds the current pose
