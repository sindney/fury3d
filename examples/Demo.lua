-- Demo.lua — Lua port of the former examples/Demo.cpp with a flythrough camera.
-- Loaded by the `fury` executable. Reproduces the same scene + pipeline as the
-- old C++ demo, plus WASD/arrow translation, mouse-drag look, mouse-wheel speed
-- adjust, and a small ImGui tuning panel.

local octree   = nil
local cam_node = nil

-- Camera state. yaw/pitch own the orientation explicitly so we don't have to
-- read the rotation back from the SceneNode each frame. Initial values match
-- the static pose the original demo opened with: position (0, 10, 25), yaw 0,
-- pitch -30 degrees down (rotation around local X).
local cam_pos           = nil           -- Vector4, set in on_init
local yaw               = 0.0
local pitch             = -math.rad(30.0)
local move_speed        = 1.0           -- units per second, tunable via slider
local mouse_sensitivity = 0.004          -- radians per pixel of drag, tunable

-- Camera tuning panel — hidden by default; toggle from the menu bar.
local show_camera_window = false

-- Camera tuning panel — hidden by default; toggle from the menu bar.
local show_camera_window = false

-- Mouse-drag state. last_mx/last_my are valid only while `dragging` is true.
local dragging = false
local last_mx  = 0
local last_my  = 0

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

    -- Register a "Camera" menu in the engine's menu bar. The callback runs
    -- inside ImGui::BeginMainMenuBar() each frame, after the engine's File
    -- and View menus.
    Gui.SetMenuBarCallback(function()
        if Gui.BeginMenu("Camera") then
            if Gui.MenuItem("Settings") then
                show_camera_window = not show_camera_window
            end
            Gui.EndMenu()
        end
    end)
end

-- Forward and right vectors derived from yaw/pitch in the engine's convention.
-- MathUtil.EulerRadToQuat(yaw, pitch, 0) builds q_yaw * q_pitch (yaw around
-- world Y, pitch around local X). At identity the camera looks down -Z and
-- its right is +X, so applying q_pitch then q_yaw to those base vectors:
--   forward = (-cos(pitch)*sin(yaw),  sin(pitch), -cos(pitch)*cos(yaw))
--   right   = ( cos(yaw),             0,          -sin(yaw))
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
