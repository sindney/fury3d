# editor-ui-automation

## MODIFIED Requirements

### Requirement: Scene persistence correctness SHALL use the headless CLI rather than OS file-dialog automation

Editor UI tests SHALL exercise scene open/save commands against known temporary paths through the editor's internal path-based command surface. Automated tests SHALL NOT depend on native operating-system file dialogs. The saved output SHALL then be loaded into a fresh scene by a headless CLI round-trip test, which verifies the persistence contract independently of editor state.

#### Scenario: Editor save is verified headlessly

- **WHEN** an editor UI test modifies a loaded fixture and saves it to a temporary path
- **AND** the headless round-trip runner loads and re-saves that output
- **THEN** the saved scene document matches the expected modified state

#### Scenario: Test does not alter the source fixture

- **WHEN** an editor open/save automation test completes or fails
- **THEN** the original checked-in scene and the user's editor settings are unchanged
- **AND** generated scenes, settings, reports, and captures are confined to the test output directory
