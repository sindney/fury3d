## ADDED Requirements

### Requirement: The `Engine.run` options table SHALL accept `dpi_aware_override`

The `Engine.run(callbacks, options?)` Lua binding SHALL accept the following additional key on the options table:

- `dpi_aware_override` (boolean, default `false`) — when `true`, the engine multiplies the caller's `gui_scale` by the system DPI. See the `platform-window-dpi` capability.

The Lua bindings SHALL read the key via `opts_table.get_or<bool>(key, default)` semantics, matching the existing `max_fps`, `gui_scale`, `gui_font_scale` keys. Unknown keys SHALL be silently ignored (no exception, no log entry).

The new key SHALL be additive: existing call sites that pass only `max_fps` (or no options at all) compile and run unchanged.

#### Scenario: dpi_aware_override composes with gui_scale

- **WHEN** a Lua script calls `Engine.run(callbacks, { gui_scale = 1.0, dpi_aware_override = true })`
- **AND** the system DPI is `2.0`
- **THEN** the effective `gui_scale` is `2.0`

#### Scenario: Unknown keys are ignored

- **WHEN** a Lua script passes `{ dpi_aware_override = true, no_such_key = "x" }`
- **THEN** `dpi_aware_override` is `true`
- **AND** `no_such_key` is silently dropped without log spam
