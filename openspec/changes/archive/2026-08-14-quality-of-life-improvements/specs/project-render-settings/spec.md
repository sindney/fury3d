## ADDED Requirements

### Requirement: Every settings window section SHALL open collapsed

The Engine Settings window SHALL open with every collapsible section (`Camera`, `Render`, `Import`, `Postprocess`, `Sky`, plus any project-registered section) in its collapsed state. Sections SHALL render only their header row until the user expands them. The header row SHALL remain interactive; clicking the header toggle expands/collapses the section.

The collapsed-default state applies to the first time the Settings window is opened in a given process. The render settings editable widgets (HDR/LDR toggle, CSM toggle, postprocess chain editor, CSM tuning parameters) live inside the `Render` section and SHALL be hidden until the user expands that section.

#### Scenario: First open shows collapsed sections

- **WHEN** the user picks `File → Settings` for the first time in an editor session
- **THEN** every section header is visible
- **AND** only the header rows render — no inner controls are visible
- **AND** the HDR/LDR toggle, CSM toggle, CSM tuning fields, and postprocess chain editor are hidden until `Render` is expanded

#### Scenario: Clicking a header expands that section

- **WHEN** the user clicks a collapsed section header
- **THEN** the section expands and its inner widgets render
- **AND** the other sections remain in their collapsed state

### Requirement: Per-section open/closed state SHALL persist across editor runs

The Settings window SHALL persist each section's open/closed state to the editor's settings store. On the next editor launch, the Settings window SHALL restore the saved state before the first frame.

A user who never opens the Settings window SHALL see the same default-collapsed behaviour every run; only sections the user has explicitly touched SHALL have their persisted state honoured.

#### Scenario: Expanded sections stay expanded after restart

- **WHEN** the user expands the `Render` section, closes the editor, and relaunches
- **THEN** the Settings window opens with `Render` already expanded
- **AND** the other sections (untouched) remain collapsed

#### Scenario: HDR toggle is hidden until Render is expanded

- **WHEN** the Settings window opens with the `Render` section collapsed
- **THEN** the HDR/LDR toggle is not visible
- **AND** the scene's HDR mode is still active (the toggle is just a UI affordance, not the source of truth)

#### Scenario: Postprocess-chain editor is hidden until Postprocess is expanded

- **WHEN** the Settings window opens with the `Postprocess` section collapsed
- **THEN** the chain editor table is not rendered
- **AND** the active pipeline still applies the configured chain (the editor is just a UI surface)
