## Why

The vendored ImGui (v1.52, 2017-era) is many years out of date and lacks
docking / multi-viewport support, which makes a real editor layout
impossible. At the same time, `examples/Demo.lua` has grown into a
de-facto editor — File menu, camera panel, scene-tree work — but every
new panel adds Lua boilerplate and the hard-coded floating windows can't
be arranged into an editor-style workspace. We want to (1) upgrade
ImGui to the current docking branch (v1.92.8-docking), (2) move the
editor shell into C++ behind a `WITH_EDITOR` build flag (still
extensible from Lua), and (3) restructure the demo as `Editor.lua` with
the canonical four-panel layout (Scene Inspector / Viewport / Console +
Content Browser / Settings + Profiler).

## What Changes

- **BREAKING**: Replace `engine/ThirdParty/ImGui/` (v1.52) with
  v1.92.8-docking. Reorganized: `imgui/`, `imgui/backends/`,
  `imgui/misc/freetype/` (optional). The current `imgui_fury.cpp`
  PlotVar helper moves to `engine/Fury/EditorPlotVar.cpp`.
- Update `engine/Fury/Gui.cpp` to drive the new ImGui frame lifecycle
  (`ImGui::CreateContext`, `ImGuiBackendFlags_*`, custom SFML/OpenGL
  backend updated for the new API), enable `ImGuiConfigFlags_DockingEnable`,
  and replace the deprecated `RenderDrawListsFn` callback with the
  modern explicit `ImGui::Render()` + draw-data path.
- Add a new `WITH_EDITOR` CMake option (default ON) that gates the
  editor subsystem. When OFF, the engine builds without any editor code
  but `_FURY_GUI_IMP_` (basic ImGui overlay) still works.
- Introduce a C++ Editor subsystem in `engine/Fury/Editor/` — owns the
  dockspace, the menu bar (File / Window / Settings), the four built-in
  windows (Scene Inspector, Console, Content Browser, Profiler+Buffers),
  the theme registry (12 themes from
  `imgui_styles/themes-by-TheAncientOwl.md`), and a Lua extension hook
  so scripts can register additional menu items / windows.
- Rename `examples/Demo.lua` → `examples/Editor.lua`. The new file
  drives the editor: camera flythrough stays in Lua; menu structure
  (File / Window / Settings) is owned by C++ and Lua only sets
  per-section state (active theme, import options, current scene path,
  etc.) via the new `Editor.*` Lua bindings.
- Update `examples/main.cpp` argv resolution and any documentation /
  CLI text that still says `Demo.lua` to default to `Editor.lua`.
- Combine the existing Profiler / GBuffer / Shadow Buffer windows
  (`engine/Fury/Gui.cpp:489`-onward) into one tabbed Profiler window
  owned by the editor.
- Add a Console window: single-line `InputText` to send commands to a
  registered Lua handler, multi-line scrolling text view that mirrors
  the engine's `Log` output (subscribe to `Log` via a new ring-buffer
  sink).
- Add a Content Browser that lists files in the directory of the
  currently-open scene (as opened by File → Open or set by File → New).
- Add a Settings window with three categories: **Camera**, **Import**,
  **Themes** (theme selector wired to the 12-entry theme registry).
- Default dock layout (applied on first run / when reset): Scene
  Inspector docked left, Console + Content Browser tabbed at bottom,
  Profiler and Settings floating.

## Capabilities

### New Capabilities

- `editor-shell`: The C++ editor subsystem behind `WITH_EDITOR` —
  dockspace, menu bar, four built-in windows (Scene Inspector / Console
  / Content Browser / Profiler), default layout, theme registry, and the
  Lua extension surface.

### Modified Capabilities

- `scene-editor`: The Lua bindings around the demo / now editor change
  shape — the `File` menu moves from Lua-emitted (`Gui.SetMenuBarCallback`)
  to C++-owned with Lua hooks; `Demo.lua` is renamed `Editor.lua`; the
  argv-startup / Quit / camera-tuning behaviors carry over but are
  re-expressed against the new `Editor.*` binding surface.

## Impact

- **Engine code**: New `engine/Fury/Editor/` directory (Editor.cpp,
  EditorWindows.cpp, EditorThemes.cpp, EditorLog.cpp); `Gui.cpp` /
  `Gui.h` updated for the new ImGui API; `LuaBindings.cpp` gains an
  `Editor` table; `Engine.cpp` gains an `Editor::Tick` call inside the
  main loop.
- **Third-party**: `engine/ThirdParty/ImGui/` replaced with v1.92.8-docking
  (sources + backends/imgui_impl_opengl3.{cpp,h} +
  backends/imgui_impl_sfml3 — written fresh since the upstream backend
  targets SFML 2; we already vendor SFML 3.1).
- **Build**: `engine/CMakeLists.txt` glob for ImGui sources changes;
  new `WITH_EDITOR` option toggles `-DWITH_EDITOR` and adds
  `engine/Fury/Editor/*.cpp` / new editor headers to the source list.
- **Examples**: `examples/Demo.lua` → `examples/Editor.lua`. CLI / docs
  references updated (`docs/CLI.md`, `docs/LUA.md`,
  `examples/main.cpp:argv[1]` default).
- **Specs**: `scene-editor` updated for the rename + menu-ownership
  shift; new `editor-shell` capability added.
- **Saved scenes / file format**: Unchanged.
