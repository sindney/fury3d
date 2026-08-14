# editor-settings-collapsed-default

## Purpose

The Engine Settings window opens with every collapsible section closed so the first frame is not an overwhelming dashboard of toggles. Sections expand on user click; the user's choices are remembered for the editor process lifetime via ImGui's normal window storage.

## Requirements

### Requirement: Every settings window section SHALL open collapsed by default

The Engine Settings window SHALL render every collapsible section (`Editor`, `Camera`, `Render`, `Import`, `Engine`, plus any project-registered section) in its collapsed state on first appearance and after a fresh process launch where the user has not touched the section's header.

Implementation: each section heading is preceded by `ImGui::SetNextItemOpen(false, ImGuiCond_FirstUseEver)`. Sections with children only render their header row until the user expands them; the header row remains interactive (clicking the header toggles open/closed).

#### Scenario: First open shows collapsed sections

- **WHEN** the user opens the Settings window in a fresh editor process
- **THEN** every section header is visible
- **AND** only the header rows render — no inner controls are visible

#### Scenario: Clicking a header expands the section

- **WHEN** the user clicks a collapsed section header
- **THEN** the section expands and its inner widgets render
- **AND** the other sections remain collapsed

#### Scenario: Clicking an expanded header re-collapses it

- **WHEN** the user clicks an already-expanded section header
- **THEN** the section collapses and its inner widgets are hidden
