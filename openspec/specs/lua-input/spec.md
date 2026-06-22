# lua-input

## Purpose

Lua-facing bindings for the engine's input polling layer plus a minimal ImGui forwarding surface, together with the `examples/Demo.lua` flythrough camera that drives the bindings. The capability covers the `Key` and `MouseButton` enum tables, the `InputUtil` singleton accessors, the `Gui` menu-bar extension point and its thin ImGui forwarders, and the Demo script that exercises all of the above to deliver a script-driven flythrough camera with live ImGui tuning sliders.

## Requirements

### Requirement: Lua SHALL expose a Key enum table for keyboard input

The Lua binding layer SHALL register a global table `Key` whose fields map readable names to the integer values used by `sf::Keyboard::Key` (so that they can be passed to `InputUtil` query functions). The table SHALL include at minimum: letters `A` through `Z`, digits `Num0` through `Num9`, `Space`, `LShift`, `RShift`, `LControl`, `RControl`, `LAlt`, `RAlt`, `Up`, `Down`, `Left`, `Right`, `Escape`, `Enter`, `Tab`, `Backspace`. The integer values stored in `Key` SHALL be identical to the underlying `static_cast<int>(sf::Keyboard::Key::X)` so that pass-through to `InputUtil` indexes the correct slot.

#### Scenario: Key.W matches sf::Keyboard::Key::W

- **WHEN** a Lua script reads `Key.W`
- **THEN** the returned integer equals `static_cast<int>(sf::Keyboard::Key::W)`
- **AND** passing `Key.W` to `InputUtil:GetKeyDown(...)` returns `true` while the W key is physically held

#### Scenario: Common navigation keys are bound

- **WHEN** a Lua script reads `Key.Up`, `Key.Down`, `Key.Left`, `Key.Right`, `Key.Space`, `Key.LShift`
- **THEN** none of the reads return `nil`
- **AND** each value is a number

### Requirement: Lua SHALL expose a MouseButton enum table

The Lua binding layer SHALL register a global table `MouseButton` with fields `Left`, `Right`, `Middle`, mapped to the corresponding `sf::Mouse::Button` integer values.

#### Scenario: MouseButton.Left maps to sf::Mouse::Button::Left

- **WHEN** a Lua script reads `MouseButton.Left`
- **THEN** the returned integer equals `static_cast<int>(sf::Mouse::Button::Left)`
- **AND** passing it to `InputUtil:GetMouseDown(MouseButton.Left)` returns `true` while the left mouse button is pressed

### Requirement: Lua SHALL expose InputUtil polling accessors

The Lua binding layer SHALL register `InputUtil` as a usertype with a static `Instance()` returning the singleton, and the following methods bound on the singleton (mirroring the C++ shape):

- `GetKeyDown(key: integer) -> boolean` — reads `m_KeyDown[key]`.
- `GetMouseDown() -> boolean` — true if any mouse button is currently pressed.
- `GetMouseDown(btn: integer) -> boolean` — reads `m_MouseDown[btn]`.
- `GetMousePosition() -> (number, number)` — the cached `(x, y)` from the most recent `MouseMoved` event, in window-relative point coordinates.
- `GetMouseWheel() -> number` — the last-frame scroll delta.
- `GetWindowFocused() -> boolean`.
- `GetWindowSize() -> (number, number)` — the current window size in points (matches `sf::Window::getSize`).

All accessors SHALL return the engine-side cached state — the same state populated by `Engine::HandleEvent`. The accessors SHALL be safe to call from any Lua callback (`on_update`, `on_fixed_update`, signal-connected closures). Returning `nil` from a method is a binding bug.

#### Scenario: Polling key state inside on_update

- **WHEN** a Lua `on_update(dt)` callback calls `InputUtil.Instance():GetKeyDown(Key.W)`
- **THEN** the return value is `true` while W is held, `false` otherwise
- **AND** transitions track per-frame, with one frame of latency at most

#### Scenario: Mouse-position read

- **WHEN** a Lua script calls `local x, y = InputUtil.Instance():GetMousePosition()`
- **THEN** `x` and `y` are numbers in the range `[0, window_width]` and `[0, window_height]` respectively while the cursor is inside the window
- **AND** both values reflect the most recent `MouseMoved` event captured by `Engine::HandleEvent`

### Requirement: Demo.lua SHALL implement a flythrough camera using the new bindings

`examples/Demo.lua` SHALL drive the existing `cam_node` per-frame from `on_update(dt)` using only the new Lua bindings. The control scheme SHALL be:

- `W` / `Up` → translate forward along the camera's local −Z.
- `S` / `Down` → translate backward (+Z).
- `A` / `Left` → strafe left (−X).
- `D` / `Right` → strafe right (+X).
- `Space` → translate up (world +Y).
- `LControl` → translate down (world −Y).
- `LShift` held → multiplies move speed by 5×.
- Left mouse button held + cursor moves → yaw on world Y by `Δx · sensitivity`, pitch on local X by `Δy · sensitivity`, clamped to ±89°.
- Mouse wheel up/down → bump/cut the base move speed (clamped to a sane range).

After mutating position/rotation, the script SHALL call `cam_node:Recompose(false)` to flush the local transform.

#### Scenario: Camera moves forward when W is held

- **WHEN** the demo is running and the user holds W for one second
- **THEN** the camera's local position changes monotonically along its forward axis by approximately `move_speed × 1s`
- **AND** holding W and LShift simultaneously increases the per-second delta to roughly `5 × move_speed`

#### Scenario: Mouse drag yaws and pitches the camera

- **WHEN** the user holds left mouse button and drags the cursor right by 100 pixels
- **THEN** the camera's yaw advances by approximately `100 × sensitivity` radians (or degrees, per implementation)
- **AND** pitch is clamped: continuous upward drag does not roll the view past ±89°

#### Scenario: No camera motion when the window is unfocused

- **WHEN** the demo window loses focus (e.g., user Cmd-Tabs away)
- **THEN** held keys are no longer treated as "down" for the next frame
- **AND** the camera stops moving until focus returns and a key transitions down again

### Requirement: Demo.lua SHALL expose live tuning sliders for camera feel

`examples/Demo.lua` SHALL render a small ImGui panel during `on_update` that exposes two `SliderFloat` controls writing back into the script's local state:

- `Move Speed` — clamped to `[0.5, 50.0]`, default `8.0`.
- `Mouse Sensitivity` — clamped to `[0.0005, 0.02]`, default `0.0035`.

The script's flythrough math SHALL read these values every frame, so dragging a slider during runtime immediately changes the feel without restart. The panel SHALL be implemented either by (a) a Lua-side wrapper around a small set of ImGui bindings, or (b) a C++ helper `Gui::SliderFloat(label, value_ptr, min, max)` (and a `Gui::Begin` / `Gui::End` pair) callable from Lua — see design.md for the chosen approach.

#### Scenario: Sliders are visible and labeled

- **WHEN** the demo runs
- **THEN** an ImGui window titled "Camera" (or "Flythrough", per implementation) is visible alongside the existing Profiler
- **AND** it contains two sliders labeled "Move Speed" and "Mouse Sensitivity"
- **AND** each slider displays its current numeric value

#### Scenario: Slider drag changes camera feel live

- **WHEN** the user drags "Move Speed" from `8.0` to `20.0`
- **THEN** the next subsequent W-key press moves the camera at the new speed
- **AND** no rebuild or restart is required

### Requirement: Gui SHALL expose a menu-bar extension point and minimal ImGui forwarders

`Gui::ShowDefault` SHALL render a `File` menu (with `Quit`) and a `View` menu (with `Profiler`, `GBuffer`, `Shadow Buffers` toggles, default-hidden). After the built-in menus, `Gui::ShowDefault` SHALL invoke an optional Lua-registered callback so scripts can append their own top-level menus. The Lua-facing surface SHALL include thin forwarders for `Gui.Begin(title, open) → (still_open, visible)`, `Gui.End()`, `Gui.SliderFloat(label, value, min, max) → float`, `Gui.Checkbox(label, value) → bool`, `Gui.Button(label) → bool`, `Gui.Separator()`, `Gui.Text(str)`, `Gui.BeginMenu(label) → bool`, `Gui.EndMenu()`, `Gui.MenuItem(label) → bool`, and `Gui.SetMenuBarCallback(fn_or_nil)`. The `Gui.Begin` two-value return enables a close (X) button on every Lua-drawn window; the caller stores `still_open` back into its visibility flag.

#### Scenario: Default view is clean

- **WHEN** the demo launches with no script-side menu interactions
- **THEN** no Profiler / GBuffer / Shadow Buffers / Camera Settings window is visible
- **AND** only the menu bar (`File`, `View`, plus any Lua-added menus) is on screen

#### Scenario: Menu toggles built-in panels

- **WHEN** the user clicks `View → Profiler`
- **THEN** the Profiler window appears with its FPS plot, memory readouts, and counters
- **AND** clicking the window's X button hides it again and unchecks the menu item

#### Scenario: Lua adds a "Camera" menu

- **WHEN** `Demo.lua` calls `Gui.SetMenuBarCallback(fn)` where `fn` opens a `BeginMenu("Camera") / MenuItem("Settings") / EndMenu()` block
- **THEN** a `Camera` menu appears in the main menu bar after `View`
- **AND** clicking `Settings` flips the Lua-side `show_camera_window` flag
- **AND** the Camera panel becomes visible the next frame, with its own X close button driven by `Gui.Begin`'s `still_open` return
