-- Reproduce the segfault when opening james.json over an existing scene.
-- Mimics Editor.lua's replace_active_scene + pipeline setup.
local function replace_active_scene(new_scene)
    local active = Scene.GetActive()
    Editor.SetSelectedSceneNode(nil)
    active:Clear()
    Importer.MergeInto(active, new_scene)
end

local function on_init()
    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), OcTree.Create()))

    -- load the default tank scene first (the editor's startup)
    local first = Importer.LoadScene(FileUtil.GetAbsPath("Resource/Scene/scene.json"))
    if first then replace_active_scene(first) end

    -- camera + pipeline (matches Editor.lua)
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1, 100)
    camera:SetShadowFar(30)
    camera:SetShadowBounds(Vector4(-5), Vector4(5))
    local cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(Vector4(0, 10, 25, 1))
    cam_node:AddComponent(Transform.Create())
    cam_node:AddComponent(camera)
    cam_node:Recompose(true)
    Scene.GetActive():GetRootNode():AddChild(cam_node)
    Scene.GetActive():GetSceneManager():AddSceneNodeRecursively(cam_node)

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    FileUtil.LoadPipelineFromFile(Pipeline.GetActive(),
        FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"))
    Pipeline.GetActive():SetCurrentCamera(cam_node)

    -- now open james.json — this is where the editor segfaults
    local second = Importer.LoadScene(FileUtil.GetAbsPath("Resource/Scene/james.json"))
    if second then
        replace_active_scene(second)
        print("replaced with james.json OK")
    end
    Window.Close()
end

local function on_update(dt)
    if Pipeline.GetActive() then
        Pipeline.GetActive():Execute(Scene.GetActive():GetSceneManager())
    end
end

Engine.run({ on_init = on_init, on_update = on_update, on_shutdown = function() end })
