# Ocean island demo

First-person island game shell built on the RmlUi game UI
(docs/GAME_UI.md). Main menu over a drifting island camera, FPS player
on the beach, shared options panel, HUD. Scene: ocean_island.bin;
game script: examples/IslandGame.lua.

## Run

```
cd examples/
./fury IslandGame.lua
```

The scene working dir is this folder, so UI documents load from
`ui/` and settings persist here.

## Controls

- WASD / arrow keys - move, Space - jump, mouse - look
- ESC - pause menu (Continue / Options / Exit to main menu / Exit to
  desktop)

## UI

All UI lives in `ui/` as plain RML/RCSS - edit the files and restart
the game to reload (no hot reload). Documents: main_menu.rml,
pause_menu.rml, options.rml (shared by main and pause), hud.rml
(crosshair + FPS counter). Theme: ui.rcss.

## Settings

The options panel (ui/options.rml) drives the engine live and persists
to game_settings.cfg (flat key=value, rewritten on Back/Exit):

- Display: resolution (1280x720 - 3840x2160), fps cap
  (60/90/144/uncapped), vsync, field of view (60-110)
- Graphics: post-processing master toggle + SSAO / SSR / FXAA,
  CSM shadows + quality (1024/2048/4096), HDR, ocean SSR
- Controls: mouse sensitivity (0.2-4.0), hide cursor in game

Values are read back from game_settings.cfg at boot; the file can be
deleted to restore defaults.
