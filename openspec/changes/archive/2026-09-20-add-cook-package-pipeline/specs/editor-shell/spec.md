# editor-shell (delta)

## ADDED Requirements

### Requirement: The editor SHALL provide a File -> "Package..." flow that drives furye-cli

The furye editor's File menu SHALL contain a "Package..." item directly after "Save As..." with no keyboard shortcut, enabled only when a scene is loaded and has a saved on-disk path. The item SHALL open a settings modal offering: pak compression (`lz4` default, `none`), texture target (Host default / legacy / modern), output folder (default: the scene's directory, with a native folder picker), and a verbose toggle. Confirming SHALL spawn the sibling `furye-cli` binary (`package <scene> --compression ... --output <folder>/<stem>.pak` [+ target/verbose flags]) as a subprocess and switch to a modal progress view that blocks the rest of the UI, streams the subprocess's combined stdout/stderr into a scrolling multiline log (pin-to-bottom unless the user scrolled up, bounded buffer), and offers Cancel (terminates the subprocess and its process group) while running and Close when finished. Completion SHALL show "Package complete: <pak path>" on exit 0 or "Package failed (exit N)" otherwise.

#### Scenario: Menu item placement and enablement

- **WHEN** a scene with a saved path is loaded in furye
- **THEN** File menu shows "Package..." directly after "Save As..." with no shortcut and enabled
- **AND** with an unsaved new scene the item is disabled

#### Scenario: Settings to command line

- **WHEN** the user picks compression none, target legacy, folder /tmp/out and confirms
- **THEN** the spawned command is `<exe dir>/furye-cli package <scene> --compression none --output /tmp/out/<stem>.pak --texture-target legacy`
- **AND** the progress modal streams the CLI's log lines as they arrive

#### Scenario: Cancel terminates the subprocess

- **WHEN** the user clicks Cancel mid-package
- **THEN** the subprocess (and its process group) is terminated
- **AND** the dialog shows "Cancelled by user" with a Close button
