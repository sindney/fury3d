-- test_editor_shutdown.lua — same as Editor.lua but auto-closes after N frames
local frame_count = 0
local exit_at = 30

local octree           = nil
local cam_node         = nil
local cam_pos          = nil
local yaw              = 0.0
local pitch            = -math.rad(30.0)
local move_speed       = 5.0
local mouse_sensitivity = 0.004
local dragging  = false
local last_mx   = 0
local last_my   = 0

local function on_init()
    octree = OcTree.Create()
    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), octree))

    local path = FileUtil.GetAbsPath("Resource/Scene/scene.bin")
    FileUtil.LoadSceneFromCompressedFile(Scene.GetActive(), path)
    Editor.SetCurrentScene(path, true)

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

    -- Default sun to make Editor.lua match
    local sun = Light.Create()
    sun:SetType(LightType.SUN)
    sun:SetIntensity(0.3)
    sun:SetCastShadows(false)
    sun:CalculateAABB()
    local sun_node = SceneNode.Create("DefaultSun")
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, -math.rad(45.0), 0.0))
    sun_node:Recompose(false)
    sun_node:AddComponent(Transform.Create())
    sun_node:AddComponent(sun)
    sun_node:Recompose(true)
    Scene.GetActive():GetRootNode():AddChild(sun_node)
    Scene.GetActive():GetSceneManager():AddSceneNodeRecursively(sun_node)
end

local function camera_basis()
    local cy, sy = math.cos(yaw),   math.sin(yaw)
    local cp, sp = math.cos(pitch), math.sin(pitch)
    local fwd = Vector4(-cp * sy,  sp, -cp * cy, 0.0)
    local rgt = Vector4( cy,       0.0, -sy,      0.0)
    return fwd, rgt
end

local function on_update(dt)
    frame_count = frame_count + 1
    if frame_count >= exit_at then
        Window.Close()
    end

    local input  = InputUtil.Instance()
    local has_kb = not Gui.WantCaptureKeyboard()
    local has_mo = (not Gui.WantCaptureMouse()) or Editor.IsViewportContentHovered()
    local focused = input:GetWindowFocused()

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

    if has_mo then
        local wheel = input:GetMouseWheel()
        if wheel ~= 0.0 then
            move_speed = move_speed + wheel
            if move_speed < 0.5  then move_speed = 0.5  end
            if move_speed > 50.0 then move_speed = 50.0 end
        end
    end

    cam_node:SetLocalPosition(cam_pos)
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
    cam_node:Recompose(false)

    Gui.ShowDefault(dt)
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
