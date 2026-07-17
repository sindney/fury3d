-- test_editor_full_shutdown.lua — full Editor.lua path that captures sol::protected_function
-- This mirrors what Editor.lua does that triggers the segfault:
--   Editor.SetSceneIO, Editor.SetCommandHandler, Editor.SetFrameSelectionHandler,
--   Editor.SetCameraSettings.
local frame_count = 0
local exit_at = 30

-- Forward-declared state
local octree           = nil
local cam_node         = nil
local cam_pos          = nil
local yaw              = 0.0
local pitch            = -math.rad(30.0)
local move_speed       = 5.0
local mouse_sensitivity = 0.004

local SCENE_FILE_FILTER = "json,bin,gltf,glb,fbx"

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

    -- Default sun
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

    -- THE TRIGGERS: these all capture sol::protected_function.
    Editor.SetSceneIO({
        on_new     = function() Editor.ClearCurrentScene() end,
        on_open    = function(p) Editor.SetCurrentScene(p, true) end,
        on_import  = function(p) Editor.SetCurrentScene(p, false) end,
        on_save    = function(p) end,
        on_save_as = function(p) end,
        scene_dir  = function() return FileUtil.GetAbsPath("Resource/Scene/") end,
    })

    Editor.SetCommandHandler(function(line)
        local fn = load(line, "console")
        if fn then pcall(fn) end
    end)

    Editor.SetFrameSelectionHandler(function(node)
        if not node then return end
        cam_pos = node:GetWorldPosition()
    end)

    Editor.SetCameraSettings({
        controls = {
            { label = "Move Speed", kind = "slider", min = 0.5, max = 50.0,
              get = function() return move_speed end,
              set = function(v) move_speed = v end },
        }
    })
end

local function on_update(dt)
    frame_count = frame_count + 1
    if frame_count >= exit_at then Window.Close() end

    if Editor.IsViewportContentHovered() then
        local cp, sp = math.cos(pitch), math.sin(pitch)
        local cy, sy = math.cos(yaw),   math.sin(yaw)
        cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
    end

    Gui.ShowDefault(dt)
    Pipeline.GetActive():Execute(octree)
end

local function on_shutdown()
    octree, cam_node, cam_pos = nil, nil, nil
end

Engine.run({
    on_init     = on_init,
    on_update   = on_update,
    on_shutdown = on_shutdown,
})
