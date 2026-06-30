-- Editor.lua — flythrough camera + scene-editor glue.
-- Loaded by the `fury` executable. Provides:
--   * WASD/arrow camera (carried over from Demo.lua)
--   * Scene-IO callbacks (New / Open / Import / Save As) registered with
--     the C++ editor via Editor.SetSceneIO
--   * Camera-tuning controls registered with Editor.SetCameraSettings
--   * Optional Camera top-level menu (Settings shortcut)
--
-- Menu ownership:
--   The C++ editor (engine/Fury/Editor) owns File and Window menus.
--   This script provides project-specific behavior via Editor.* callbacks.
--   Gui.SetMenuBarCallback emits the per-project Camera menu so it renders
--   between Window and any trailing engine menus.
--
-- The camera node lives OUTSIDE the scene's root tree, so File -> New
-- (which calls scene:Clear()) doesn't drop the camera. The camera is its
-- own root SceneNode that the pipeline references directly via
-- Pipeline.SetCurrentCamera.
--
-- Startup scene:
--   * No argv: load Resource/Scene/scene.bin (engine's bundled tank scene).
--   * argv:    `./fury Editor.lua <name>` opens <name> at startup.
--              The path is tried literally first, then with a Resource/Scene/
--              prefix, then any extension Importer.LoadScene supports.
--              On failure: fall back to scene.bin so the editor stays interactive.

local octree           = nil
local cam_node         = nil
local cam_pos          = nil           -- Vector4, set in on_init
local yaw              = 0.0
local pitch            = -math.rad(30.0)
local move_speed       = 5.0           -- world units per second
local mouse_sensitivity = 0.004

-- Mouse-drag state.
local dragging  = false
local last_mx   = 0
local last_my   = 0

-- ---------------------------------------------------------------------------
-- Status helper — pushes a one-line note to the editor's Console window.

local function set_status(msg)
    Editor.Log("info", msg)
end

-- Forward-declared helpers (their bodies appear later in the file but are
-- referenced from earlier local functions). Lua needs the name in scope at
-- closure-capture time.
local ensure_default_sun

-- Replace the active scene's content with `new_scene`. Camera + pipeline survive.
local function replace_active_scene(new_scene)
    local active = Scene.GetActive()
    active:Clear()
    Importer.MergeInto(active, new_scene)
    ensure_default_sun(active)
end

-- Enumerate Resource/Scene/ for the editor's File → Open / Import submenus.
-- Re-read every frame; Save As writes show up on the next frame's Open submenu.
local function list_scene_files()
    return FileUtil.ListDirectory(
        FileUtil.GetAbsPath("Resource/Scene/"),
        {".json", ".bin", ".gltf", ".glb", ".fbx"})
end

-- True for formats the engine can save back to (.json / .bin). Any other
-- extension routes File → Save through the Save As modal so imported
-- assets aren't silently overwritten.
local function is_native_format(filename)
    local ext = filename:lower():match("%.[^.]+$")
    return ext == ".json" or ext == ".bin"
end

local function open_scene(filename)
    local full = FileUtil.GetAbsPath("Resource/Scene/" .. filename)
    local imported = Importer.LoadScene(full)
    if imported then
        replace_active_scene(imported)
        Editor.SetCurrentScene(full, is_native_format(filename))
        set_status("opened " .. filename)
    else
        set_status("failed to open " .. filename)
    end
end

local function import_scene(filename)
    local full = FileUtil.GetAbsPath("Resource/Scene/" .. filename)
    local imported = Importer.LoadScene(full)
    if imported then
        local n = Importer.MergeInto(Scene.GetActive(), imported)
        ensure_default_sun(Scene.GetActive())
        set_status("imported " .. n .. " node(s) from " .. filename)
    else
        set_status("failed to import " .. filename)
    end
end

-- Shared core for File → Save (in-place) and File → Save As (named target).
-- Both pass an absolute path under Resource/Scene/ and expect a .json or .bin
-- extension. Returns true on success.
local function write_scene_to_path(full)
    local ext  = full:lower():match("%.[^.]+$") or ""
    local ok
    if ext == ".json" then
        ok = FileUtil.SaveFile(Scene.GetActive(), full)
    elseif ext == ".bin" then
        ok = FileUtil.SaveCompressedFile(Scene.GetActive(), full)
    else
        set_status("Save: unsupported extension '" .. ext .. "' (use .json or .bin)")
        return false
    end
    if ok then
        Editor.SetCurrentScene(full, true)
        set_status("wrote " .. full)
    else
        set_status("save failed for " .. full)
    end
    return ok
end

local function save_active_scene(filename)
    -- Save As path. Output goes into Resource/Scene/.
    write_scene_to_path(FileUtil.GetAbsPath("Resource/Scene/" .. filename))
end

local function save_scene_in_place(full)
    -- Save (no rename): Cmd+S already verified the path is native, so the
    -- extension dispatch in write_scene_to_path is a tautological double-check.
    write_scene_to_path(full)
end

-- ---------------------------------------------------------------------------
-- Default-sun fallback
--
-- The deferred-Lambert pipeline has no built-in ambient term — anything not
-- inside a light's radius renders pixel-black. To keep imported scenes
-- visible by default, attach a faint white directional sun when the scene
-- has none. Toggleable from Settings → Import → Auto-Add Default Sun.

local function tree_has_directional_light(node)
    if not node then return false end
    local l = node:GetLight()
    if l and l:GetType() == LightType.DIRECTIONAL then return true end
    for i = 0, node:GetChildCount() - 1 do
        if tree_has_directional_light(node:GetChildAt(i)) then return true end
    end
    return false
end

local function ensure_default_sun_impl(scene)
    if not scene then return end
    if not Editor.GetImportFlag("auto_default_sun", true) then return end
    if tree_has_directional_light(scene:GetRootNode()) then return end
    local sun = Light.Create()
    sun:SetType(LightType.DIRECTIONAL)
    sun:SetColor(Color(1.0, 1.0, 1.0, 1.0))
    sun:SetIntensity(0.3)
    sun:SetCastShadows(false)
    sun:CalculateAABB()
    local sun_node = SceneNode.Create("DefaultSun")
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, -math.rad(45.0), 0.0))
    sun_node:Recompose(false)
    sun_node:AddComponent(Transform.Create())
    sun_node:AddComponent(sun)
    sun_node:Recompose(true)
    scene:GetRootNode():AddChild(sun_node)
    scene:GetSceneManager():AddSceneNodeRecursively(sun_node)
    set_status("imported scene had no directional light — added DefaultSun")
end
ensure_default_sun = ensure_default_sun_impl

-- ---------------------------------------------------------------------------
-- Startup scene resolution

local function load_default_scene()
    local path = FileUtil.GetAbsPath("Resource/Scene/scene.bin")
    FileUtil.LoadSceneFromCompressedFile(Scene.GetActive(), path)
    Editor.SetCurrentScene(path, true)
end

local function file_exists(path)
    local f = io.open(path, "rb")
    if f then f:close(); return true end
    return false
end

local function resolve_startup_scene(name)
    if not name or name == "" then return nil end
    local literal = FileUtil.GetAbsPath(name)
    if file_exists(literal) then return literal end
    local prefixed = FileUtil.GetAbsPath("Resource/Scene/" .. name)
    if file_exists(prefixed) then return prefixed end
    return nil
end

-- ---------------------------------------------------------------------------

local function on_init()
    octree = OcTree.Create()

    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), octree))

    -- Startup scene: honor arg[1] when set; otherwise load the bundled scene.bin.
    local startup = arg and arg[1]
    if startup and startup ~= "" then
        local resolved = resolve_startup_scene(startup)
        if resolved then
            local imported = Importer.LoadScene(resolved)
            if imported then
                replace_active_scene(imported)
                Editor.SetCurrentScene(resolved, is_native_format(startup))
                set_status("opened " .. startup)
            else
                print("Editor.lua: Importer.LoadScene rejected '" .. startup .. "'; falling back to scene.bin")
                set_status("failed to open " .. startup .. " — using default scene")
                load_default_scene()
            end
        else
            print("Editor.lua: startup scene '" .. startup .. "' not found (tried literal and Resource/Scene/ prefix); falling back to scene.bin")
            set_status("not found: " .. startup .. " — using default scene")
            load_default_scene()
        end
    else
        load_default_scene()
    end

    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1, 100)
    camera:SetShadowFar(30)
    camera:SetShadowBounds(Vector4(-5), Vector4(5))

    cam_pos  = Vector4(0.0, 10.0, 25.0, 1.0)

    cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(cam_pos)
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
    cam_node:Recompose(false)
    cam_node:AddComponent(Transform.Create())
    cam_node:AddComponent(camera)
    cam_node:Recompose(true)

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    Pipeline.GetActive():SetCurrentCamera(cam_node)
    FileUtil.LoadPipelineFromFile(
        Pipeline.GetActive(),
        FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"))

    -- Wire the editor's File menu / Content Browser callbacks.
    Editor.SetSceneIO({
        list_files = list_scene_files,
        on_new     = function()
            Scene.GetActive():Clear()
            Editor.ClearCurrentScene()
            set_status("scene cleared")
        end,
        on_open    = open_scene,
        on_import  = import_scene,
        on_save    = save_scene_in_place,
        on_save_as = save_active_scene,
        scene_dir  = function() return FileUtil.GetAbsPath("Resource/Scene/") end,
    })

    -- Initial sync of the Auto-Add Default Sun flag — the Settings → Import
    -- checkbox reads/writes this; ensure_default_sun_impl reads it back.
    Editor.SetImportFlag("auto_default_sun", true)

    -- Camera tuning lives inside Settings → Camera now.
    Editor.SetCameraSettings({
        controls = {
            { label = "Move Speed",
              kind  = "slider",
              min   = 0.5, max = 50.0,
              get   = function() return move_speed end,
              set   = function(v) move_speed = v end },
            { label = "Mouse Sensitivity",
              kind  = "slider",
              min   = 0.0005, max = 0.02,
              get   = function() return mouse_sensitivity end,
              set   = function(v) mouse_sensitivity = v end },
        }
    })

    -- Console: evaluate ad-hoc Lua snippets. Errors land in the Console as
    -- error-level entries instead of crashing the engine.
    Editor.SetCommandHandler(function(line)
        local fn, err = load(line, "console")
        if not fn then
            Editor.Log("error", err)
            return
        end
        local ok, result = pcall(fn)
        if not ok then
            Editor.Log("error", tostring(result))
        elseif result ~= nil then
            Editor.Log("info", tostring(result))
        end
    end)
end

-- Forward and right vectors derived from yaw/pitch in the engine's convention.
local function camera_basis()
    local cy, sy = math.cos(yaw),   math.sin(yaw)
    local cp, sp = math.cos(pitch), math.sin(pitch)
    local fwd = Vector4(-cp * sy,  sp, -cp * cy, 0.0)
    local rgt = Vector4( cy,       0.0, -sy,      0.0)
    return fwd, rgt
end

local function on_update(dt)
    local input  = InputUtil.Instance()
    local has_kb = not Gui.WantCaptureKeyboard()
    -- The Viewport window is a real ImGui window, so hovering it sets
    -- WantCaptureMouse=true. We still want the camera-drag and wheel to
    -- work over the viewport, so treat the mouse as "available" when
    -- either ImGui doesn't want it OR the cursor is inside the viewport
    -- content rect (NOT the title bar / resize borders — IsViewportHovered
    -- would fire there too and cause the camera to rotate while the user
    -- drags the undocked viewport window around).
    local has_mo = (not Gui.WantCaptureMouse()) or Editor.IsViewportContentHovered()
    local focused = input:GetWindowFocused()

    -- ── mouse-drag yaw / pitch ────────────────────────────────────────────
    -- Suppress the camera-drag while a pick is resolving (defense in depth
    -- alongside the click-vs-drag gate in the C++ editor): a true click
    -- schedules a pick whose readback completes over the next two frames,
    -- and we don't want an immediately-following drag to fight it.
    local lmb_down = focused and has_mo and input:GetMouseDown(MouseButton.Left) and not Editor.IsPickInFlight()
    if lmb_down then
        local mx, my = input:GetMousePosition()
        if dragging then
            yaw   = yaw   - (mx - last_mx) * mouse_sensitivity
            pitch = pitch - (my - last_my) * mouse_sensitivity
            local limit = math.rad(89.0)
            if pitch >  limit then pitch =  limit end
            if pitch < -limit then pitch = -limit end
        end
        last_mx, last_my = mx, my
        dragging = true
    else
        dragging = false
    end

    -- ── WASD / arrows / Space / LControl translate ────────────────────────
    -- Gated on content-rect hover (not just OS window focus), so WASD
    -- doesn't translate the camera while the user is interacting with a
    -- docked panel or the viewport window's chrome. Combined with the
    -- same gate on the LMB drag, this guarantees drag and WASD share a
    -- single predicate — they cannot fall out of sync with each other.
    if focused and Editor.IsViewportContentHovered() then
        local fwd, rgt = camera_basis()
        local up       = Vector4(0.0, 1.0, 0.0, 0.0)
        local move     = Vector4(0.0, 0.0, 0.0, 0.0)

        if input:GetKeyDown(Key.W) or input:GetKeyDown(Key.Up)    then move = move + fwd end
        if input:GetKeyDown(Key.S) or input:GetKeyDown(Key.Down)  then move = move - fwd end
        if input:GetKeyDown(Key.A) or input:GetKeyDown(Key.Left)  then move = move - rgt end
        if input:GetKeyDown(Key.D) or input:GetKeyDown(Key.Right) then move = move + rgt end
        if input:GetKeyDown(Key.Space)    then move = move + up end
        if input:GetKeyDown(Key.LControl) then move = move - up end

        local mlen = move:Length()
        if mlen > 0.0 then
            local boost = input:GetKeyDown(Key.LShift) and 5.0 or 1.0
            cam_pos = cam_pos + move * (move_speed * boost * dt / mlen)
        end
    end

    -- ── mouse-wheel adjusts the base move speed (clamped) ─────────────────
    if has_mo then
        local wheel = input:GetMouseWheel()
        if wheel ~= 0.0 then
            move_speed = move_speed + wheel
            if move_speed < 0.5  then move_speed = 0.5  end
            if move_speed > 50.0 then move_speed = 50.0 end
        end
    end

    -- ── push state back into the SceneNode ────────────────────────────────
    cam_node:SetLocalPosition(cam_pos)
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
    cam_node:Recompose(false)

    -- ── ImGui overlays ────────────────────────────────────────────────────
    -- With WITH_EDITOR=ON, Editor::Tick has already emitted the menu bar,
    -- dockspace, and built-in windows during the engine's main loop (it
    -- runs between Gui::NewFrame and the user callback). With WITH_EDITOR=
    -- OFF, Gui.ShowDefault still runs the menu-bar callback so the Camera
    -- menu shows up; the editor windows simply don't exist.
    Gui.ShowDefault(dt)

    Pipeline.GetActive():Execute(octree)

    -- Gui::Render is invoked from the engine's main loop (Engine::Run),
    -- after Editor::TickPostRender, so ImGui composites on top of the 3D
    -- scene (including the Viewport window's render-target image). The
    -- pipeline no longer calls Gui::Render itself.
end

local function on_shutdown()
    octree   = nil
    cam_node = nil
    cam_pos  = nil
end

Engine.run({
    on_init     = on_init,
    on_update   = on_update,
    on_shutdown = on_shutdown,
})
