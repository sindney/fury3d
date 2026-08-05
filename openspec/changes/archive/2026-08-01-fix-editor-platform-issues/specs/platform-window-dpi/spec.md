## ADDED Requirements

### Requirement: The Windows build SHALL declare per-monitor V2 DPI awareness

The `fury` and `furye` Windows binaries SHALL be linked with a manifest declaring per-monitor V2 DPI awareness. The manifest SHALL live at `engine/Resources/fury.exe.manifest` and SHALL be embedded by CMake at link time (via `set_target_properties(fury furye PROPERTIES LINK_FLAGS "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/Resources/fury.exe.manifest")`) gated on `PLATFORM_WINDOWS`. The manifest content SHALL include:

```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"
          xmlns:asmv3="urn:schemas-microsoft-com:asm.v3">
  <asmv3:application>
    <asmv3:windowsSettings>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
      <longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>
    </asmv3:windowsSettings>
  </asmv3:application>
</assembly>
```

The manifest SHALL be applied ONLY on `PLATFORM_WINDOWS` (CMake: `if(PLATFORM_WINDOWS)` / `if(WIN32)` around the `LINK_FLAGS` set, plus a `#if PLATFORM_WINDOWS` guard in any C++ code that needs to know it's running on Windows). macOS and Linux builds SHALL NOT link the manifest and SHALL NOT call any DPI-awareness API. The DPI-awareness behavior on macOS / Linux is unchanged: SFML's existing path applies, and `GetSystemDPI()` (see below) returns a `1.0×` value.

V2 is chosen over V1 (`true/pm` only) because V2 (a) re-issues `WM_DPICHANGED` when the user drags the window between monitors, and (b) gives the OS enough information to apply automatic non-client-area scaling (title bar, borders). SFML's Win32 backend at `engine/ThirdParty/SFML/src/SFML/Window/Win32/WindowImplWin32.cpp:70-125` only sets `ProcessPerMonitorDpiAware` (v1, via the legacy SHCore API) and explicitly does not call `SetProcessDpiAwarenessContext` / Per-Monitor V2. The manifest is the only way to opt in to V2 without patching vendored SFML.

The new manifest entry SHALL be additive — it MUST NOT remove the existing `subsystem:windows` flags that the build already sets. The current build's `<GenerateManifest>true</GenerateManifest>` (MSVC default) produces a stock manifest with no `<dpiAwareness>` element; this change replaces the stock manifest, it does not stack a second manifest.

The manifest link code lives in `engine/CMakeLists.txt` behind `if(PLATFORM_WINDOWS)` (which currently resolves to `WIN32`). The C++ side SHALL include `Fury/Macros.h` and use `#if PLATFORM_WINDOWS` guards where it needs to know the build target.

#### Scenario: PerMonitorV2 is in the embedded manifest

- **WHEN** the Windows build of `furye.exe` is produced
- **THEN** the embedded application manifest contains a `<dpiAwareness>PerMonitorV2</dpiAwareness>` element

#### Scenario: macOS / Linux are unaffected

- **WHEN** the engine is built on macOS or Linux
- **THEN** no Windows manifest is linked
- **AND** the `fury` / `furye` executables do not contain a `<dpiAwareness>` element

#### Scenario: DPI change between monitors is observable

- **WHEN** the user drags the engine window from a 100%-scaled monitor to a 200%-scaled monitor
- **THEN** Windows delivers a `WM_DPICHANGED` event to the window
- **AND** SFML updates the window's `getSize()` to reflect the new effective point size
- **NOTE**: the engine reads DPI once at `Engine::Initialize` (see `GetSystemDPI()`), so a mid-run DPI change requires a restart to re-scale the GUI. This is documented behavior; a follow-up change can add a hot-reload path.

### Requirement: The engine SHALL compute the system DPI at `Engine::Initialize` and feed it into `EngineOptions::gui_scale` as a default

The engine SHALL expose a static `float Engine::GetSystemDPI()` returning the system DPI as a multiplier (`1.0` at 96 DPI, `2.0` at 192 DPI, etc.). The implementation SHALL be:

- **Windows** (`#if PLATFORM_WINDOWS`): `GetDpiForSystem()` divided by `96.0f`. The `User32.lib` link is already present in `engine/CMakeLists.txt` via `ws2_32`; `GetDpiForSystem` lives in `User32.dll` and is loaded by default.
- **macOS** (`#if PLATFORM_MACOS`): `[NSScreen mainScreen].backingScaleFactor` (Objective-C++ via a new `engine/Fury/Engine_dpi_mac.mm` shim), defaulting to `1.0f` if the API is unavailable. NOTE: today SFML 3 macOS forces `highDpi=NO`, so this returns the screen's logical scale factor only; the value is still passed through so a future Retina-rendering change can consume it without API churn.
- **Linux** (`#if PLATFORM_LINUX`): hard-coded `1.0f` for now (no portable X11 / Wayland call without an additional dependency; revisit when the editor gains a Linux target).

`Engine::Initialize` SHALL call `GetSystemDPI()` exactly once and, when the caller did NOT pass a non-zero `gui_scale` in `EngineOptions`, SHALL set the effective `gui_scale` to the system DPI value. When the caller DID pass a non-zero `gui_scale`, the engine SHALL respect it. The `EngineOptions::gui_scale` default SHALL be `0.0f` (the sentinel) so callers that don't pass anything get the system DPI on HiDPI displays — the editor Just Works on 200% monitors without the user having to opt in. Scripts that want pixel-perfect UI at `1.0×` can pass `gui_scale = 1.0` explicitly.

A new `EngineOptions::dpi_aware_override` field (default `false`) controls what happens when the caller passed `gui_scale` AND the system DPI is non-`1.0`:

- `false` (default): the caller's `gui_scale` wins; system DPI is ignored.
- `true`: the engine multiplies the caller's `gui_scale` by the system DPI (so a user passing `gui_scale = 1.0` still gets HiDPI scaling; passing `gui_scale = 0.5` gets `0.5 × systemDpi`).

`Engine::Initialize` SHALL log a single `FURYD << "system DPI: " << dpi;` line so the resolved value is visible in `Log.txt`.

#### Scenario: Default scale tracks system DPI on Windows

- **WHEN** the engine starts on a Windows machine with system DPI `192` (200% scale)
- **AND** the caller passes `EngineOptions{ gui_scale = 0.0f }` (the "let the engine decide" sentinel)
- **THEN** the effective `gui_scale` is `2.0`
- **AND** `FURYD << "system DPI: 2"` appears in `Log.txt`

#### Scenario: Explicit gui_scale wins by default

- **WHEN** the engine starts on a Windows machine with system DPI `192`
- **AND** the caller passes `EngineOptions{ gui_scale = 1.0f, dpi_aware_override = false }`
- **THEN** the effective `gui_scale` is `1.0`
- **AND** the system DPI is read but not applied

#### Scenario: dpi_aware_override composes

- **WHEN** the engine starts on a Windows machine with system DPI `192`
- **AND** the caller passes `EngineOptions{ gui_scale = 1.0f, dpi_aware_override = true }`
- **THEN** the effective `gui_scale` is `2.0` (1.0 × 2.0)

#### Scenario: Non-Windows platforms return 1.0 by default

- **WHEN** the engine starts on macOS or Linux with no special configuration
- **THEN** `Engine::GetSystemDPI()` returns `1.0`
- **AND** the effective `gui_scale` is whatever the caller passed (or `1.0` if `0.0` was passed)

#### Scenario: gui_scale=0.0 sentinel is documented

- **WHEN** `EngineOptions::gui_scale == 0.0f` is passed
- **THEN** the engine substitutes the system DPI value (or `1.0` if the system DPI is also `0` for any reason)
- **AND** the substitution happens at `Engine::Initialize` time, before `Gui::Initialize` is called
- **AND** the `EngineOptions::gui_scale` default is `0.0f` (the sentinel) so callers that build `EngineOptions{}` without touching `gui_scale` get the system DPI on HiDPI displays by default

### Requirement: The editor SHALL document the DPI-aware GUI scale contract in `docs/ARCHITECTURE.md`

A new section in `docs/ARCHITECTURE.md` (numbered §17, immediately after the existing §16 "SFML 3 HiDPI on macOS") SHALL document:

1. The Windows manifest-declaration behavior (above).
2. The `Engine::GetSystemDPI()` API and its platform-specific implementations.
3. The `gui_scale = 0.0` sentinel meaning.
4. The `dpi_aware_override` semantics.
5. The known limitation: DPI is read once at `Engine::Initialize`, so a mid-run DPI change (dragging the window between monitors) does NOT rescale the GUI until restart. This is consistent with the existing once-at-init GUI scale contract.
6. A pointer to the SFML 3 macOS HiDPI §16 note for the future-Retina context.

The new section SHALL be added on Windows builds; on macOS / Linux the engine log line ("system DPI: 1") is sufficient and the section still exists for completeness.

#### Scenario: ARCHITECTURE.md gains the DPI section

- **WHEN** a developer reads `docs/ARCHITECTURE.md`
- **THEN** §17 "Windows DPI awareness" exists and contains the items above

#### Scenario: Log line identifies the effective DPI

- **WHEN** the engine starts on any platform
- **THEN** `Log.txt` contains a `system DPI: <value>` line
- **AND** the value is `1.0` on Linux, `1.0` on macOS by default, and the actual scaled value on Windows

### Requirement: The ImGui font scale SHALL use `style.FontScaleMain` (ImGui 1.92) instead of the deprecated `io.FontGlobalScale`, and SHALL follow `gui_scale` when the caller does not pass an explicit value

The engine's `Gui::Initialize` SHALL set `ImGui::GetStyle().FontScaleMain = fontScale` (the ImGui 1.92 replacement) INSTEAD of the legacy `ImGui::GetIO().FontGlobalScale = fontScale`. The deprecation is documented at `engine/ThirdParty/ImGui/imgui.cpp:543`: `io.FontGlobalScale` was "renamed/moved" to `style.FontScaleMain` in ImGui 1.92. The engine vendors ImGui 1.92.8-docking (`engine/ThirdParty/ImGui/imgui.h:32`), so the new field is available.

The `fontScale` argument SHALL follow the same `gui_scale = 0.0f` sentinel convention: when the caller did not pass an explicit `gui_font_scale`, the engine substitutes the resolved `gui_scale` (the effective DPI multiplier). This makes the font scale track the widget layout on HiDPI displays — without the substitution, widgets render at 2× but text is still 1× and the editor looks "small" despite the widget padding being correct. When the caller passes an explicit `gui_font_scale`, the engine uses it directly (no DPI substitution).

The behavior is identical to the old path for typical font scale values (0.5–4.0); the change is purely an API migration to the supported field. `style.ScaleAllSizes(scale)` (the widget / padding / rounding scale) is unchanged.

The engine's `Gui::Initialize` SHALL set `ImGui::GetStyle().FontScaleMain = fontScale` (the ImGui 1.92 replacement) INSTEAD of the legacy `ImGui::GetIO().FontGlobalScale = fontScale`. The deprecation is documented at `engine/ThirdParty/ImGui/imgui.cpp:543`: `io.FontGlobalScale` was "renamed/moved" to `style.FontScaleMain` in ImGui 1.92. The engine vendors ImGui 1.92.8-docking (`engine/ThirdParty/ImGui/imgui.h:32`), so the new field is available.

The behavior is identical to the old path for typical font scale values (0.5–4.0); the change is purely an API migration to the supported field. `style.ScaleAllSizes(scale)` (the widget / padding / rounding scale) is unchanged.

#### Scenario: FontScaleMain receives the value

- **WHEN** `Gui::Initialize(window, 2.0f, 2.0f)` is called
- **THEN** `ImGui::GetStyle().FontScaleMain` is `2.0f` after the call
- **AND** `io.FontGlobalScale` is left at its 1.0 default

#### Scenario: FontScaleMain follows gui_scale sentinel

- **WHEN** `EngineOptions::gui_scale == 0.0f` (sentinel) and `EngineOptions::gui_font_scale == 0.0f` (sentinel)
- **AND** the system DPI is `2.0`
- **THEN** `Gui::Initialize` is called with `(scale=2.0, fontScale=2.0)`
- **AND** both `style.ScaleAllSizes` and `style.FontScaleMain` are `2.0` after the call
- **AND** the editor's text labels and widget layout both scale to 200%

#### Scenario: Explicit gui_font_scale overrides the sentinel

- **WHEN** the caller passes `EngineOptions{ gui_scale = 1.0f, gui_font_scale = 1.5f }` on a 200% DPI system
- **THEN** `Gui::Initialize` is called with `(scale=1.0, fontScale=1.5)`
- **AND** widgets render at 1.0× but text is 50% larger than the default 13 px

#### Scenario: ImGui renders fonts at the right density

- **WHEN** the user runs the editor on a Windows machine at 200% DPI
- **AND** `EngineOptions::gui_scale == 0` (the DPI sentinel)
- **THEN** `Engine::GetSystemDPI()` returns `2.0`
- **AND** `Gui::Initialize` is called with `fontScale = 2.0` (substituted from the gui_scale sentinel)
- **AND** the editor's text labels render at double the pixel size they did before the fix
