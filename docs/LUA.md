# Fury3D — Lua scripting

> Status (2026-06-18): minimum viable bridge — exposes the engine API surface
> that `examples/Demo.lua` actually exercises. Wider bindings (Light,
> MeshRender, Material, AnimationPlayer, InputUtil signals, etc.) are deferred
> to follow-up changes.

## How it works

The `fury` executable is a Lua launcher. It:

1. Opens an `sf::Window` at 1920×1080 with depth-24 / stencil-8 / GL 3.3 context settings (matching the previous C++ demo).
2. Calls `fury::Engine::Initialize(...)` to bring up the engine subsystems.
3. Opens a `sol::state` and calls `fury::LuaBindings::Register(lua)` to wire engine types into the script's global namespace.
4. Injects the active SFML window into Lua as `__window` (a private global the bindings read; don't shadow it in your scripts).
5. Loads and executes the script at `argv[1]`, defaulting to `Demo.lua` in the working directory.
6. The script calls `Engine.run({...callbacks...})` — that drops into C++ and runs the main loop until the window closes.
7. After `Engine.run` returns the Lua state is closed, then `fury::Engine::Shutdown()` runs, then the process exits.

## `Engine.run` callback contract

```lua
Engine.run({
    on_init         = function() ... end,    -- called once before the loop starts
    on_update       = function(dt) ... end,  -- called once per render frame; dt is a float (seconds-fraction since last fixed tick)
    on_fixed_update = function() ... end,    -- called 0..MAX_FRAMESKIP times per frame to catch up to 25 Hz
    on_shutdown     = function() ... end,    -- called once after the loop exits
}, {
    max_fps         = 144,    -- optional, default 144. 0 (or false) disables the cap.
    gui_scale       = 1.0,    -- optional, default 1.0. ImGuiStyle::ScaleAllSizes multiplier.
    gui_font_scale  = 1.0,    -- optional, default 1.0. Assigned to ImGuiIO::FontGlobalScale.
})
```

All four callbacks are optional. Omitting one is equivalent to passing `nil` — the engine just won't invoke that hook.

The second argument (the options table) is also optional. Calling `Engine.run({...callbacks...})` with no second arg is fine — defaults apply.

**Options:**

- `max_fps` (number, default `144`): frame-rate cap applied via `sf::Window::setFramerateLimit`. `0`, `false`, or a negative number disables the cap (the demo will run as fast as the host allows). Per-frame work + the underlying SFML limiter define the actual upper bound; on macOS expect ±5 FPS slack.
- `gui_scale` (number, default `1.0`): multiplier passed to `ImGuiStyle::ScaleAllSizes`. Controls widget sizes, padding, borders. `1.5` makes the UI about 50% larger.
- `gui_font_scale` (number, default `1.0`): assigned directly to `ImGuiIO::FontGlobalScale`. Controls only the bitmap font size. Usually keep this equal to `gui_scale`.

Unknown keys in the options table are silently ignored, so you can leave a `vsync = true` (or similar) entry in your script and it won't error — it just won't do anything yet.

Ordering guarantees per frame:
1. `RenderUtil:BeginFrame()` (engine-internal).
2. `Engine::HandleEvent` for each pending SFML event.
3. Zero or more `on_fixed_update` invocations (engine targets 25 Hz with a max of 5 catch-up ticks per frame, then yields).
4. `Gui::NewFrame(frame_dt)` (engine-internal).
5. `on_update(dt)` — your render-rate logic. **This is where you call `Pipeline.GetActive():Execute(scene_manager)`** to actually draw.
6. `window.display()` and `RenderUtil:EndFrame()` (engine-internal).

The loop exits when the SFML window receives `sf::Event::Closed`. After that, `on_shutdown` fires, then `Engine.run` returns.

**Profiler counters are previous-frame snapshots.** `RenderUtil:GetDrawCall()` and friends return values as of the most recently completed frame, not the in-progress one. This means `Gui.ShowDefault` can safely be drawn before or after `Pipeline.Execute` in `on_update` — counter visibility doesn't depend on ordering.

**Lifetime caveat.** Lua functions stored in the callback table keep their `sol::function` refs alive for the duration of `Engine.run`. Don't stash callback closures somewhere they outlive `Engine.run`'s return — they reference the Lua state, which is closed by the launcher right after the script finishes.

## Bound API reference

The bindings live in `engine/Fury/LuaBindings.cpp`. Read that file for the source of truth; this section is a quick reference.

### `Vector4`

```lua
local v = Vector4(1.0, 2.0, 3.0)        -- (x, y, z), w defaults to 1
local v4 = Vector4(1.0, 2.0, 3.0, 0.0)  -- full (x, y, z, w)
local s = Vector4(5.0)                  -- splat (5, 5, 5, 1)
v.x, v.y, v.z, v.w = 0, 1, 2, 1         -- field assignment via property
local len = v:Length()                  -- method call uses ':'
local n = v:Normalized()
v:Normalize()                           -- in-place
local sum = v + Vector4(1, 1, 1, 1)     -- binary +/-
local scaled = v * 2.5                  -- vector * scalar
local neg = -v                          -- unary minus

-- Static helpers
Vector4.XAxis  -- read-only constants
Vector4.YAxis
Vector4.ZAxis
```

### `Quaternion`

```lua
local q = Quaternion()                  -- identity (0, 0, 0, 1)
local r = Quaternion(0, 0, 0, 1)        -- explicit (x, y, z, w)
q:Identity()                            -- reset to identity in place
q.x, q.y, q.z, q.w = 0, 0, 0, 1
```

### `MathUtil`

```lua
MathUtil.PI                             -- constants
MathUtil.HalfPI
MathUtil.DegToRad
MathUtil.RadToDeg
local rad = MathUtil.DegreeToRadian(45.0)
local deg = MathUtil.RadianToDegree(rad)
local q = MathUtil.EulerRadToQuat(yaw, pitch, roll)   -- (yaw, pitch, roll) in radians
```

### `LogLevel`

```lua
LogLevel.EROR    -- error
LogLevel.WARN    -- warning
LogLevel.INFO    -- info
LogLevel.DBUG    -- debug
```

### `OcTree`

```lua
local tree = OcTree.Create(
    Vector4(-1000, -1000, -1000, 1),
    Vector4( 1000,  1000,  1000, 1),
    2)  -- max depth
```

### `Scene`

```lua
local scene = Scene.Create("name", FileUtil.GetAbsPath(), tree)
Scene.SetActive(scene)
local s = Scene.GetActive()
local root = s:GetRootNode()
local mgr  = s:GetSceneManager()
local em   = s:GetEntityManager()
local dir  = s:GetWorkingDir()
s:SetWorkingDir("/some/path/")
```

> **Why `Scene.SetActive(...)` instead of `Scene.Active = ...`?** sol2's
> `sol::property` mechanism assigns onto the Lua-side metatable rather than the
> C++ static field in some configurations. Explicit getter/setter functions
> sidestep the issue. `Pipeline.GetActive()` / `Pipeline.SetActive()` follow
> the same pattern.

### `Component` (base type)

The `Component` Lua usertype is registered with no constructor; you can't instantiate one directly. Its purpose is to let `SceneNode:AddComponent` accept any subclass.

### `Transform`

```lua
local t = Transform.Create()                                   -- default
local t = Transform.Create(Vector4(0,0,0,1), Quaternion(),
                           Vector4(1,1,1,1))                   -- with TRS
```

### `Camera`

```lua
local cam = Camera.Create()
cam:PerspectiveFov(0.7854, 1.778, 1, 100)   -- (fov_rad, aspect, near, far)
cam:SetShadowFar(30)
cam:SetShadowBounds(Vector4(-5), Vector4(5))
cam:GetNear()       -- returns float
cam:GetFar()        -- returns float
cam:GetShadowFar()  -- returns float
```

### `SceneNode`

```lua
local node = SceneNode.Create("name")
node:SetLocalPosition(Vector4(0, 10, 25, 1))         -- Vector4 form
node:SetLocalPosition(0, 10, 25)                     -- 3-float form
node:SetLocalRoattion(Quaternion())                  -- Quaternion form
node:SetLocalRoattion(0, 0.5, 0)                     -- (x, y, z) Euler form
node:SetLocalRoattion(Vector4.YAxis, MathUtil.PI)    -- (axis, angle) form
node:SetLocalScale(Vector4(2, 2, 2, 1))
node:SetLocalScale(2.0)                              -- uniform scalar
node:Recompose(false)                                -- false = don't update octree yet
node:Recompose(true)                                 -- true  = update octree (do this once after AddComponent)
node:AddComponent(Transform.Create())
node:AddComponent(camera)
local pos = node:GetWorldPosition()
local n = node:GetChildCount()
local c = node:GetChildAt(0)
```

> **Note**: `SetLocalRoattion` is the engine-side spelling (sic) — the engine
> source has this typo from years ago and the Lua bindings preserve it.

### `Pipeline` (base) and `PrelightPipeline`

```lua
Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
Pipeline.GetActive():SetCurrentCamera(cam_node)
Pipeline.GetActive():Execute(octree)
```

### `FileUtil`

```lua
local exe_dir = FileUtil.GetAbsPath()                                -- exe-dir, with trailing slash
local p = FileUtil.GetAbsPath("Resource/Scene/scene.bin")            -- exe-dir + relative
local p_fs = FileUtil.GetAbsPath("foo\\bar", true)                   -- second arg: convert backslash to forward
local exists = FileUtil.FileExist(p)
FileUtil.LoadSceneFromCompressedFile(scene, path)                    -- LZ4 .bin loader
FileUtil.LoadPipelineFromFile(pipeline, path)                        -- JSON pipeline loader
FileUtil.SaveFile(scene, path)                                       -- human-readable JSON writer
FileUtil.SaveCompressedFile(scene, path)                             -- LZ4 .bin writer
local files = FileUtil.ListDirectory(dir)                            -- enumerate files in a directory
local gltf  = FileUtil.ListDirectory(dir, {".gltf", ".fbx"})         -- with extension filter (case-insensitive)
```

`FileUtil.ListDirectory(path, filter)` returns a Lua array of filenames (no
path prefix). The second argument is optional; when provided, only files
whose extension matches one of the entries are returned. Hidden files
(leading `.`) and subdirectories are always excluded. Missing path → empty
array + a `FURYW` warning in `Log.txt`.

> **Why typed names?** sol2's overload resolution from a Lua usertype to a
> C++ `shared_ptr<Base>` parameter is fragile in v3.5. We bind the
> Serializable-typed loaders as distinctly-named functions per concrete subtype
> instead of relying on overload picking.

### `Importer` — runtime asset import

Same translation code as the offline `fury convert` CLI — see
`docs/CLI.md` for the full lossy-mapping table. All four functions return
`nil` on error; the reason is logged to `Log.txt` via `FURYE`. Demo.lua's
File menu treats `nil` as "log and skip, keep the previous active scene
visible."

```lua
local scene = Importer.LoadGltf(path)                                -- .gltf or .glb
local scene = Importer.LoadFbx(path)                                 -- .fbx (chained via FBX2glTF subprocess)
local scene = Importer.LoadScene(path)                               -- dispatch by extension
local n     = Importer.MergeInto(target_scene, source_scene)         -- returns count of merged root children
```

`LoadScene` dispatch:
- `.json` → `FileUtil.LoadFile` against a fresh `Scene`
- `.bin`  → `FileUtil.LoadCompressedFile`
- `.gltf` / `.glb` → `LoadGltf`
- `.fbx`  → `LoadFbx`

`MergeInto(target, source)` appends `source`'s root children to `target`'s
root, transfers its materials/meshes/animations into `target`'s
`EntityManager` (dedupes by hashcode with a `FURYW` per collision), and
re-registers the new subtrees with `target`'s `SceneManager` so they show
up in the next `Pipeline:Execute`. `source` is left empty and can be
discarded.

**Heads-up — FBX import blocks the render thread.** Loading an FBX runs
the `FBX2glTF` subprocess synchronously; expect a 1–3 second freeze for
typical models. v1 doesn't show a progress indicator; clicking
`File → Open Scene → tank.fbx` makes the window appear unresponsive
until the conversion completes. Use the offline CLI (`fury convert fbx`)
for bigger models or batch jobs.

### `RenderUtil`

```lua
local r = RenderUtil.Instance()
-- (no methods bound this round — RenderUtil is internally driven by Engine.run)
```

### `Input`

```lua
local input = InputUtil.Instance()

-- Polling — call from on_update each frame.
if input:GetKeyDown(Key.W) then ... end
if input:GetMouseDown(MouseButton.Left) then ... end
local x, y     = input:GetMousePosition()    -- in window-local points
local wheel    = input:GetMouseWheel()
local focused  = input:GetWindowFocused()
local w, h     = input:GetWindowSize()
```

`Key` and `MouseButton` are plain Lua tables of integer-valued enums populated
from `sf::Keyboard::Key` and `sf::Mouse::Button`. The currently-bound subset:

```
Key.A … Key.Z
Key.Num0 … Key.Num9
Key.Space, Key.LShift, Key.RShift, Key.LControl, Key.RControl, Key.LAlt, Key.RAlt
Key.Up, Key.Down, Key.Left, Key.Right
Key.Escape, Key.Enter, Key.Tab, Key.Backspace

MouseButton.Left, MouseButton.Right, MouseButton.Middle
```

Need a key that isn't here (`F1`, ``Grave``, …)? Add one line to the `Key`
block in `engine/Fury/LuaBindings.cpp`:

```cpp
key_tbl["F1"] = static_cast<int>(sf::Keyboard::Key::F1);
```

Only polling is bound. The C++ `InputUtil::OnKeyDown` / `OnMouseMove` signals
are not yet exposed to Lua — bridging a `sol::function` closure to the
member-pointer-based `Signal<Args...>` requires its own change. For
continuous input (camera fly, hold-to-move), polling is the right tool
anyway.

### `Gui`

```lua
-- Engine UI
Gui.ShowDefault(dt)              -- engine's built-in menu bar + Profiler
Gui.Render()                     -- emit ImGui draw lists

-- Input-capture queries (gate camera input when cursor is over a widget)
if Gui.WantCaptureMouse()    then ... end
if Gui.WantCaptureKeyboard() then ... end

-- Window primitives. Begin returns two values: still_open (after the X
-- button click) and visible (false when collapsed). Always pair Begin
-- with End regardless of visibility.
local still_open, visible = Gui.Begin("My Panel", show_my_panel)
if visible then
    Gui.Text("hello")
    move_speed = Gui.SliderFloat("Move Speed", move_speed, 0.5, 50.0)
    enabled    = Gui.Checkbox("Enabled", enabled)
    save_path  = Gui.InputText("filename", save_path, 128)
    if Gui.Button("Click me") then ... end
    Gui.Separator()
end
Gui.End()
show_my_panel = still_open

-- Menu bar extension. The callback runs inside ImGui::BeginMainMenuBar()
-- each frame, after the engine's built-in File / View menus. Pass nil to
-- clear.
Gui.SetMenuBarCallback(function()
    if Gui.BeginMenu("Camera") then
        if Gui.MenuItem("Settings") then
            show_camera_window = not show_camera_window
        end
        Gui.EndMenu()
    end
end)
```

The mutating widgets (`SliderFloat`, `Checkbox`, `InputText`) return the new
value rather than taking a pointer — sol2 doesn't auto-marshal Lua numbers
or strings into `float*` / `char*`, so we use this in/out shape. Idiomatic
call: `value = Gui.X("...", value, ...)`.

#### Scene editor menu — full pattern from Demo.lua

The shipped `examples/Demo.lua` adds a `Scene` menu (alongside its `Camera`
menu) with `New` / `Open` / `Import` / `Save As...`. The pattern is
reusable for any script that wants a minimum-viable editor.

> **Don't use `"File"` as the top-level label.** The engine's built-in menu
> bar already has a "File" entry (carrying Quit), and ImGui dedupes
> top-level menu items by label hash — a second `BeginMenu("File")` shares
> the same ID and ends up visually mis-sized. Pick a distinct,
> action-shaped name like "Scene".

```lua
local show_save_modal = false
local save_path       = "scene_saved.json"
local status_text     = ""
local status_ttl      = 0.0

local function set_status(msg, ttl)
    status_text = msg
    status_ttl  = ttl or 3.0
end

local function list_scene_files()
    return FileUtil.ListDirectory(
        FileUtil.GetAbsPath("Resource/Scene/"),
        {".json", ".bin", ".gltf", ".glb", ".fbx"})
end

Gui.SetMenuBarCallback(function()
    if Gui.BeginMenu("Scene") then
        if Gui.MenuItem("New") then
            Scene.GetActive():Clear()
            set_status("scene cleared")
        end
        if Gui.BeginMenu("Open") then
            for _, name in ipairs(list_scene_files()) do
                if Gui.MenuItem(name) then
                    local imp = Importer.LoadScene(
                        FileUtil.GetAbsPath("Resource/Scene/" .. name))
                    if imp then
                        Scene.GetActive():Clear()
                        Importer.MergeInto(Scene.GetActive(), imp)
                        set_status("opened " .. name)
                    else
                        set_status("failed to open " .. name)
                    end
                end
            end
            Gui.EndMenu()
        end
        if Gui.BeginMenu("Import") then
            -- same enumeration; merges instead of clearing
            for _, name in ipairs(list_scene_files()) do
                if Gui.MenuItem(name) then
                    local imp = Importer.LoadScene(
                        FileUtil.GetAbsPath("Resource/Scene/" .. name))
                    if imp then
                        local n = Importer.MergeInto(Scene.GetActive(), imp)
                        set_status("imported " .. n .. " node(s) from " .. name)
                    end
                end
            end
            Gui.EndMenu()
        end
        if Gui.MenuItem("Save As...") then show_save_modal = true end
        Gui.EndMenu()
    end
end)

-- Save modal — rendered from on_update, NOT the menu callback:
function on_update(dt)
    if status_ttl > 0 then
        status_ttl = status_ttl - dt
        if status_ttl <= 0 then status_text = "" end
    end
    -- ... other UI ...
    if show_save_modal then
        local still_open, visible = Gui.Begin("Save Scene As", show_save_modal)
        if visible then
            save_path = Gui.InputText("filename", save_path, 128)
            if Gui.Button("Save") then
                local full = FileUtil.GetAbsPath("Resource/Scene/" .. save_path)
                local ext  = save_path:lower():match("%.[^.]+$") or ""
                if ext == ".json" then FileUtil.SaveFile(Scene.GetActive(), full)
                elseif ext == ".bin"  then FileUtil.SaveCompressedFile(Scene.GetActive(), full)
                end
                show_save_modal = false
            end
        end
        Gui.End()
        show_save_modal = still_open
    end
end
```

Key conventions from Demo.lua you may want to copy:

- **The camera node lives OUTSIDE the active scene's root tree.** That way
  `Scene:Clear()` doesn't drop your camera. Build it as a standalone
  `SceneNode` and reference it from the pipeline via
  `Pipeline.SetCurrentCamera(cam_node)`.
- **Enumerate the directory every frame.** It's a cheap `directory_iterator
  + extension filter`. Saved files appear in the `Open` submenu on the next
  frame; intentional.
- **Render modals from `on_update`, not the menu callback.** The menu
  callback runs inside `ImGui::BeginMainMenuBar()`; modal windows need to be
  outside that scope.

### `Engine`

```lua
Engine.run({ on_init = ..., on_update = ..., on_fixed_update = ..., on_shutdown = ... })
Engine.run({...callbacks...}, { max_fps = 60, gui_scale = 1.25, gui_font_scale = 1.25 })
```

`Engine.run` is the **only** Engine entry point exposed to Lua. `Initialize`, `HandleEvent`, `Update`, `FixedUpdate`, `Shutdown` are launcher-level concerns and are not callable from scripts. The optional second argument is the options table — see the contract section above for keys.

## Hello, world

A minimal `.lua` script that opens a window and prints a heartbeat every fixed tick:

```lua
local frame = 0

local function on_init()
    print("hello, fury3d")
end

local function on_update(dt)
    frame = frame + 1
end

local function on_fixed_update()
    if frame % 25 == 0 then
        print("frame " .. frame)
    end
end

local function on_shutdown()
    print("goodbye after " .. frame .. " frames")
end

Engine.run({
    on_init = on_init,
    on_update = on_update,
    on_fixed_update = on_fixed_update,
    on_shutdown = on_shutdown,
})
```

Run with: `./fury hello.lua` (from the working directory you want resources resolved against).

## Gotchas

- **`:` vs `.`** — usertype methods use `:` (which passes `self` as the first arg). Free functions in tables (`MathUtil.EulerRadToQuat`, `FileUtil.GetAbsPath`) use `.`. Mixing them fails silently or with a sol2 error about argument types.
- **Vector4 arithmetic returns by value.** `local sum = a + b` produces a new `Vector4`; the original `a` is unchanged. If you want in-place modification, use `:Normalize()` or assign through `.x`/`.y`/etc.
- **`Scene.Active` and `Pipeline.Active` are getter/setter functions, not properties.** Use `Scene.SetActive(...)` and `Scene.GetActive()` (same for `Pipeline`). See the §6 box above for why.
- **`__window`** is a private launcher-injected global pointing at the active `sf::Window`. The Lua bindings read it inside `Engine.run`. Don't shadow this name in your scripts or you'll break `Engine.run`.
- **Lua stdlib is fully open.** The launcher loads `base`, `string`, `math`, `table`, `io`, `os`, `package`. Scripts can read/write files, exec processes, etc. Acceptable for a dev tool today; revisit before shipping any script-running runtime to end users.
- **Callback errors are caught.** Unhandled errors inside `on_init` / `on_update` / `on_fixed_update` / `on_shutdown` are logged via `FURYE` and the loop continues. The engine doesn't abort on a Lua callback error.
- **Only the keys listed in `LuaBindings.cpp`'s `Key` table are pre-bound.** Add more entries to the table if you need keys outside the demo's set.
- **Component access is one-way.** `SceneNode:AddComponent(c)` works; the templated `GetComponent<T>()` is not bound. If you need to read components back, do it C++-side (the engine code can still introspect components freely).
- **The vector type is Vector4 even for 3D positions.** This is a long-standing engine convention, not a Lua-binding artifact. See `docs/ARCHITECTURE.md` §5.1 for the rationale.
- **macOS Retina is currently non-native.** SFML 3.1's macOS backend hardcodes `highDpi = NO` (`engine/ThirdParty/SFML/src/SFML/Window/macOS/SFOpenGLView.mm:128`), so the OpenGL surface is sized in screen points, not backing pixels. UI and scene look slightly soft on Retina displays — there's no `gui_scale` value that produces sharp pixels short of patching SFML. See `docs/ARCHITECTURE.md` §16. The engine compensates by defaulting `gui_scale` and `gui_font_scale` to `1.0` (the previous SFML-2-era 2× compensation would now double-scale).
- **Calling Gui functions from `on_init` is undefined.** The engine initializes ImGui inside `Engine::Run`, *after* `on_init` returns. Touching `Gui.ShowDefault` / `Gui.Render` from `on_init` will hit an uninitialized ImGui context. Stick to `on_update`.

## Future expansion

When the bridge needs to grow:

1. Add includes and the new `lua.new_usertype<T>(...)` blocks at the bottom of `engine/Fury/LuaBindings.cpp`.
2. If `T` derives from another bound type, list the chain via `sol::base_classes, sol::bases<...>()`.
3. If `T` has overloads with the same arity, use `sol::overload(static_cast<...>(&T::method), ...)` to disambiguate.
4. If a method takes `shared_ptr<Base>` where Base is registered, do **not** rely on sol2's automatic upcast — bind a typed wrapper as a free function (see `LoadSceneFromCompressedFile` / `LoadPipelineFromFile` for the pattern).
5. Add a new section to this doc with the new methods.
6. Run the demo to make sure existing bindings still work — sol2's heavy template instantiation can flag incompatibilities at compile time, but lifetime issues only show up at run time.
