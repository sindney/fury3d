## ADDED Requirements

### Requirement: Every settings window section SHALL open collapsed

The Engine Settings window SHALL open with every collapsible section in its collapsed state. Examples of sections: `Camera`, `Render`, `Import`, `Postprocess`, `Sky` (if present), and any project-registered settings section.

A "collapsed" section SHALL render only its header row (the section title and the section's primary toggle, if any) and SHALL NOT render its inner widgets. The header row SHALL remain interactive: clicking the header toggles the section open/closed; the primary toggle (if present) SHALL still be wired to its setting.

The collapsed-default state applies to the first time the Settings window is opened in a given process. After the user expands a section, that section's open state is remembered for the lifetime of the editor process (see the persistence requirement below).

#### Scenario: First open shows collapsed sections

- **WHEN** the user picks `File → Settings` for the first time in an editor session
- **THEN** every section header is visible
- **AND** only the header rows render — no inner controls are visible
- **AND** the window is sized to fit the headers (no extra vertical space reserved for expanded contents)

#### Scenario: Clicking a header expands that section

- **WHEN** the user clicks a collapsed section header
- **THEN** the section expands and its inner widgets render
- **AND** the other sections remain in their collapsed state

#### Scenario: Clicking an expanded header re-collapses it

- **WHEN** the user clicks an already-expanded section header
- **THEN** the section collapses and its inner widgets are hidden

### Requirement: Per-section open/closed state SHALL persist across editor runs

The Settings window SHALL persist each section's open/closed state to the editor's settings store (the same location that holds camera tuning and other per-user state). On the next editor launch, the Settings window SHALL restore the saved state before the first frame is rendered.

State persistence SHALL include:

- The window's open/closed visibility itself (already implicit via the existing `Settings` toggle).
- Each section's expanded flag, keyed by the section's stable identifier (e.g. `render`, `camera`, `import`, `postprocess`, `sky`).
- The window's position and size (preserved from the existing settings behaviour).

A user who never opens the Settings window SHALL see the same default-collapsed behaviour every run; only sections the user has explicitly touched SHALL have their persisted state honoured.

#### Scenario: Expanded sections stay expanded after restart

- **WHEN** the user expands the `Render` section, closes the editor, and relaunches
- **THEN** the Settings window opens with `Render` already expanded
- **AND** the other sections (untouched) remain collapsed

#### Scenario: Untouched sections default to collapsed

- **WHEN** the user has only ever expanded `Render` and relaunches the editor
- **THEN** `Camera`, `Import`, `Postprocess`, and `Sky` open in the collapsed state
- **AND** `Render` is expanded

#### Scenario: Corrupt or missing settings file

- **WHEN** the editor's settings file is missing or unreadable
- **THEN** every section opens in the collapsed state (the safe default)
- **AND** the editor writes a default settings file on the next persistence cycle
