# Fury3D - Game UI (RmlUi)

> Status (2026-09-25): shipped. Game UI is RmlUi: documents in RML (HTML
> subset), styled with RCSS (CSS subset: flexbox, absolute positioning,
> pseudo-classes, transitions, font effects, ninepatch decorators, data
> binding). The library hands mesh + textures to the engine; rendering
> rides the render-thread frame packet like ImGui. The editor's ImGui is
> untouched - GameUI is the game-facing path. Demo:
> examples/IslandGame.lua + examples/Projects/ocean/ui/.

## Quick start

```lua
local ctx = rmlui.contexts["main"]

rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Regular.ttf")  -- before LoadDocument
GameUI.LoadDocument("ui/main_menu.rml")                   -- scene working dir
GameUI.Show("ui/main_menu.rml")

local doc = ctx.documents["ui/main_menu.rml"]
doc:GetElementById("start"):AddEventListener("click", function(ev) ... end)
```

- `GameUI.LoadDocument(path)` returns the path on success, nil on
  failure. The load path is the document id for its whole lifecycle.
- `GameUI.Show/Hide/Toggle/Close(path)` and `GameUI.IsLoaded(path)`
  return bools (false when that path is not loaded). `Close` defers
  destruction to the next `ctx:Update()`.
- Rich per-element work (data models, event listeners, tree walks) goes
  through the `rmlui` plugin global. The `rmlui` global is a userdata
  with metamethods, not a table; `GameUI` is a plain table keyed by
  load path.

## Asset paths

- Documents and relative hrefs resolve against the scene's working dir
  (Scene.Path semantics): a scene created with working dir
  `Projects/ocean/` loads `GameUI.LoadDocument("ui/main_menu.rml")`
  from `Projects/ocean/ui/main_menu.rml`.
- The `Engine/` prefix maps to the engine resource root (`Resource/`
  next to the binaries): `Engine/Ui/smoke.rml` ->
  `examples/Resource/Ui/smoke.rml`, `Engine/Fonts/LatoLatin-Regular.ttf`
  -> `examples/Resource/Fonts/LatoLatin-Regular.ttf`.

## Fonts

- Fonts live in `Resource/Fonts/` and load through the VFS:
  `rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Regular.ttf")`. The face
  name is the font's family name (`font-family: LatoLatin;` for the
  LatoLatin-*.ttf faces).
- Load fonts BEFORE any document that references them: a failed
  font-family lookup is cached permanently per element, so a document
  loaded before its face renders in the default face forever.
- After loading and showing documents, run `ctx:Update()` before the
  first visible frame (the demo runs it twice). Otherwise text has no
  computed metrics yet and returns height-1 rectangles; nothing
  renders.

## RML/RCSS basics + traps

RML is HTML-like; RCSS is CSS-like. A complete theme to copy lives at
`examples/Projects/ocean/ui/ui.rcss`. Traps that bite:

- **No UA stylesheet.** The initial value of `display` is `inline`, so
  bare divs are zero-size and width/height are ignored on them. Reset
  every block-level tag you use:
  `body, div, p, span, h1, h2, button, input { display: block; }`.
- **No comma-list font-family.** `font-family: Lato, sans-serif;`
  turns the whole string into one face name and never matches. One face
  name per declaration.
- **border-radius takes px, not %.** `border-radius: 50%;` is dropped;
  use px (`7px` on a 14px box is a circle).
- **Range sliders:** the internal parts match by TAG name, not class or
  id: `input[type=range] slidertrack`, `sliderprogress`, `sliderbar`;
  arrows are `sliderarrowdec` / `sliderarrowinc` (hide them if unwanted).
  RmlUi ships no default look for them, and the input needs an explicit
  `height` or the parts collapse to zero.
- **The range `change` event fires BEFORE the value attribute
  updates.** In `onchange`, `GetAttribute("value")` still returns the
  old value - read `event.parameters.value`:
  `<input type="range" onchange="Opt_Fov(event.parameters.value)">`.
- **Inline handlers are `function(event, element, document)`.**
  `onclick="Menu_Start()"` compiles to a closure that resolves the Lua
  global at event time, so the global may be defined after
  LoadDocument as long as it exists before the event fires.
- **Percentage sizes track the context (window) size.** The document
  body fills the context, so `width: 100%` on body plus absolutely
  positioned children follows window resizes for free.
- **Flexbox works for centering:** `display: flex; align-items: center;
  justify-content: center;` on a full-size container centers a modal at
  any resolution.
- Textures blend premultiplied (images are premultiplied at decode).
  Eight-digit hex colors carry alpha (`#4fd1ff18`).

## Lua interop

- **Data models**: open the model BEFORE loading the document -
  `{{ var }}` interpolation resolves at parse time:

  ```lua
  local model = ctx:OpenDataModel("hud", { score = 0 })
  GameUI.LoadDocument("ui/hud.rml")        -- body carries data-model="hud"
  model.score = 42; ctx:Update()           -- rebinds the {{ score }} text
  ```

- **Events**: `el:AddEventListener("click", function(ev) ... end)`;
  `el:DispatchEvent("click", { button = 0 })` synthesizes a click. The
  inline handler still fires alongside an AddEventListener callback.
- **Visibility**: the bindings expose no IsVisible; read
  `doc.style.visibility` ("visible"/"hidden") after a `ctx:Update()`.
- **Layout** runs inside `Context::Render()`; the engine calls Update +
  Render every frame so windowed scripts never think about it. Headless
  scripts (`fury exec`, `furye-cli gui`) must call `ctx:Update()` and
  `ctx:Render()` themselves before reading `offset_width` etc.

## Input

SFML events forward to the UI automatically. Gate world input behind
menus with `GameUI.WantCaptureMouse()/WantCaptureKeyboard()` (mirrors
the ImGui `Gui.WantCapture*` pattern). HUD documents that float over
the game view should set `pointer-events: none` so they never capture.

## Settings plumbing (game-facing)

What the demo wires (examples/IslandGame.lua):
`Window.SetFpsCap(60|90|144|false)`, `Window.SetVsync(b)`,
`Window.SetResolution(w, h)` (windowed; camera aspect follows the
resize), `InputUtil.Instance():SetCursorGrabbed(b)/SetCursorVisible(b)`,
`CharacterController:SetFirstPerson(true)` + `SetCameraDistance(0)` +
`SetCameraHeight(cm)` + `SetMouseSensitivity(f)`, `Camera:SetFov(radians)`,
and RenderSettings chain toggles by walking `rs:GetChain()` for the
index then `rs:SetEffectEnabled(idx, b)` + `Pipeline.ApplyRenderSettings(pl, rs)`.

## Debugging

- `FURY_UI_DEBUG=1` opens the RmlUi visual debugger (element tree,
  computed styles, box model) in any windowed run.
- Headless agent loop: `furye-cli gui tree|inspect|event|shot` -
  see the `furye-cli gui` section in docs/CLI.md. Engine logs share
  stdout with the JSON output: extract with `| grep '^{'`.
- Windowed runs accept `--screenshot <path> [--screenshot-frame N]`
  (and `--screenshot-series` for contact sheets); see "Screenshot mode"
  in docs/CLI.md.

## Threading (C++ readers)

RmlUi itself runs entirely on the game thread; only GL submission
crosses over. `GameUIRenderer` (the RmlUi RenderInterface) records
compiled geometry + batches into a `GameUIFrameData` during
`Context::Render()`; the frame packet carries it
(`FramePacket::uiFrame`) and the frame executor replays it on the
render thread. Texture loads become engine `Texture` objects whose GL
upload is dispatched through the render thread - the game thread never
touches GL. Details: engine/Fury/GameUIRenderer.{h,cpp} and
engine/Fury/GameUI.h.

## Limitations

- No SDF fonts: FreeType raster atlas baked per size; text scaled far
  above its baked size softens. Use font effects (outline/shadow/glow);
  a custom FontEngineInterface is the upgrade path.
- No box-shadow (use font-effect shadow on text; decorations on
  panels).
- Scissor is y-flipped between RmlUi and GL - handled internally,
  authors never see it.
- dp ratio is 1 on macOS (SFML forces highDpi=NO); it follows the
  system DPI elsewhere.

## Demo

`./fury IslandGame.lua` from examples/: island main menu, ESC pause
menu, options panel, FPS HUD. See examples/Projects/ocean/README.md.
