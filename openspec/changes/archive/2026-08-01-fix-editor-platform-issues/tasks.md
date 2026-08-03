# Implementation Tasks — fix-editor-platform-issues

## 0. Add `PLATFORM_WINDOWS` / `PLATFORM_MACOS` / `PLATFORM_LINUX` / `PLATFORM_IOS` / `PLATFORM_ANDROID` / `PLATFORM_DESKTOP` / `PLATFORM_MOBILE` macros to `engine/Fury/Macros.h`

- [x] 0.1 At the top of `engine/Fury/Macros.h` (after `#include <iostream>`, before the existing `FURY_API` block), add a new section that defines the platform macros. Use `defined(_WIN32)`, `__APPLE__` with `TargetConditionals.h` for iOS, `__ANDROID__`, and `defined(__linux__)` as the source predicates; each macro resolves to `1` on its target and `0` elsewhere. Provide a `#error` for unsupported platforms.
- [x] 0.2 Below the platform macro block, add `#ifndef PLATFORM_WINDOWS #define PLATFORM_WINDOWS 0 #endif` (and similarly for the other macros) so the macros are usable as numeric preprocessor expressions on every platform.
- [x] 0.3 Add `#define PLATFORM_DESKTOP (PLATFORM_WINDOWS || PLATFORM_MACOS || PLATFORM_LINUX)` and `#define PLATFORM_MOBILE (PLATFORM_IOS || PLATFORM_ANDROID)` aggregates.
- [ ] 0.4 Build and verify the engine still compiles on every platform (Windows, macOS, Linux). The existing engine code that uses `defined(_WIN32)` / `__APPLE__` / `__linux__` is unchanged in this commit; the new macros are additive.

## 1. Bounds-check key events in `Engine::HandleEvent` (IME crash fix, `PLATFORM_WINDOWS`)

- [ ] 1.1 In `engine/Fury/Engine.cpp`, add `#include "Fury/Macros.h"` near the top (alongside the other engine includes) so the `PLATFORM_WINDOWS` guard is in scope.
- [ ] 1.2 Locate the `KeyPressed` branch (around line 157) and add a `#if PLATFORM_WINDOWS ... #endif` block at the top with two lines: `if (key->code == sf::Keyboard::Key::Unknown) return;` followed by `if (static_cast<unsigned int>(key->code) >= sf::Keyboard::KeyCount) return;`. The guard is wrapped so non-Windows builds do not pay the cost of the check (and the bug only fires on Windows).
- [ ] 1.3 Apply the same `#if PLATFORM_WINDOWS` guard to the `KeyReleased` branch (around line 163). Both branches share the same vulnerability; fix both in one commit.
- [ ] 1.4 Add a frame-local counter `int dropped_unknown_keys = 0;` at the top of the `Engine::Run` main loop and increment it from each rejection. At end-of-frame, if `dropped_unknown_keys > 0`, log a single `FURYW << "rejected " << dropped_unknown_keys << " invalid KeyPressed/KeyReleased events this frame";` and reset the counter.
- [ ] 1.5 Build (`cmake --build build-engine --target fury -j`) and verify the editor launches with no regressions for normal WASD / arrow / typing input.
- [ ] 1.6 *(Requires user verification on Windows)* Set the Windows IME to Chinese (Pinyin), focus the editor, press WASD. Confirm: no crash, no out-of-bounds write, the editor remains responsive. The `FURYW` rejection line should appear in `Log.txt` for each frame that the IME is composing.

## 2. Track IME composition state on `InputUtil`

- [ ] 2.1 In `engine/Fury/InputUtil.h`, add a private `bool m_IMEComposing = false;` field, a public `Signal<bool>::Ptr OnIMEStateChanged = Signal<bool>::Create();`, a public `bool IsIMEComposing() const { return m_IMEComposing; }`, and a public `void ResetTransientInputState();` declaration.
- [ ] 2.2 In `engine/Fury/InputUtil.cpp`, implement `ResetTransientInputState()` to zero `m_KeyDown`, `m_MouseDown`, `m_MouseWheel`, and `m_MousePosition` (matches the existing `FocusLost` body at `Engine.cpp:140-144`).
- [ ] 2.3 In `engine/Fury/Engine.cpp::HandleEvent`, at the top of the `TextEntered` branch (line 152), add the rate-limit counter: keep a frame-local `int text_events_this_frame = 0;` (declared once at the top of `Engine::Run`). For each `TextEntered`, if `text_events_this_frame >= opts.text_event_max_per_frame`, drop the event and increment a `dropped_text_events` counter; else emit `OnTextEntered` and increment `text_events_this_frame`. After exceeding the cap once, emit one synthetic `OnTextEntered(0xFFFD)` and log a single `FURYW << "TextEntered rate limit hit: " << dropped_text_events << " events dropped this frame";`. Reset both counters at end-of-frame. The rate-limit code is unguarded (cross-platform) — text-event flooding is a generic concern.
- [ ] 2.4 Add a per-frame composition heuristic, wrapped in `#if PLATFORM_WINDOWS`. Track which keys arrived in the current frame (a small `std::set<sf::Keyboard::Key>` cleared at end-of-frame). On each `TextEntered` whose codepoint is in `0x20..0x7E` and not `0x7F`: if no `KeyPressed` for `Backspace` / `Enter` / `Escape` arrived in the same frame, set `m_IMEComposing = true` (if not already) and emit `OnIMEStateChanged(true)`. On each `KeyPressed` for `Enter` / `Escape`: if `m_IMEComposing` was true, set it false and emit `OnIMEStateChanged(false)`. The `m_IMEComposing` field is the only state; the `OnIMEStateChanged` signal is unguarded (the field stays `false` on non-Windows, so the signal never fires on macOS / Linux today).
- [ ] 2.5 Build and verify the existing editor behavior is unchanged when no IME is active. On non-Windows builds, verify that `IsIMEComposing()` always returns `false` (the Windows-only heuristic is the only writer of the field).

## 3. Initial focus bootstrap in `Engine::Initialize`

- [ ] 3.1 In `engine/Fury/Engine.cpp::Initialize`, after `InputUtil::Initialize(...)` (line 91), add the focus bootstrap. Take the `sf::Window &window` parameter (already present at line 73) and call `if (window.hasFocus()) { InputUtil::Instance()->m_WindowFocused = true; InputUtil::Instance()->OnWindowFocus->Emit(true); }`.
- [ ] 3.2 *(Requires user verification on Windows)* Build, launch `furye.exe` from a non-foreground shell (e.g., cmd, double-click). Confirm: `Log.txt` shows no `FocusGained` event, but `m_WindowFocused` is true (read via `InputUtil.Instance():GetWindowFocused()` from the Lua console). The first user mouse-drag in the viewport works.
- [ ] 3.3 *(Requires user verification on macOS)* Build, launch `furye` from the terminal. Confirm: drag works on first click. (macOS already delivers `NSWindowDidBecomeKeyNotification` at creation, so this is mostly a regression check.)

## 4. Focus-driven mouse seed in `Engine::HandleEvent`

- [ ] 4.1 In `engine/Fury/Engine.cpp::HandleEvent`, at the top of the `FocusGained` branch (line 147), add a one-shot seed block: read `sf::Mouse::getPosition(window)` (need to thread the `sf::Window &` reference into `HandleEvent` — change the signature to `static void HandleEvent(sf::Event &event, sf::Window &window)` and update the call site at `Engine.cpp:302`), write the result to `inputMgr->m_MousePosition`, call `inputMgr->ResetTransientInputState()` (which re-zeros the just-seeded mouse position — re-write it after the reset), and emit a synthetic `inputMgr->OnMouseMove->Emit(mx, my)` BEFORE the existing `OnWindowFocus->Emit(true)`.
- [ ] 4.2 Guard the seed with a per-focus-gain one-shot: track a `bool needs_focus_seed = true;` in `Engine::Run`, set to `true` on each `FocusLost`, and only run the seed block when the flag is true. Set the flag to `false` after the seed.
- [ ] 4.3 In the `FocusLost` branch (line 130), also call `inputMgr->ResetTransientInputState()` (it currently inlines the key / button zeroing; the new helper makes the symmetry with the new `FocusGained` seed explicit).
- [ ] 4.4 *(Requires user verification on macOS)* Open the native open dialog (via `File → Open...`), pick a file or cancel, confirm the editor's drag still works immediately after the dialog returns (the macOS `MouseEntered` path is not implemented in this change, but the new `FocusGained` seed covers the case where Cocoa re-fires the notification).

## 5. Extend `EngineOptions` with `text_event_max_per_frame` and `dpi_aware_override`

- [x] 5.1 In `engine/Fury/Engine.h`, add `int text_event_max_per_frame = 64;` and `bool dpi_aware_override = false;` to the `EngineOptions` struct (next to `gui_scale` at line 31). Defaults preserve existing behavior. **Note (added during verification):** the `gui_scale` default was also flipped from `1.0f` to `0.0f` (the sentinel) so callers that don't pass anything get the system DPI on HiDPI displays by default. This is a small behavior change from the original `fix-demo-fps-profiler-retina` default but matches the user-facing expectation that the editor scales with the display.
- [ ] 5.2 In `engine/Fury/Engine.cpp::Run` (the three-arg form, line 268), log `FURYD << "text_event_max_per_frame: " << opts.text_event_max_per_frame;` and `FURYD << "dpi_aware_override: " << opts.dpi_aware_override;` next to the existing `gui_scale` / `gui_font_scale` logs (lines 278-280).
- [ ] 5.3 *(Optional)* Add a `0.0f` sentinel for `gui_scale` in `EngineOptions` (default stays `1.0f`; pass `0.0f` explicitly to mean "use system DPI"). Add a `FURYD << "gui_scale source: " << (opts.gui_scale == 0.0f ? "system DPI" : "explicit");` log line.

## 6. Implement `Engine::GetSystemDPI()` and the DPI resolver (uses `PLATFORM_*` macros)

- [ ] 6.1 In `engine/Fury/Engine.h`, declare `static float GetSystemDPI();`.
- [ ] 6.2 In `engine/Fury/Engine.cpp`, implement `GetSystemDPI()` using the new platform macros. The Windows path uses `#if PLATFORM_WINDOWS`, the macOS path uses `#if PLATFORM_MACOS` (forwarded to the `.mm` shim in task 7), the Linux path uses `#if PLATFORM_LINUX`:

```cpp
float Engine::GetSystemDPI() {
#if PLATFORM_WINDOWS
    return static_cast<float>(GetDpiForSystem()) / 96.0f;
#elif PLATFORM_MACOS
    return furyGetMacOSBackingScale();
#else
    return 1.0f;
#endif
}
```

- [ ] 6.3 In `Engine::Initialize`, after `GetSystemDPI()` is implemented, compute the effective `gui_scale` for `Gui::Initialize`:
  - If `opts.gui_scale == 0.0f`: effective = systemDpi.
  - Else if `opts.dpi_aware_override`: effective = `opts.gui_scale * systemDpi`.
  - Else: effective = `opts.gui_scale`.
- [ ] 6.4 Log `FURYD << "system DPI: " << systemDpi;` and `FURYD << "effective gui_scale: " << effective;` once at init.
- [ ] 6.5 Pass `effective` (not the raw `opts.gui_scale`) to `Gui::Initialize(window, effective, opts.gui_font_scale)` at `Engine.cpp:273`.

## 7. macOS DPI shim (`PLATFORM_MACOS` only)

- [ ] 7.1 Create `engine/Fury/Engine_dpi_mac.mm` (Objective-C++) with a single function:

```cpp
#import <AppKit/AppKit.h>
float furyGetMacOSBackingScale() {
    @autoreleasepool {
        NSScreen* screen = [NSScreen mainScreen];
        if (screen == nil) return 1.0f;
        return (float)[screen backingScaleFactor];
    }
}
```

- [ ] 7.2 In `engine/CMakeLists.txt`, add the shim to the engine source list, gated on `if(PLATFORM_MACOS)` (or the equivalent `if(APPLE)`):

```cmake
if(PLATFORM_MACOS)
    list(APPEND FURY_COMMON_SRC "${PROJECT_SOURCE_DIR}/Fury/Engine_dpi_mac.mm")
endif()
```

- [ ] 7.3 Build on macOS and confirm `Log.txt` shows `system DPI: 1` (SFML 3 forces `highDpi=NO`, so the screen's backing scale is not actually used yet — this is a forward-compat hook).
- [ ] 7.4 On Windows / Linux builds, confirm the `.mm` file is never compiled (it is not in the source list).

## 8. Ship the V2 DPI awareness manifest (`PLATFORM_WINDOWS` only)

- [ ] 8.1 Create `engine/Resources/fury.exe.manifest` with the content from the `platform-window-dpi` spec's first requirement (the XML with `<dpiAwareness>PerMonitorV2</dpiAwareness>` and `<dpiAware>true/pm</dpiAware>`).
- [ ] 8.2 In `engine/CMakeLists.txt`, add a Windows-gated manifest link to both targets. The gate uses `if(PLATFORM_WINDOWS)` (which currently resolves to `if(WIN32)`); on macOS / Linux the manifest link is skipped:

```cmake
if(PLATFORM_WINDOWS)
    set(_FURY_MANIFEST "${PROJECT_SOURCE_DIR}/Resources/fury.exe.manifest")
    set_target_properties(fury PROPERTIES LINK_FLAGS "/MANIFESTINPUT:${_FURY_MANIFEST}")
    set_target_properties(furye PROPERTIES LINK_FLAGS "/MANIFESTINPUT:${_FURY_MANIFEST}")
endif()
```

(Add this after the `fury_add_target` calls so the targets exist when the properties are set.)
- [ ] 8.3 Build `furye.exe` and verify the manifest is embedded:

```bash
mt.exe -inputresource:build-engine/Examples/Release/furye.exe;#1 -validate_manifest
```

Confirm the output includes `<dpiAwareness>PerMonitorV2</dpiAwareness>`.
- [ ] 8.4 *(Requires user verification on Windows)* Set Windows display scale to 200%, launch the editor, confirm the window is now rendered at the physical-pixel size (no OS bitmap stretching). On a 4K 27" monitor the editor should fill the same physical area as before but at 2× the font / widget size.
- [ ] 8.5 *(Requires user verification on Windows)* Drag the editor window from a 100% monitor to a 200% monitor. Confirm Windows issues `WM_DPICHANGED` and the title bar resizes correctly (V2-specific behavior). Note: the engine does not re-scale the GUI on this event; that is a follow-up.

## 9. Migrate `io.FontGlobalScale` → `style.FontScaleMain` (ImGui 1.92)

- [ ] 9.1 In `engine/Fury/Gui.cpp` (line 48), replace `io.FontGlobalScale = fontScale;` with `ImGui::GetStyle().FontScaleMain = fontScale;`. Keep `style.ScaleAllSizes(scale);` (line 52) unchanged.
- [ ] 9.2 Build and verify fonts render at the expected sizes for the existing test cases (the `examples/Demo.lua` default is `fontScale = 1.0`).
- [ ] 9.3 *(Optional, recommended)* Run the engine with `Engine.run(callbacks, { gui_scale = 2.0 })` on a non-HiDPI machine to verify the manual 2× path still works (this is the path the user is using in screenshots if they ever applied it).

## 10. Editor shortcut guard for IME composition

- [ ] 10.1 In `engine/Fury/Editor/Editor.cpp` (and `EditorWindows.cpp` if it owns shortcut action bodies), locate every `ImGui::Shortcut(... ImGuiInputFlags_RouteGlobal)` action and add `if (fury::InputUtil::Instance()->IsIMEComposing()) return;` at the top of the action body. The full set of shortcuts to guard: `Ctrl+N`, `Ctrl+O`, `Ctrl+Shift+I`, `Ctrl+S`, `Ctrl+Shift+S`, `Ctrl+Q`.
- [ ] 10.2 Build and verify `Ctrl+S` typed in a text input field no longer fires `on_save` when an IME is composing.
- [ ] 10.3 *(Requires user verification on Windows)* With Chinese IME active, type a composition, press `Ctrl+S` to commit. Confirm: the composition commits AND no save dialog appears. Press `Ctrl+S` again (no composition) and confirm the save dialog appears.

## 11. Expose `InputUtil.IsIMEComposing()` to Lua

- [ ] 11.1 In `engine/Fury/LuaBindings.cpp`, locate the `InputUtil` usertype block (around line 1543) and add `"IsIMEComposing", &InputUtil::IsIMEComposing` to the binding list.
- [ ] 11.2 Verify the binding is available on both `WITH_EDITOR=ON` and `WITH_EDITOR=OFF` builds (the `IsIMEComposing` method is on `InputUtil` which exists in both). For `WITH_EDITOR=OFF`, ensure the return value is `false` (the cross-platform accessor returns the `m_IMEComposing` field, which defaults to `false` and never gets set without `WITH_EDITOR`).
- [ ] 11.3 In `LuaBindings.cpp::engine_tbl["run"]` (around line 1632), read the new `text_event_max_per_frame` and `dpi_aware_override` keys from the options table via `opts_table.get_or<T>(key, default)`. Pass them through to the new `EngineOptions` fields.
- [ ] 11.4 In `docs/LUA.md`, document the new `InputUtil.Instance():IsIMEComposing()` method and the two new `Engine.run` options (`text_event_max_per_frame`, `dpi_aware_override`) with their defaults and the `gui_scale = 0` sentinel meaning.

## 12. Documentation

- [ ] 12.1 In `docs/ARCHITECTURE.md`, add §17 "Windows DPI awareness" with the items from the `platform-window-dpi` spec's last requirement (manifest, `GetSystemDPI`, the `gui_scale = 0` sentinel, `dpi_aware_override`, the once-at-init limitation, a pointer to §16 for the SFML 3 macOS HiDPI context).
- [ ] 12.2 In `docs/ARCHITECTURE.md`, add a short §17.1 "IME composition contract" describing the engine's IME-state API and the editor's shortcut-guard behavior.

## 13. End-to-end verification

- [ ] 13.1 Clean rebuild: `cmake --build build-engine --target fury furye -j`. Confirm the build succeeds.
- [ ] 13.2 Run `./furye` from `examples/bin/` on Windows. `Log.txt` should show:
  - `system DPI: 2` (on 200% display)
  - `effective gui_scale: 2`
  - `framerate cap: 144`
  - `gui_scale: 2`
  - `gui_font_scale: 1` (unchanged)
  - `text_event_max_per_frame: 64`
  - `dpi_aware_override: false`
- [ ] 13.3 Confirm the editor window opens at the expected size with the UI proportional to the system DPI.
- [ ] 13.4 Confirm a left-mouse drag in the viewport works on the first click (no need to switch the window to background first).
- [ ] 13.5 *(Requires user verification)* With Chinese IME active, press WASD. Confirm no crash. The camera should move normally (the keys are emitted from `OnKeyDown` for `Key::W`/etc., which still works because those have valid `code` values).
- [ ] 13.6 Run `openspec validate fix-editor-platform-issues --strict` and `openspec status --change fix-editor-platform-issues` — confirm `isComplete: true` once tasks are checked.

## 14. Commit (do NOT push)

- [ ] 14.1 `git status` — verify the modified set matches the file inventory in the proposal's Impact section.
- [ ] 14.2 Stage and commit with a single message of the form: `fix(editor): IME bounds-check, focus bootstrap, Windows HiDPI awareness`.
- [ ] 14.3 Do not push.

## 15. Out-of-scope notes for archive

- [ ] 15.1 Capture in the archive note: true Retina rendering on macOS still requires patching SFML's `WindowImplCocoa` / `SFOpenGLView` (e.g. `[oglView setWantsBestResolutionOpenGLSurface:YES]`) and the GBuffer texture-allocation path that assumes pixel == point. Deferred. (Already documented in proposal §Impact and design §Decisions 4 / 6.)
- [ ] 15.2 Capture: any future `WM_DPICHANGED` handler that re-applies the GUI scale mid-run goes in `Engine::Run` near the event-pump. The infrastructure (the `GetSystemDPI()` call, the `effective` resolver, the V2 manifest) is already in place; the handler is the only missing piece. *(Documented in design §Open Questions.)*
- [ ] 15.3 Capture: the `m_GlobalScale` field in `engine/Fury/Gui.cpp:22` is dead state (set at line 31, never read). Future cleanup; not in scope for this change. *(Documented in design §Decisions 7 and the platform-window-dpi spec.)*
