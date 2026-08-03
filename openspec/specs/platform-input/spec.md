# platform-input

## Purpose

Cross-platform input-layer correctness for the editor. Bounds-checks SFML key events to prevent out-of-bounds writes, bootstraps and re-seeds the focus state so the first user click after launch routes to the editor's hit-tests, and provides a transient-state reset helper for focus round trips.

## Requirements

### Requirement: `InputUtil` SHALL expose a `ResetTransientInputState()` helper

`InputUtil::ResetTransientInputState()` SHALL zero every entry of `m_KeyDown`, every entry of `m_MouseDown`, `m_MouseWheel`, and `m_MousePosition`. The helper is called by `Engine::HandleEvent` on `FocusLost` (to drop held-key/button state that SFML may have failed to deliver while unfocused) and on `FocusGained` (before seeding the cursor position). It is also exposed for callers that need to reset input state without a focus event.

#### Scenario: ResetTransientInputState clears held input

- **WHEN** the engine calls `InputUtil::ResetTransientInputState()`
- **THEN** every entry of `m_KeyDown` is `false`
- **AND** every entry of `m_MouseDown` is `false`
- **AND** `m_MouseWheel` is `0.0f`
- **AND** `m_MousePosition` is reset to `(0, 0)`

### Requirement: The engine SHALL bounds-check `sf::Event::KeyPressed` and `KeyReleased` codes to prevent out-of-bounds writes (Windows-specific)

The engine's `Engine::HandleEvent` SHALL reject any `sf::Event::KeyPressed` or `sf::Event::KeyReleased` whose `code` is `sf::Keyboard::Key::Unknown` (value `-1`) or otherwise outside the valid range `[0, sf::Keyboard::KeyCount)`. The check is required because the SFML Win32 backend's `virtualKeyCodeToSF()` (in `engine/ThirdParty/SFML/src/SFML/Window/Win32/WindowImplWin32.cpp:1174-1293`) returns `sf::Keyboard::Key::Unknown` for any virtual key code that does not map to a known SFML key — Windows IME virtual keys (`VK_PROCESSKEY = 0xE5`, IME composition virtual keys) and various media / vendor-specific keys fall through the `default:` branch. SFML still pushes those `KeyPressed` / `KeyReleased` events into the event queue, and the previous Fury code cast the enum to `unsigned int` and indexed `m_KeyDown[0xFFFFFFFF]`, writing 4.3 billion entries past the array.

The check SHALL be wrapped in `#if PLATFORM_WINDOWS` because the bug is currently Windows-only — only the SFML Win32 backend returns `Key::Unknown` for IME composition virtual keys. The macOS / Linux SFML backends do not exhibit this behavior. The check uses the `PLATFORM_WINDOWS` macro from `engine/Fury/Macros.h` (newly added); the macro resolves to `1` on Windows builds and `0` elsewhere. The engine SHALL still include `Fury/Macros.h` in `Engine.cpp` so the guard is in scope.

The check SHALL be the first statement inside the `KeyPressed` and `KeyReleased` branches of `Engine::HandleEvent`:

```cpp
#if PLATFORM_WINDOWS
    if (key->code == sf::Keyboard::Key::Unknown) return;
    if (static_cast<unsigned int>(key->code) >= sf::Keyboard::KeyCount) return;
#endif
```

Rejection SHALL be silent (no `FURYW` log per event) — the event rate is too high under an active IME to log per-event. The engine SHALL instead emit a single `FURYW << "rejected N invalid KeyPressed events this frame"` summary at end-of-frame when at least one invalid key was dropped that frame (computed in `Engine::Run`).

The `PLATFORM_WINDOWS` guard is a safety pin: the same check is harmless on other platforms, but the bug it fixes is Windows-specific. If a future SFML port (Wayland + IME, X11 + IMEs) starts emitting `Key::Unknown` events for unrecognized virtual keys, the corresponding `PLATFORM_LINUX` / future-`PLATFORM_WAYLAND` branch can be added with the same two lines.

#### Scenario: Unknown key code is rejected

- **WHEN** the engine receives a `KeyPressed` event with `code == sf::Keyboard::Key::Unknown`
- **THEN** no write to `m_KeyDown` occurs
- **AND** no `OnKeyDown` signal is emitted
- **AND** no `FURYW` log line is emitted for that single event

#### Scenario: Out-of-range key code is rejected

- **WHEN** the engine receives a `KeyPressed` event with `code` outside `[0, KeyCount)`
- **THEN** no write to `m_KeyDown` occurs
- **AND** no `OnKeyDown` signal is emitted

#### Scenario: Valid key code is processed normally

- **WHEN** the engine receives a `KeyPressed` event with `code == sf::Keyboard::Key::W`
- **THEN** `m_KeyDown[static_cast<unsigned int>(sf::Keyboard::Key::W)]` is set to `true`
- **AND** `OnKeyDown->Emit(sf::Keyboard::Key::W)` fires

#### Scenario: Chinese IME active no longer corrupts memory

- **WHEN** the user has a Chinese IME active
- **AND** presses WASD (which generates a flurry of `KeyPressed` events with `code == Key::Unknown` for `VK_PROCESSKEY` and IME composition virtual keys)
- **THEN** no out-of-bounds write occurs
- **AND** the editor does not crash

### Requirement: The engine SHALL seed the mouse position on `FocusGained` so the first click is accurate

When the engine receives `sf::Event::FocusGained` (or, on macOS, an `sf::Event::MouseEntered` returning `true` after a native open dialog dismisses), the engine SHALL:

1. Call `sf::Mouse::getPosition(window)` and write the result to `InputUtil::m_MousePosition` (the engine's cached cursor position).
2. Call `InputUtil::ResetTransientInputState()` to drop any key/button state that SFML may have failed to deliver while the window was unfocused.
3. Emit a synthetic `OnMouseMove(mx, my)` signal with the seeded coordinates so editor code paths that only update on `OnMouseMove` (the Viewport-window hit-test, the gizmo, the camera orbit) observe the seed on the same frame.
4. Emit `OnWindowFocus(true)` as it does today.

The seed SHALL fire exactly once per focus-gain transition, NOT on every `FocusGained` event in the OS's focus loop.

On macOS, the same seed SHALL also fire on `sf::Event::MouseEntered` returning `true` (the open-dialog dismissal path), so the editor's cached mouse state catches up after the dialog releases its modal loop.

#### Scenario: First focus after launch seeds the cursor

- **WHEN** the engine window first receives `FocusGained`
- **THEN** `InputUtil::m_MousePosition` equals the current OS cursor position (read via `sf::Mouse::getPosition(window)`)
- **AND** one `OnMouseMove` is emitted with that position
- **AND** `OnWindowFocus(true)` is emitted

#### Scenario: Subsequent focus-gains do not re-seed

- **WHEN** the engine loses focus and regains it 10 seconds later
- **THEN** exactly one seed-and-emit cycle happens (not one per `FocusGained` in the OS loop)

#### Scenario: macOS open dialog dismissal seeds the cursor

- **WHEN** the user dismisses the native open dialog
- **AND** the engine receives `sf::Event::MouseEntered` with `true`
- **THEN** `InputUtil::m_MousePosition` is updated to the OS cursor position at the moment of the `MouseEntered` event
- **AND** one `OnMouseMove` is emitted

#### Scenario: Held keys are dropped on focus gain

- **WHEN** the engine regains focus
- **AND** SFML had not delivered release events for a key held before focus was lost (so `m_KeyDown` is stale)
- **THEN** `InputUtil::ResetTransientInputState()` clears the stale state
- **AND** the `OnMouseMove` emission is the only post-focus-gain event besides `OnWindowFocus(true)`

### Requirement: The engine SHALL bootstrap the focus state at `Engine::Initialize` from `window.hasFocus()`

`Engine::Initialize` SHALL bootstrap the `InputUtil::m_WindowFocused` field from the live window's focus state. Specifically, after creating the `sf::Window` and after `InputUtil::Initialize` has been called, the engine SHALL call `window.hasFocus()`. If the call returns `true`, the engine SHALL set `InputUtil::m_WindowFocused = true` and emit `InputUtil::OnWindowFocus->Emit(true)`. If the call returns `false`, the field is left at its existing default (`false`) and no signal is emitted (the `FocusGained` event will fire when the user gives the window focus later).

The bootstrap is required because:

- `InputUtil::m_WindowFocused` is initialized to `false` in `InputUtil.h:27`.
- `sf::Window` is created with `WS_VISIBLE` via `CreateWindowW` in `engine/ThirdParty/SFML/src/SFML/Window/Win32/WindowImplWin32.cpp:190, 215`; the window is shown immediately.
- Windows does NOT fire `WM_SETFOCUS` for a window that is created already visible and given foreground implicitly (it only fires `WM_SETFOCUS` for explicit focus transitions).
- SFML's Win32 backend only handles `WM_SETFOCUS` / `WM_KILLFOCUS`; it does NOT handle `WM_ACTIVATE` (see `WindowImplWin32.cpp:831-847`). So a window shown already-focused never receives an SFML `FocusGained` event, and `m_WindowFocused` stays `false` until the user manually triggers a focus transition (alt-tab, title-bar click).
- The Lua drag gate at `examples/Editor.lua:705` reads `focused = input:GetWindowFocused()`, so a stale `false` value silently disables mouse drag.

The bootstrap is one-shot per process; it runs exactly once in `Engine::Initialize`, never again. Subsequent focus transitions are owned by the existing `FocusLost` / `FocusGained` branches in `Engine::HandleEvent`.

The bootstrap is NOT Windows-only — `sf::Window::hasFocus()` is a cross-platform API and the same defensive read is useful on macOS / Linux. The bug that surfaced it is Windows-specific (the Win32 focus-event mapping is sparse), but the bootstrap code runs on every platform. No `PLATFORM_WINDOWS` guard is needed.

#### Scenario: Window created with focus boots focused

- **WHEN** the engine is launched (double-click or command line) and the launching shell is not in the foreground
- **THEN** `sf::Window::hasFocus()` returns `true` at the end of `Engine::Initialize`
- **AND** `InputUtil::m_WindowFocused` is `true`
- **AND** `InputUtil::OnWindowFocus(true)` has fired exactly once before `Engine::Run` enters its main loop
- **AND** the first user mouse-drag interaction works without requiring a focus transition

#### Scenario: Window created without focus stays unfocused

- **WHEN** the engine is launched while another window has the foreground (e.g. the launching shell's terminal is in focus)
- **THEN** `sf::Window::hasFocus()` returns `false` at the end of `Engine::Initialize`
- **AND** `InputUtil::m_WindowFocused` remains `false`
- **AND** no `OnWindowFocus` signal is emitted during `Engine::Initialize`
- **AND** the existing `FocusGained` event handling in `Engine::HandleEvent` owns the rest of the focus state machine

#### Scenario: After dialog dismissal, bootstrap state survives

- **WHEN** the user opens the native file dialog and dismisses it
- **AND** the engine never receives a `FocusGained` event from the OS (the dialog's modal loop did not redeliver focus to the parent HWND)
- **THEN** `m_WindowFocused` is whatever it was before the dialog opened
- **NOTE**: this is the existing Windows behavior; the bootstrap is a one-shot and does not re-run on dialog dismissal. The `FocusGained` seed requirement above handles dialog dismissal in the macOS / non-Vista-fallback case; the Windows case requires a manual focus transition, which is the pre-existing limitation and out of scope for this change.
