-- Demo.lua — flythrough camera + minimum-viable scene editor.
-- Loaded by the `fury` executable. Provides:
--   * WASD/arrow camera (carried over from earlier work)
--   * File menu: New / Open / Import / Save Scene As
--   * Camera menu: tuning panel
--
-- Two design choices worth flagging up-front:
--   1. The camera node lives OUTSIDE the scene's root tree, so File -> New
--      Scene (which calls scene:Clear()) doesn't drop the camera. The camera
--      is its own root SceneNode that the pipeline references directly via
--      Pipeline.SetCurrentCamera.
--   2. The File menu enumerates Resource/Scene/ every frame (cheap in v1 — a
--      directory_iterator + extension filter). When you Save Scene As, the
--      file shows up in the Open submenu on the next frame; this is intentional.

local octree           = nil
local cam_node         = nil
local cam_pos          = nil           -- Vector4, set in on_init
local yaw              = 0.0
local pitch            = -math.rad(30.0)
local move_speed       = 1.0
local mouse_sensitivity = 0.004

-- ImGui panel state.
local show_camera_window = false
local show_save_modal    = false
local save_path          = "scene_saved.json"

-- Transient status line (e.g. "wrote scene_saved.json", "open failed").
-- Cleared after status_ttl seconds.
local status_text = ""
local status_ttl  = 0.0

-- Mouse-drag state.
local dragging  = false
local last_mx   = 0
local last_my   = 0

-- ---------------------------------------------------------------------------
-- Scene editor helpers

local function set_status(msg, ttl)
    status_text = msg
    status_ttl  = ttl or 3.0
end

-- Replace the active scene's content with `new_scene`. Camera + pipeline survive.
-- `new_scene` is a freshly-imported Scene::Ptr from Importer.LoadScene; we
-- move its content into the active scene rather than swapping Scene.Active so
-- the camera-and-pipeline wiring (which references Scene.Active indirectly via
-- the octree) doesn't need to be reattached.
local function replace_active_scene(new_scene)
    local active = Scene.GetActive()
    active:Clear()
    Importer.MergeInto(active, new_scene)
end

-- Enumerate the contents of Resource/Scene/ for the Open / Import submenus.
-- Returns a Lua array of filenames (no path prefix).
local function list_scene_files()
    return FileUtil.ListDirectory(
        FileUtil.GetAbsPath("Resource/Scene/"),
        {".json", ".bin", ".gltf", ".glb", ".fbx"})
end

local function open_scene(filename)
    local full = FileUtil.GetAbsPath("Resource/Scene/" .. filename)
    local imported = Importer.LoadScene(full)
    if imported then
        replace_active_scene(imported)
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
        set_status("imported " .. n .. " node(s) from " .. filename)
    else
        set_status("failed to import " .. filename)
    end
end

local function save_active_scene(filename)
    -- Output goes into Resource/Scene/. Extension dictates format.
    local full = FileUtil.GetAbsPath("Resource/Scene/" .. filename)
    local ext  = filename:lower():match("%.[^.]+$") or ""
    local ok
    if ext == ".json" then
        ok = FileUtil.SaveFile(Scene.GetActive(), full)
    elseif ext == ".bin" then
        ok = FileUtil.SaveCompressedFile(Scene.GetActive(), full)
    else
        set_status("Save: unsupported extension '" .. ext .. "' (use .json or .bin)")
        return
    end
    set_status(ok and ("wrote " .. filename) or ("save failed for " .. filename))
end

-- ---------------------------------------------------------------------------
-- Menu bar — installed once in on_init.

local function build_menu_bar()
    -- File menu.
    if Gui.BeginMenu("File") then
        if Gui.MenuItem("New Scene") then
            Scene.GetActive():Clear()
            set_status("scene cleared")
        end
        if Gui.BeginMenu("Open Scene") then
            for _, name in ipairs(list_scene_files()) do
                if Gui.MenuItem(name) then open_scene(name) end
            end
            Gui.EndMenu()
        end
        if Gui.BeginMenu("Import") then
            for _, name in ipairs(list_scene_files()) do
                if Gui.MenuItem(name) then import_scene(name) end
            end
            Gui.EndMenu()
        end
        if Gui.MenuItem("Save Scene As") then show_save_modal = true end
        Gui.EndMenu()
    end
    -- Camera menu.
    if Gui.BeginMenu("Camera") then
        if Gui.MenuItem("Settings") then
            show_camera_window = not show_camera_window
        end
        Gui.EndMenu()
    end
end

-- ---------------------------------------------------------------------------

local function on_init()
    octree = OcTree.Create(
        Vector4(-1000, -1000, -1000, 1),
        Vector4( 1000,  1000,  1000, 1),
        2)

    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), octree))
    FileUtil.LoadSceneFromCompressedFile(
        Scene.GetActive(),
        FileUtil.GetAbsPath("Resource/Scene/scene.bin"))

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

    Gui.SetMenuBarCallback(build_menu_bar)
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
    local has_mo = not Gui.WantCaptureMouse()
    local focused = input:GetWindowFocused()

    -- Status line TTL decay.
    if status_ttl > 0 then
        status_ttl = status_ttl - dt
        if status_ttl <= 0 then status_text = "" end
    end

    -- ── mouse-drag yaw / pitch ────────────────────────────────────────────
    local lmb_down = focused and has_mo and input:GetMouseDown(MouseButton.Left)
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
    if focused and has_kb then
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
    Gui.ShowDefault(dt)

    if show_camera_window then
        local still_open, visible = Gui.Begin("Camera", show_camera_window)
        if visible then
            Gui.Text("WASD/arrows move, Space/LCtrl up/down")
            Gui.Text("Hold left mouse to look. LShift = 5x speed. Wheel = speed.")
            move_speed        = Gui.SliderFloat("Move Speed",        move_speed,        0.5,    50.0)
            mouse_sensitivity = Gui.SliderFloat("Mouse Sensitivity", mouse_sensitivity, 0.0005, 0.02)
        end
        Gui.End()
        show_camera_window = still_open
    end

    if show_save_modal then
        local still_open, visible = Gui.Begin("Save Scene As", show_save_modal)
        if visible then
            Gui.Text("Output path is relative to Resource/Scene/.")
            Gui.Text("Extension determines format (.json -> human-readable, .bin -> LZ4).")
            save_path = Gui.InputText("filename", save_path, 128)
            if Gui.Button("Save") then
                save_active_scene(save_path)
                show_save_modal = false
            end
        end
        Gui.End()
        show_save_modal = still_open
    end

    if status_text ~= "" then
        local _, visible = Gui.Begin("status", true)
        if visible then Gui.Text(status_text) end
        Gui.End()
    end

    Gui.Render()

    Pipeline.GetActive():Execute(octree)
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
