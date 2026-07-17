-- Test shutdown with full editor-like setup.
local frame_count = 0
local exit_at = 20

local function on_init()
    local octree = OcTree.Create()
    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), octree))
    local path = FileUtil.GetAbsPath("Resource/Scene/scene.bin")
    FileUtil.LoadSceneFromCompressedFile(Scene.GetActive(), path)
    Editor.SetCurrentScene(path, true)

    -- Camera + cam_node (matches Editor.lua)
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1, 100)
    camera:SetShadowFar(30)
    camera:SetShadowBounds(Vector4(-5), Vector4(5))
    local cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(Vector4(0, 10, 25, 1))
    cam_node:AddComponent(Transform.Create())
    cam_node:AddComponent(camera)
    cam_node:Recompose(true)

    -- Spotlight light to trigger spotlight_convex creation
    local spotlight = Light.Create()
    spotlight:SetType(LightType.SPOT)
    local spot_node = SceneNode.Create("SpotLight")
    spot_node:AddComponent(Transform.Create())
    spot_node:AddComponent(spotlight)
    spot_node:Recompose(true)
    Scene.GetActive():GetRootNode():AddChild(spot_node)
    Scene.GetActive():GetSceneManager():AddSceneNodeRecursively(spot_node)

    -- Pipeline (matches Editor.lua)
    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    Pipeline.GetActive():SetCurrentCamera(cam_node)
    FileUtil.LoadPipelineFromFile(
        Pipeline.GetActive(),
        FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"))
end

local function on_update(dt)
    frame_count = frame_count + 1
    if frame_count >= exit_at then
        Window.Close()
    end
    Pipeline.GetActive():Execute(Scene.GetActive())
end

local function on_shutdown() end

Engine.run({ on_init = on_init, on_update = on_update, on_shutdown = on_shutdown })
