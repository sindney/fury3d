## Context

The furye editor has three independent Windows-first regressions that all surface at the platform boundary:

- **IME / WASD crash** — `Engine::HandleEvent` writes `m_KeyDown[static_cast<unsigned int>(key->code)] = true` with no bounds check. `sf::Keyboard::Key::Unknown` is `-1` (cast to `0xFFFFFFFF`). SFML's Win32 backend (`engine/ThirdParty/SFML/src/SFML/Window/Win32/WindowImplWin32.cpp:1174-1293`, `virtualKeyCodeToSF`) returns `Key::Unknown` for any `wParam` it does not recognize, including `VK_PROCESSKEY` (0xE5) and IME composition virtual keys. SFML still pushes those `KeyPressed` events into the queue, and the engine writes 4.3 billion entries past the end of `m_KeyDown[]`. The bug surfaces only when an IME is active because that's when the OS generates the unknown virtual keys; pressing WASD under the IME triggers the OOB write and the editor crashes.
- **Mouse drag dead on first launch** — left-mouse drag does not start a viewport drag (no pick, no gizmo, no camera orbit) on the very first interaction. Toggling the window to the background and back once restores it. On macOS, the same symptom appears after the native open-file dialog returns. Root cause: `InputUtil::m_WindowFocused` is initialized to `false` (`engine/Fury/InputUtil.h:27`). `sf::Window` is created with `WS_VISIBLE` via `CreateWindowW` (`engine/ThirdParty/SFML/src/SFML/Window/Win32/WindowImplWin32.cpp:190, 215`) so the window is shown immediately. Windows does NOT auto-fire `WM_SETFOCUS` for a window shown this way (only for explicit focus transitions). SFML's Win32 backend only maps `WM_SETFOCUS` / `WM_KILLFOCUS` to `Event::FocusGained` / `FocusLost`; it does NOT handle `WM_ACTIVATE` (see `WindowImplWin32.cpp:831-847`). So `m_WindowFocused` stays `false` until the user manually triggers a focus transition (alt-tab, title-bar click). The Lua drag gate at `examples/Editor.lua:705` reads `lmb_down = focused and has_mo and input:GetMouseDown(MouseButton.Left) ...`, so drag is dead even though mouse events are flowing. On Windows, NFD's `IFileOpenDialog::Show` is modal and the parent HWND does NOT receive `WM_SETFOCUS` after the dialog closes — the same flag stays `false` permanently until a manual focus event. On macOS, NFD does call `[keyWindow makeKeyAndOrderFront:nil]` after `runModal`, which usually re-fires `NSWindowDidBecomeKeyNotification`, but the timing race makes it occasional.
- **GUI too small on Windows HiDPI** — at 200% system display scale, the editor window renders at native pixel size with no DPI awareness. The `fix-demo-fps-profiler-retina` change (archived 2026-06-23) defaulted `EngineOptions::gui_scale` to `1.0f` to fix the macOS Retina double-scaling; that left Windows HiDPI uncovered. The engine has no application manifest today (MSVC's default `<GenerateManifest>true</GenerateManifest>` produces a stock manifest with no `<dpiAwareness>` element). SFML's Win32 backend at `engine/ThirdParty/SFML/src/SFML/Window/Win32/WindowImplWin32.cpp:70-125` only sets `ProcessPerMonitorDpiAware` (v1, via the legacy SHCore API) and explicitly does not call `SetProcessDpiAwarenessContext` / Per-Monitor V2. So the editor process is "Per Monitor DPI Aware" (v1) at best, with no automatic non-client-area scaling and no `WM_DPICHANGED` redelivery when the user drags between monitors. Additionally, the ImGui 1.92 `io.FontGlobalScale` field used in `engine/Fury/Gui.cpp:48` is deprecated in favor of `style.FontScaleMain` (`engine/ThirdParty/ImGui/imgui.cpp:543`).

Constraints:
- Vendored SFML 3.1.2 is the only windowing layer; the engine cannot change SFML's event semantics. Bounds-check on the Fury side is the only fix.
- The engine already drops held keys on `FocusLost` (`engine/Fury/Engine.cpp:130-146`); symmetry on `FocusGained` is the natural place for the mouse-seed fix.
- The DPI fix must not regress the macOS Retina path: the existing `gui_scale = 1.0` default is correct for macOS where SFML 3 forces `highDpi=NO` (see `docs/ARCHITECTURE.md` §16). The new `gui_scale = 0.0` sentinel + system-DPI read only activates on Windows, and only when the caller doesn't pass an explicit value.
- The `fix-demo-fps-profiler-retina` change shipped `EngineOptions` with `gui_scale = 1.0f` as a default. Changing the default to `0.0f` would be a silent behavior change for any caller that already passes `EngineOptions{...}` without explicitly setting `gui_scale`. The fix is to use `0.0f` as a new sentinel meaning "let the engine decide" while keeping `1.0f` as the explicit default for non-sentinel callers.

## Goals / Non-Goals

**Goals:**
- Stop the OOB write that crashes the editor under any active IME.
- Make the first click after window launch (or after a focus loss) route to the editor's hit-tests reliably.
- Make the editor's GUI proportional to the system DPI on Windows without regressing the macOS Retina path.
- Expose the new IME / focus / DPI state to Lua scripts via `InputUtil.IsIMEComposing()` so projects can branch on it (e.g. disable hotkeys while typing Chinese).
- Keep all existing `EngineOptions` and `InputUtil` callers working without recompilation.

**Non-Goals:**
- Implementing true Retina rendering on macOS (requires an SFML patch; already documented as future work in §16).
- Hot-reload of the DPI scale when the window is dragged between monitors of different DPI (DPI is read once at init, matching the once-at-init GUI scale contract).
- Per-IME-API integration beyond composition detection (e.g. inline composition strings, candidate lists). The engine's only IME concern is "is composition in progress, so we should not route shortcuts"; text inputs already get the codepoints via `OnTextEntered`.
- Patching vendored SFML. The OOB is the engine's responsibility; SFML's event semantics are upstream.

## Decisions

### Decision 1: Bounds-check `key->code` in `Engine::HandleEvent`, guarded with `PLATFORM_WINDOWS`, not in SFML

The fix lives in the engine's `Engine::HandleEvent`, not in vendored SFML. The check is two lines per branch, wrapped in `#if PLATFORM_WINDOWS` because the bug is Windows-specific:

```cpp
#if PLATFORM_WINDOWS
    if (key->code == sf::Keyboard::Key::Unknown) return;
    if (static_cast<unsigned int>(key->code) >= sf::Keyboard::KeyCount) return;
#endif
```

The `PLATFORM_WINDOWS` macro is defined in `engine/Fury/Macros.h` (new in this change) alongside `PLATFORM_MACOS`, `PLATFORM_LINUX`, `PLATFORM_IOS`, `PLATFORM_ANDROID`, and `PLATFORM_DESKTOP` / `PLATFORM_MOBILE` aggregates. The new macros are added once at the top of `Macros.h` and used in lieu of raw `defined(_WIN32)` / `__APPLE__` / `__linux__` checks throughout the engine. The engine SHALL include `Fury/Macros.h` in `Engine.cpp` so the guard is in scope. The guard is a safety pin — the same check is harmless on other platforms, but the bug it fixes is Windows-only. If a future SFML port starts emitting `Key::Unknown` events for unrecognized virtual keys, the corresponding `PLATFORM_LINUX` branch can be added with the same two lines.

**Alternative considered:** patch `WindowImplWin32.cpp` to drop unknown keys before `pushEvent`. Rejected — the vendored SFML is a third-party submodule; touching it complicates future upstream merges and provides no benefit (the engine is the only consumer of the events).

**Alternative considered:** make `m_KeyDown` a `std::map` or `std::unordered_map` so out-of-range keys no-op. Rejected — perf cost (every `GetKeyDown` becomes a map lookup) and the bounds check is the smallest change that fixes the root cause.

**Alternative considered:** leave the bounds-check unguarded (always on, on every platform). Rejected by the user's platform-macro convention — the engine should make Windows-specific defensive code visible as such, not hide it inside a cross-platform `if`. The PLATFORM_WINDOWS guard is also a forcing function: when a future platform exhibits the same bug, the contributor adds a new branch instead of wondering "is this even relevant here?"

### Decision 2: Composition-state heuristic, not a Windows-only IME API

The engine has no Windows-specific IME API. Instead, it derives composition state from the existing event stream:

- `composing` becomes `true` when the engine sees a `TextEntered` with a canonical-ASCII codepoint (0x20–0x7E, excluding 0x7F) and no `KeyPressed` for `Backspace`/`Enter`/`Escape` in the same frame.
- `composing` becomes `false` when the engine sees a `KeyPressed` for `Enter` (commit) or `Escape` (cancel).

The heuristic matches the user-visible behavior: a Chinese IME in composition emits candidate-window previews as ASCII codepoints, while commit / cancel go through normal key events. False positives are rare (rapid typing of printable characters in a text field does not normally fire Enter or Escape).

The composition-state code lives in `InputUtil.{h,cpp}` and is cross-platform; only the `ResetTransientInputState()` and `OnIMEStateChanged` accessor calls are gated. The bug that motivated the heuristic is Windows-specific, but the API itself is useful on any platform where a future IMEs starts producing `TextEntered` events for composition.

**Alternative considered:** call `ImmGetCompositionStringW` directly on Windows to read the IME composition buffer. Rejected — adds a Windows-specific code path to the engine and provides more data than the editor needs. The heuristic is enough to gate shortcut routing.

**Alternative considered:** drive composition state from `OnTextEntered` alone (any `TextEntered` means composing). Rejected — false positives when the user types in a regular text input (each character is a `TextEntered` but not IME composition).

### Decision 3: Bootstrap `m_WindowFocused` from `window.hasFocus()` at `Engine::Initialize`, then seed mouse on `FocusGained`

The first-launch drag bug is two problems in one:

1. `m_WindowFocused` defaults to `false` and is only set true on a `FocusGained` event. Windows does not fire `WM_SETFOCUS` for a window shown via `CreateWindowW(WS_VISIBLE)`, so the event never arrives.
2. Even after focus is set, the cached `m_MousePosition` is `(0, 0)` until the OS delivers a `MouseMoved` event, so the editor's hit-tests can fail if the user clicks before the cursor has moved inside the window.

The fix is two pieces:

**Bootstrap (one-shot at `Engine::Initialize`):**

```cpp
if (window.hasFocus()) {
    InputUtil::Instance()->m_WindowFocused = true;
    InputUtil::Instance()->OnWindowFocus->Emit(true);
}
```

This corrects the initial `false` default for the common case (launching shell not in foreground → Windows gives focus to the new HWND at creation). Subsequent focus transitions are owned by the existing `FocusLost` / `FocusGained` branches in `Engine::HandleEvent`.

**Mouse seed (one-shot on `FocusGained`):**

1. Read `sf::Mouse::getPosition(window)` and write to `m_MousePosition`.
2. Call `InputUtil::ResetTransientInputState()` to drop any held keys/buttons that SFML failed to deliver while the window was unfocused.
3. Emit one synthetic `OnMouseMove(mx, my)`.
4. Emit `OnWindowFocus(true)` as before.

The seed is one-shot because the engine already updates `m_MousePosition` on every `MouseMoved` after the seed. Re-seeding on every `FocusGained` would clobber subsequent legitimate `MouseMoved` updates.

**Alternative considered:** change `InputUtil`'s ctor default from `false` to `true`. Rejected — the field is `false` for a reason: a window that is created without foreground is genuinely not focused, and the bootstrap can correct the focused case without flipping the unfocused case.

**Alternative considered:** patch SFML's `WindowImplWin32` to also handle `WM_ACTIVATE`. Rejected — vendored SFML; the engine-level bootstrap is one line of code and doesn't touch a submodule.

**Alternative considered:** seed the mouse in the `Engine::Run` main loop BEFORE the first `pollEvent` call. Rejected — the seed must fire AFTER the window has focus, and "the window has focus" is an OS-level event that is not guaranteed to have arrived by the time `Engine::Run` starts. The bootstrap in `Engine::Initialize` already handles the first-launch case; the `FocusGained` seed handles the post-dialog case on macOS.

### Decision 4: DPI awareness via V2 manifest, gated on `PLATFORM_WINDOWS`, not via `SetProcessDpiAwarenessContext` and not via patching SFML

The Windows DPI awareness is declared via a manifest file linked into the `fury` and `furye` binaries. The manifest is a one-time `engine/Resources/fury.exe.manifest` with `<dpiAwareness>PerMonitorV2</dpiAwareness>` and `<dpiAware>true/pm</dpiAware>`. The manifest link in `engine/CMakeLists.txt` is gated on `if(PLATFORM_WINDOWS)` (which currently resolves to `if(WIN32)`); the C++ side includes `Fury/Macros.h` and uses `#if PLATFORM_WINDOWS` where it needs to know the build target.

V2 (not V1) is chosen because:
- V2 re-issues `WM_DPICHANGED` when the user drags the window between monitors of different DPI, enabling future hot-reload of the GUI scale (not in this change, but the wiring is in place).
- V2 gives the OS enough information to apply automatic non-client-area scaling (title bar, borders), which the user sees as crisp 2× chrome.
- SFML's Win32 backend at `WindowImplWin32.cpp:70-125` explicitly does not call `SetProcessDpiAwarenessContext` (it calls only the legacy `SetProcessDpiAwareness(ProcessPerMonitorDpiAware)` from SHCore). The manifest is the only way to opt in to V2 without patching vendored SFML.

**Alternative considered:** call `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` in `Engine::Initialize` at runtime. Rejected — runtime calls happen after the OS has already decided the window's initial scale, so the first frame may be drawn at the wrong DPI. Manifest declaration is processed by the OS before the process starts and applies to the window's initial layout. Manifest is the conventional fix for per-monitor V2.

**Alternative considered:** patch SFML's `WindowImplWin32` to call `SetProcessDpiAwarenessContext`. Rejected — vendored SFML is a submodule; touching it complicates future upstream merges and provides no benefit (the manifest opt-in is upstream-friendly).

**Alternative considered:** declare DPI awareness in the engine's main CMake file with `set_property(... VS_DPI_AWARE "PerMonitor")`. Rejected — that's a Visual-Studio-specific generator expression; the engine's CMake build needs to work on MinGW and Clang too. A manifest file is generator-agnostic.

### Decision 5: `gui_scale = 0.0f` is a sentinel for "use system DPI", AND the default

The `EngineOptions::gui_scale` default is `0.0f` (the sentinel meaning "use the system DPI"). The resolver is:

```cpp
const float systemDpi = Engine::GetSystemDPI();
float effectiveScale = opts.gui_scale;
if (effectiveScale == 0.0f) effectiveScale = systemDpi;
else if (opts.dpi_aware_override) effectiveScale *= systemDpi;
```

The resolver runs once at `Engine::Initialize` time, before `Gui::Initialize`. The result is logged (`FURYD << "system DPI: " << systemDpi;`).

The default is the sentinel so that callers who don't pass anything get the system DPI on HiDPI displays — the editor Just Works on 200% monitors without the user having to opt in. Scripts that want pixel-perfect UI at `1.0×` can pass `gui_scale = 1.0` explicitly. The previous default of `1.0f` (set by the `fix-demo-fps-profiler-retina` change) was correct for the macOS Retina double-scaling symptom but left Windows HiDPI uncovered: a caller building `EngineOptions{}` got `1.0` regardless of system DPI, so the editor's UI was half-size on a 200% display. Flipping the default to the sentinel fixes the user-facing bug while preserving the escape hatch.

**Alternative considered:** keep the `1.0f` default and require callers to opt in via `gui_scale = 0.0` explicitly. Rejected — the editor's user base is "people who launch `furye.exe`", not "people who write Lua options tables", and the opt-in requires changing `examples/Editor.lua` to match. A sentinel default is the lower-friction path and matches the user's expectation that the editor scales with the display.

**Alternative considered:** always multiply by system DPI on Windows regardless of the caller's value. Rejected — users who explicitly pass `gui_scale = 0.5` want a smaller UI (debugging, low-res video capture), and overriding that breaks their script.

### Decision 6: Editor shortcut routing guards on `IsIMEComposing()`

The `Editor::Tick` shortcut bindings (`Ctrl+S`, `Ctrl+O`, etc.) each add an early-out at the top of the action body:

```cpp
if (InputUtil::IsIMEComposing()) return;
```

The check is per-action so individual shortcuts can be exempted (none today, but the design supports it). The check happens INSIDE the action body, not around the `ImGui::Shortcut(...)` call, so the binding still registers and the shortcut text still displays.

**Alternative considered:** call `ImGui::GetIO().WantTextInput` and check the `InputText` focus state. Rejected — `WantTextInput` is true for any focused `InputText`, not specifically for IME composition, so it would suppress shortcuts in normal text-input scenarios.

**Alternative considered:** wrap the entire `Editor::Tick` in a composition guard. Rejected — the editor's non-shortcut work (drawing the dock layout, the gizmo, the viewport) should still happen during composition so the editor remains visually responsive.

### Decision 7: Migrate `io.FontGlobalScale` → `style.FontScaleMain` (ImGui 1.92)

`engine/Fury/Gui.cpp:48` writes `io.FontGlobalScale = fontScale`. The ImGui 1.92 deprecation comment at `engine/ThirdParty/ImGui/imgui.cpp:543` notes that the field was "renamed/moved" to `style.FontScaleMain`. The engine vendors ImGui 1.92.8-docking (`engine/ThirdParty/ImGui/imgui.h:32`), so the new field is available.

The migration is a one-line change:

```cpp
- io.FontGlobalScale = fontScale;
+ ImGui::GetStyle().FontScaleMain = fontScale;
```

Behavior is identical for typical font scale values; the change is an API hygiene fix to use the supported field. `style.ScaleAllSizes(scale)` (the widget / padding / rounding scale) is unchanged.

**Alternative considered:** leave the deprecated write in place. Rejected — ImGui 1.92 may remove the legacy field in a future patch release; using the supported field is forward-compatible.

## Risks / Trade-offs

- **Risk: Bounds-check on `KeyPressed`/`KeyReleased` might silently drop a legitimate future SFML key that the engine wants.** → Mitigation: the bounds check matches SFML's own `sf::Keyboard::KeyCount`; if SFML adds new keys in a future version, they will land inside `[0, KeyCount)` and the check still passes. The only dropped codes are the documented "Unknown" sentinel and any out-of-range enum.

- **Risk: Composition-state heuristic false-positives on rapid typing.** → Mitigation: the heuristic only flips `composing` to `true` when no `Backspace`/`Enter`/`Escape` is in the same frame; rapid typing of one character per frame passes through normally. False positives in the editor (suppressing `Ctrl+S` while the user types fast) are recoverable — pressing `Ctrl+S` again after the next key release works.

- **Risk: `sf::Mouse::getPosition(window)` returns coordinates in the window's client-area frame, but the engine expects screen coordinates (or vice versa).** → Mitigation: SFML's documentation says `Mouse::getPosition` returns window-relative coordinates when a window argument is passed; that matches what `OnMouseMove` already emits. Verified against the existing `MouseMoved` path (`Engine.cpp:194-199`) which uses the same coordinate system.

- **Risk: `window.hasFocus()` returns a stale value at `Engine::Initialize` time, before the OS has fully wired up the new window.** → Mitigation: SFML's `hasFocus()` queries the OS directly via `GetForegroundWindow` (Win32) / `[NSWindow isKeyWindow]` (macOS) / `XGetInputFocus` (X11) at call time; it is not a cached value. Calling it after `CreateWindowW` returns is the standard pattern for "is my window in the foreground right now". If the result is wrong, the worst case is the same as today (drag stays dead), and the existing `FocusLost`/`FocusGained` event handlers still own the rest of the focus state machine.

- **Risk: Manifest link via `LINK_FLAGS` breaks the existing `WIN32_EXECUTABLE` build setting.** → Mitigation: `LINK_FLAGS "/MANIFESTINPUT:..."` is additive; the existing `WIN32_EXECUTABLE` is a property and the link flag is a separate thing. Both can coexist. If the manifest link fails, the build still produces a binary — just without DPI awareness — so the failure is non-fatal.

- **Risk: DPI read at `Engine::Initialize` is stale if the user later changes display scale or drags the window between monitors.** → Accepted: the existing once-at-init GUI scale contract is preserved. A follow-up change can add a `WM_DPICHANGED` handler that re-applies the GUI scale if a hot-reload is desired.

- **Risk: macOS `Engine_dpi_mac.mm` shim is a new Objective-C++ source file in the engine; it must be excluded from non-Apple builds.** → Mitigation: the file is gated on `if(APPLE)` in `engine/CMakeLists.txt` so non-Apple builds never see it. Linux CMake does not need to be touched.

- **Risk: The deprecated `io.FontGlobalScale` write may already be a no-op in ImGui 1.92.** → Verified: ImGui 1.92 still honors the field for backward compatibility, but a deprecation warning may be emitted in debug builds. Migrating to `style.FontScaleMain` silences the warning and is forward-compatible with future ImGui 1.9x releases.

- **Trade-off: The IME fix is Windows-specific even though some code (the `OnIMEStateChanged` signal, the `IsIMEComposing` accessor, the `ResetTransientInputState` helper) lives in the cross-platform `InputUtil`.** → Acceptable: the cross-platform API surface is useful for Linux and macOS too (where the heuristic will likely never fire today, but the accessor exists for forward-compat). The platform-specific code is guarded by `#if PLATFORM_WINDOWS` (and the new `PLATFORM_WINDOWS` macro from `engine/Fury/Macros.h`) where needed.

## Migration Plan

In-tree change, no deploy. Order the commits to keep each independently testable:

0. **`engine/Fury/Macros.h` — add `PLATFORM_WINDOWS` / `PLATFORM_MACOS` / `PLATFORM_LINUX` / `PLATFORM_IOS` / `PLATFORM_ANDROID` / `PLATFORM_DESKTOP` / `PLATFORM_MOBILE` macros.** Single header change; no behavior change. The engine's existing code that uses `defined(_WIN32)` / `__APPLE__` is unchanged in this commit; only the new macros are added. Future commits use the new macros for the new code.
1. **`engine/Fury/Engine.{h,cpp}` — bounds-check on `KeyPressed` / `KeyReleased`, guarded with `#if PLATFORM_WINDOWS`.** Lands the IME crash fix without touching any other behavior. Rebuild and repro the Chinese-IME scenario from a clean `cmake --build build-engine --target fury -j`. (Requires the user to verify on Windows with their machine set to 200% scale and Chinese IME.)
2. **`engine/Fury/InputUtil.{h,cpp}` + `engine/Fury/Engine.cpp` — IME composition tracking (gated on `PLATFORM_WINDOWS`) + rate-limit on `TextEntered` + `ResetTransientInputState()`.** Adds the input-layer API. No behavior change for non-IME users; on non-Windows platforms the composition heuristic is a no-op (the field stays `false`).
3. **`engine/Fury/Engine.cpp` — initial focus bootstrap from `window.hasFocus()` (cross-platform).** Lands the first-launch drag fix. Build and verify drag works on first click after launching `furye.exe` from a non-foreground shell.
4. **`engine/Fury/Engine.{h,cpp}` — `EngineOptions` extension (`text_event_max_per_frame`, `dpi_aware_override`, `gui_scale = 0.0` sentinel) + `GetSystemDPI()` resolver using the new macros.** Adds the new fields with their default-preserving defaults. No behavior change for existing callers that build `EngineOptions{}` without touching `gui_scale` (the default `1.0f` still applies).
5. **`engine/Resources/fury.exe.manifest` (new) + `engine/CMakeLists.txt` — V2 manifest link gated on `if(PLATFORM_WINDOWS)`.** Lands the DPI awareness. Verify `mt.exe -inputresource:build/examples/furye.exe;#1 -validate_manifest` reports `<dpiAwareness>PerMonitorV2</dpiAwareness>` after build.
6. **`engine/Fury/Engine.cpp` (macOS path, `#if PLATFORM_MACOS`) + `engine/Fury/Engine_dpi_mac.mm` (new) — `GetSystemDPI()` for macOS.** Adds the `NSScreen` shim; no behavior change on Windows / Linux. The `.mm` file is added to `FURY_COMMON_SRC` only when `PLATFORM_MACOS` is true.
7. **`engine/Fury/Gui.cpp` — `style.FontScaleMain` migration.** Replaces the deprecated `io.FontGlobalScale` write. Cross-platform ImGui change, no `PLATFORM_*` guard needed.
8. **`engine/Fury/Editor/Editor.cpp` — shortcut guard.** Adds the IME-composing guard to the shortcut action bodies. Cross-platform; the `IsIMEComposing()` check returns `false` on non-Windows so the guard is a no-op there.
9. **`engine/Fury/LuaBindings.cpp` + `docs/LUA.md` — new Lua surface.** Documents `InputUtil.IsIMEComposing()` and the two new `Engine.run` keys.
10. **`docs/ARCHITECTURE.md` — §17.** Documents the DPI behavior and the IME composition contract.
11. Commit; do not push without confirmation.

Each commit is independently revertable. The `git revert` of any single commit leaves the engine functional (some behavior may regress; the revert is a known state).

## Open Questions

- **Should the bounds-check be a `static_assert` on `sf::Keyboard::KeyCount` at the engine's compile time?** Lean against — the check is cheap and `static_assert` would not catch a future SFML change that renumbers the enum.
- **Should `Engine::GetSystemDPI()` also be exposed to Lua (`Engine.get_system_dpi() -> number`)?** Lean toward yes, for symmetry with the other EngineOptions knobs. Defer to a follow-up if the user doesn't need it.
- **Should the editor's Viewport window also respond to `WM_DPICHANGED` (re-create the offscreen render target at the new pixel size)?** Out of scope for this change; the Viewport's render target is recreated on resize, which is a different code path. The DPI change in the middle of a drag-between-monitors is an edge case worth its own change.
- **Should `Engine::HandleEvent` reject `TextEntered` codepoints that are not valid Unicode (e.g. lone surrogates)?** SFML already splits UTF-16 surrogates into UTF-32 codepoints in its Win32 backend, so this should not occur; if it does, the existing `static_cast<size_t>` is lossless for valid UTF-32 values. Defer.
