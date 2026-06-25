## Context

Fury3d ships an embedded ImGui v1.52 (2017) under
`engine/ThirdParty/ImGui/` with a hand-written SFML/OpenGL2-style
backend in `engine/Fury/Gui.cpp`. The vendored copy predates docking
and multi-viewport — modern editor layouts (resizable panels, tabbed
docks, drag-out windows) aren't expressible. The renderer initialization
also still uses the deprecated `RenderDrawListsFn` callback path
(`Gui.cpp:94`) removed in newer ImGui versions.

`examples/Demo.lua` has accumulated editor-shaped concerns: File →
New / Open / Import / Save As / Quit, a camera-tuning panel, a save
modal, and a status line. The engine itself owns Profiler / GBuffer /
Shadow Buffer windows behind a `View` menu (`Gui.cpp:489`+). All of
this is legacy compatible with the v1.52 API and reachable by Lua only
through small per-widget forwarders (`Gui.SliderFloat`, `Gui.Begin`,
`Gui.MenuItem`, `Gui.SetMenuBarCallback` …).

Repo state:
- Engine source: `engine/Fury/`
- Vendored ImGui: `engine/ThirdParty/ImGui/` (+ `engine/ThirdParty/imgui/` lowercase duplicate that should be removed)
- Demo entry point: `examples/Demo.lua` (referenced by `examples/main.cpp` argv[1] default and by `docs/CLI.md`, `docs/LUA.md`)
- Build: top-level `CMakeLists.txt` does not exist; engine builds via `engine/CMakeLists.txt`.
- Lua bindings: `engine/Fury/LuaBindings.cpp` registers `Gui`, `Importer`, `Window`, `Camera`, `Pipeline`, `Scene`, `FileUtil`, `InputUtil`, etc.

Stakeholders: every Lua sample / future game project depends on the
ImGui surface area; the upgrade is breaking but contained — only the
engine and the bundled examples touch ImGui directly.

## Goals / Non-Goals

**Goals:**
- Replace ImGui v1.52 with v1.92.8-docking (sources + opengl3/sfml3
  backend), preserve every existing Lua-visible widget call, and gain
  `ImGuiConfigFlags_DockingEnable`.
- Move the editor shell (menu bar, dockspace, four built-in windows,
  theme registry) into C++ behind `WITH_EDITOR` (default ON), with a
  small Lua extension hook for project-specific menu items / panels.
- Rename `examples/Demo.lua` → `examples/Editor.lua` and reduce it to
  the per-project bits (camera flythrough, importer/sun policy,
  command handler, theme/import/camera state writes).
- Deliver a default dock layout the user can recover from `Window →
  Reset Layout`: Scene Inspector left, Console + Content Browser
  bottom-tabbed, Profiler + Settings floating.

**Non-Goals:**
- Multi-viewport (`ImGuiConfigFlags_ViewportsEnable`) — defer; needs
  multi-window OpenGL contexts and SFML 3 viewport support that we
  haven't validated.
- DPI-aware font rebaking on monitor switch — keep the existing
  `fontScale` knob, no per-monitor adapt.
- Scene-Inspector node-property editing (transform, material, etc.) —
  the spec calls for tree-view only; property editing is a follow-up.
- Asset preview thumbnails / detailed view in Content Browser — this
  change ships file-list-only.
- Console scripting language extensions (the input field's only job is
  to call a registered Lua command handler with the typed string).

## Decisions

### 1. Vendor v1.92.8-docking sources directly, not as a submodule

We already vendor SFML, lua, sol2, rapidjson, tinygltf, LZ4, STB
in-tree. Adding a submodule for ImGui breaks the existing pattern and
forces every clone to `git submodule update`. We'll drop the
`v1.92.8-docking.zip` contents into `engine/ThirdParty/ImGui/` and
delete the lowercase `engine/ThirdParty/imgui/` directory.

Layout:
```
engine/ThirdParty/ImGui/
  imconfig.h
  imgui.cpp / imgui.h
  imgui_demo.cpp
  imgui_draw.cpp
  imgui_internal.h
  imgui_tables.cpp        # new in 1.80+
  imgui_widgets.cpp       # split out from imgui.cpp
  imstb_*.h               # new
  backends/
    imgui_impl_opengl3.cpp / .h
    imgui_impl_sfml3.cpp / .h   # OUR FILE — written fresh, see below
```

The CMake `file(GLOB IMGUI_SRC ...)` already in
`engine/CMakeLists.txt:87` keeps working with the new file list.

**Alternative considered**: keep the modified-1.52 source forever.
Rejected — every quality-of-life ImGui feature (DockSpace, BeginTable,
TreeNodeEx flags, ImGui::TextLink…) needs the upgrade.

### 2. Backend: SFML 3 + OpenGL3, written from scratch

Upstream ImGui ships `imgui_impl_sdl*`, `imgui_impl_glfw`, and the
community has SFML 2 backends — none target SFML 3. We'll write
`backends/imgui_impl_sfml3.cpp` (event handler + new-frame +
key/mouse/wheel/clipboard wiring) and reuse the upstream
`imgui_impl_opengl3.cpp` verbatim for the renderer (compatible with
GL 3.3 core which the engine already uses).

Side-effect: the existing custom renderer in `Gui.cpp` (the bespoke
shader + VBO/VAO + RenderDrawLists implementation) is replaced
wholesale. The current `Gui::Initialize` / `Gui::Shutdown` /
`Gui::HandleEvent` / `Gui::NewFrame` / `Gui::Render` API stays — only
the bodies change. Lua-facing forwarders are preserved.

**Alternative considered**: keep the custom backend, port it to the
new ImGui draw-vertex format. Rejected — minimal value, fragile, and
upstream's OpenGL3 backend has docking / multi-viewport / sRGB
correctness baked in.

### 3. `WITH_EDITOR` is a separate gate from `_FURY_GUI_IMP_`

Two CMake flags:
- `GUI_IMP` (existing, default ON) — defines `_FURY_GUI_IMP_`,
  compiles ImGui sources + the basic `Gui::*` overlay surface. Required
  for the editor.
- `WITH_EDITOR` (new, default ON) — defines `WITH_EDITOR`, compiles
  `engine/Fury/Editor/*.cpp` and registers the `Editor` Lua table.
  Requires `GUI_IMP=ON`; CMake errors otherwise.

Rationale: a future headless / embedded build can disable both; a
runtime-only game can keep ImGui (HUD, debug overlay) but skip the
editor mass. The two flags compose orthogonally without ifdef
explosion in `Gui.cpp` itself — editor code lives in its own files
guarded by a single `#ifdef WITH_EDITOR` at the top.

### 4. Menu ownership: C++ owns shell, Lua owns content

The C++ editor owns the menu bar entirely:

```
File:    New | Open ▸ | Import ▸ | Save As… | --- | Settings
Window:  • Profiler | • Scene Inspector | • Console | • Content Browser | --- | Reset Layout
[script-emitted top-level menus, e.g. Camera, render here]
View:    [engine debug overlays — kept]
```

`Gui::SetMenuBarCallback` (the existing Lua hook) is preserved but its
position moves to AFTER the editor's File/Window menus and BEFORE the
View menu. Editor.lua uses it to register the `Camera` menu just as
Demo.lua does today.

For File menu items that need project-supplied behavior (the file
lists under Open/Import, the path Save As writes to, what happens on
New), the editor exposes Lua callbacks via the new `Editor.*` table:

```lua
Editor.SetSceneIO({
  list_files     = function() return {...} end,   -- powers Open/Import
  on_new         = function() ... end,
  on_open        = function(path) ... end,        -- relative path
  on_import      = function(path) ... end,
  on_save_as     = function(path) ... end,        -- user-typed name
  scene_dir      = function() return "..." end,   -- powers Content Browser
})

Editor.SetCommandHandler(function(line) ... end)  -- Console input

Editor.SetSceneTreeProvider(function()
  return { name = "root", children = { ... } }    -- powers Scene Inspector
end)
```

This keeps Editor.lua about as long as today's Demo.lua but contains
only project-specific glue.

**Alternative considered**: emit the entire menu from Lua (today's
shape) and add docking via Lua bindings. Rejected — DockSpace setup,
default-layout reset, and ImGui ID stability for window persistence
are easier to keep in C++ than to expose as Lua surface.

### 5. Theme registry as a C++ table; selection persisted via ImGui INI

The 12 themes from `themes-by-TheAncientOwl.md` go into
`engine/Fury/Editor/EditorThemes.cpp` as plain `void Setup<Name>Style()`
functions plus a `kThemes[]` table:

```cpp
struct ThemeEntry { const char* name; void (*apply)(); };
static const ThemeEntry kThemes[] = {
  {"Dark",            &SetupDarkStyle},
  {"Forest Green",    &SetupForestGreenStyle},
  {"Amethyst",        &SetupAmethystStyle},
  // ... 9 more
};
```

The Settings → Themes panel renders a Combo from `kThemes`. The
selection is persisted by writing a single line into ImGui's
`UserType_Theme` ini-handler (registered via
`ImGui::AddSettingsHandler`), so the user's choice survives restarts
without us inventing a config-file format.

**Alternative considered**: persist to a `.json` file next to
`Editor.lua`. Rejected — ImGui already handles dock layout / window
positions / opens via `imgui.ini`; piggy-backing on the same file keeps
state in one place.

### 6. Default dock layout: build once via `ImGui::DockBuilder*` API

On first run (no `imgui.ini`) or `Window → Reset Layout`, the editor
runs:

```cpp
ImGuiID root = ImGui::DockBuilderAddNode(dockspace_id);
ImGuiID left, center, bottom;
ImGui::DockBuilderSplitNode(root, ImGuiDir_Left,  0.20f, &left,   &center);
ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, &bottom, &center);
ImGui::DockBuilderDockWindow("Scene Inspector", left);
ImGui::DockBuilderDockWindow("Console",          bottom);
ImGui::DockBuilderDockWindow("Content Browser",  bottom);   // tab in same node
// Settings, Profiler: not docked → float.
ImGui::DockBuilderFinish(dockspace_id);
```

Settings and Profiler windows are NOT pre-docked — they appear floating
when toggled on, the user can drag them anywhere.

### 7. Console + Log: ring-buffer sink subscribed to FURYI/W/E

Add a `Log` sink interface (lightweight — `Log.h` is currently a header-
only macro shim). The Console window reads from a fixed-size circular
buffer (`std::deque<std::string>` capped at 4096 entries) the editor
fills in `Log` macros. Lua bindings get `Editor.Log(level, text)` so
scripts can push to the same view.

The input field is a single-line `ImGui::InputText` with
`ImGuiInputTextFlags_EnterReturnsTrue`. On enter, the text is passed to
the registered command handler (Lua) and echoed into the log with a
`> ` prefix.

### 8. Content Browser: directory-mirror, no caching

`Editor.scene_dir()` returns the directory of the most recently
opened/created scene (defaults to `Resource/Scene/`). The Content
Browser calls `FileUtil.ListDirectory` (already in the Lua surface;
also available in C++) every frame the window is visible. Frame cost
is dominated by the OS readdir; acceptable for tens-to-hundreds of
files. The window shows file names with a small icon-by-extension
prefix (no thumbnails — deferred).

### 9. Editor.lua skeleton

The new `examples/Editor.lua` is materially shorter than today's
`Demo.lua` because the editor owns the menus, save modal, and the
profiler/buffer toggles:

```lua
-- camera state, on_init, on_update mostly carry over
-- File menu logic moves into Editor.SetSceneIO callbacks
-- show_camera_window / show_save_modal disappear (Settings + File→Save As)
-- the script-emitted Camera menu stays via Gui.SetMenuBarCallback
```

Roughly 200 lines vs the current 437.

## Risks / Trade-offs

- **[Risk] ImGui API churn — every existing `Gui::*` forwarder may need a tweak.** → Mitigation: enumerate them in `Gui.h` (≈12 functions), exercise each from Editor.lua during smoke testing, and keep behavior compatible (return value shapes don't change). The biggest moving target is `RenderDrawListsFn` (gone); we replace it with explicit `ImGui::Render(); imgui_impl_opengl3_RenderDrawData(ImGui::GetDrawData())`.
- **[Risk] SFML3 backend regressions — keystroke / mouse-wheel scaling / focus differ from today's hand-written backend.** → Mitigation: write the SFML3 backend by reading the upstream SDL/GLFW backends side-by-side and porting events 1:1, test focus + drag + wheel against the Camera-flythrough binding manually.
- **[Risk] Dock IDs vs window-title rename break user `imgui.ini` files.** → Mitigation: ship a fresh imgui.ini in `examples/` once; document in CHANGELOG that users with a hand-edited layout should `Window → Reset Layout` once.
- **[Risk] `WITH_EDITOR=OFF` accidentally regresses — engine still references `Editor::Tick` from `Engine.cpp`.** → Mitigation: declare a no-op inline `Editor::Tick()` in the editor header guarded by `#ifndef WITH_EDITOR`. Compiles, links, costs nothing.
- **[Risk] The 12-theme registry inflates binary size with redundant color tables.** → Mitigation: each theme is ~80 ImVec4 inits = ~5 KB; total ~60 KB across all themes. Acceptable.
- **[Trade-off] Console = engine-level Log mirror, not a Lua REPL.** Easier to ship; users wanting REPL semantics can wire `loadstring(line)()` themselves in their command handler.
- **[Trade-off] Content Browser hits the disk every frame the window is visible.** Cheap on macOS APFS / Windows NTFS for small dirs; if it becomes a problem we can cache and invalidate on a 1Hz timer.

## Migration Plan

1. **Snapshot** the current `engine/ThirdParty/ImGui/` contents in case
   we need to roll back.
2. Replace ImGui sources, write SFML3 backend, port `Gui.cpp` to the
   new frame lifecycle. Verify the existing engine test path
   (`Demo.lua`) still renders before adding any editor code.
3. Add `WITH_EDITOR` flag, scaffold `engine/Fury/Editor/Editor.{h,cpp}`
   with empty windows, plumb `Editor::Tick` from `Engine.cpp`.
4. Implement themes + Settings window (smallest piece; isolated).
5. Implement dockspace + default layout + Window menu toggles.
6. Implement Profiler/Buffers consolidation, Console, Scene Inspector,
   Content Browser. Each can land independently.
7. Rename `Demo.lua` → `Editor.lua`, refactor onto the new
   `Editor.SetSceneIO` / `Editor.SetSceneTreeProvider` /
   `Editor.SetCommandHandler` surface. Update `examples/main.cpp`,
   `docs/CLI.md`, `docs/LUA.md`.
8. Manual smoke: launch with no args (loads default scene), File →
   Open / Import / New / Save As, Window → toggle each, Settings →
   each theme, Console → run a Lua line, Content Browser → see
   Resource/Scene contents update after Save As.

Rollback: revert the merge commit. The change is one git
checkpoint; no migration of saved state is required (saved scenes use
the existing `.json` / `.bin` format, unchanged).

## Open Questions

- Do we want font icons (FontAwesome) for the Content Browser file
  list? The `themes-by-TheAncientOwl.md` snippet references
  `ICON_CI_*` glyphs; bundling a font enlarges the asset directory by
  ~150 KB. Default: no, plain text labels — revisit when we add the
  detailed view.
- Should `Editor.SetSceneTreeProvider` poll every frame or be event-
  driven? Polling is simpler and the tree fits in cache. Default:
  poll; revisit if profiling shows it.
- Console scrollback policy — fixed 4096 entries, or unbounded with a
  user-clearable buffer? Default: 4096-entry ring; user-clearable via
  a `Clear` button in the window header.
