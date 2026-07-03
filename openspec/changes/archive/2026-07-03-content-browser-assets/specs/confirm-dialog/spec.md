## ADDED Requirements

### Requirement: The editor SHALL provide a reusable `ConfirmDialog` modal with a Yes/No callback

The editor SHALL expose a `Editor::ConfirmDialog(const std::string& title, const std::string& message, std::function<void(bool)> onResult)` API (and a C++ side `RequestConfirmDialog(...)` entry point wired into the editor tick) that opens a `BeginPopupModal` window titled `title`, renders `message` as body text, and presents two buttons labelled `Yes` and `No`. Clicking either button SHALL close the modal and invoke `onResult(true)` (Yes) or `onResult(false)` (No) exactly once per dialog invocation. The modal SHALL be `ImGuiWindowFlags_AlwaysAutoResize` and SHALL capture the modal close (Esc / click-outside) as a `No` result.

The dialog SHALL follow the existing flag-then-`OpenPopup` pattern used by `g_SaveAsModalOpen` in `Editor.cpp`: a pending-request queue holds `(title, message, onResult)` triples, and on the next editor tick the next pending request triggers `ImGui::OpenPopup` with a stable popup ID. Only one confirm dialog SHALL be visible at a time; subsequent pending requests SHALL queue and open when the previous one closes.

This helper is intended for non-trivial confirms (asset deletion with in-use warning, discard-unsaved-changes prompts). Trivial 2-3 line inline confirms (e.g. "are you sure?" with no side-effects) MAY continue to use `ImGui::OpenPopup` inline without going through this helper.

#### Scenario: Yes invokes the callback with true and closes

- **WHEN** `Editor::ConfirmDialog("Delete", "Are you sure?", cb)` is called
- **AND** the modal renders on the next frame and the user clicks `Yes`
- **THEN** `cb(true)` is invoked exactly once
- **AND** the modal is no longer rendered on subsequent frames

#### Scenario: No invokes the callback with false and closes

- **WHEN** a confirm dialog is open and the user clicks `No`
- **THEN** `cb(false)` is invoked exactly once
- **AND** the modal is no longer rendered on subsequent frames

#### Scenario: Esc dismisses as No

- **WHEN** a confirm dialog is open and the user presses Esc
- **THEN** `cb(false)` is invoked exactly once
- **AND** the modal is no longer rendered on subsequent frames

#### Scenario: Pending requests queue when one is already open

- **WHEN** `Editor::ConfirmDialog("A", "…", cbA)` and `Editor::ConfirmDialog("B", "…", cbB)` are both called in the same frame while no dialog is currently open
- **THEN** dialog A opens on the next frame
- **AND** dialog B does not open until A is closed
- **AND** when A is closed, dialog B opens on the following frame

#### Scenario: Callback is invoked exactly once

- **WHEN** the user clicks `Yes` on a confirm dialog
- **THEN** `onResult` is invoked exactly one time (not zero, not twice)
