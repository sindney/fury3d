# editor-ui-automation Specification

## Purpose
TBD - created by archiving change refactor-automation-and-sample-scenes. Update Purpose after archive.
## Requirements
### Requirement: Editor automation SHALL use a pinned Dear ImGui Test Engine integration

The editor SHALL integrate the Dear ImGui Test Engine from a pinned dependency revision compatible with the vendored Dear ImGui revision. Test-engine code, test registration, and automation-only command-line handling SHALL be enabled by an explicit build or runtime option and SHALL NOT change normal editor input, layout, or window behavior when automation is disabled.

The integration SHALL install the required ImGui configuration, coroutine implementation, context lifecycle, and pre-swap/post-swap hooks in the order required by the pinned test-engine revision.

#### Scenario: Normal editor starts without automation

- **WHEN** the editor starts without its automation option
- **THEN** no tests are queued
- **AND** editor input and presentation behave as they do in a non-automation build

#### Scenario: Automation build registers tests

- **WHEN** the editor automation configuration starts
- **THEN** the test engine attaches to the editor's existing ImGui context
- **AND** the editor automation tests are registered before execution begins

### Requirement: Editor UI tests SHALL run unattended with stable selection and reporting

An automation invocation SHALL accept a test filter and result-output path, queue matching tests, inject input through ImGui IO rather than synthetic operating-system events, exit after the queue completes, and return non-zero when any selected test fails or no test matches. It SHALL print a result summary and write JUnit XML. A failure SHALL also produce the supported diagnostic capture or context needed to identify the failing window and item.

#### Scenario: Filtered suite passes

- **WHEN** the editor runs with a filter matching one or more automation tests
- **THEN** every matching test executes without synthetic OS clicks
- **AND** the process exits 0 and writes a JUnit report containing those results

#### Scenario: Test failure returns failure

- **WHEN** any selected editor automation test fails
- **THEN** the process exits non-zero
- **AND** the summary and JUnit report identify the failed test

#### Scenario: Unknown filter returns failure

- **WHEN** the supplied filter matches no registered test
- **THEN** the editor reports the empty selection and exits non-zero

### Requirement: Automated editor controls SHALL have stable test references

Windows and controls exercised by automation SHALL have persistent ImGui identifiers that do not depend on pointer values, translated text, or incidental visible-label changes. Tests SHALL scope item references to the intended window or popup and SHALL use stable `###` identifiers where a visible label is not a suitable contract.

#### Scenario: Visible label changes without changing test identity

- **WHEN** an automated control retains its stable ImGui identifier but changes its visible label
- **THEN** the existing automation reference still resolves to that control

#### Scenario: Repeated property rows remain unambiguous

- **WHEN** an inspector displays the same property label for multiple components
- **THEN** each automated reference is scoped by node, component, and property identity

### Requirement: The editor suite SHALL verify user-visible state and underlying model changes

The editor automation suite SHALL cover hierarchy node selection, inspector property editing, mesh preview display, and profiling-window display. Selection tests SHALL verify both highlight/inspector state and the selected scene node. Property tests SHALL change a value through the widget and verify the component value, then exercise undo where the existing command supports it. Preview and profiling tests SHALL verify visible content and live model data rather than only checking that a window opened.

#### Scenario: Select a node in the hierarchy

- **WHEN** a test clicks a known node row
- **THEN** that node becomes the editor selection
- **AND** the inspector displays that node's components

#### Scenario: Edit an inspector property

- **WHEN** a test enters a new value in a known inspector widget and commits it
- **THEN** the target component contains the new value
- **AND** the widget displays the committed value

#### Scenario: Open a mesh preview

- **WHEN** a test opens a known mesh asset in its preview window
- **THEN** the preview identifies the expected mesh and displays its renderable content and existing preview controls

#### Scenario: Display profiling data

- **WHEN** a test opens the profiling window after frames have rendered
- **THEN** expected profiler sections are visible
- **AND** displayed counters or timings are populated from the current run

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

