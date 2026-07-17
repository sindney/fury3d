-- Cycles the Fox glTF sample's Survey/Walk/Run clips with CrossFade.
-- Requires glTF-Sample-Assets/Models/Fox/glTF/Fox.gltf (fetch separately).
-- Run: ./furye play_fox.lua
local clips = { "Survey", "Walk", "Run" }
local idx = 1
local timer = 0.0
local anim

local function find_mesh_node(node)
    if node:GetMeshRender() then return node end
    for i = 0, node:GetChildCount() - 1 do
        local found = find_mesh_node(node:GetChildAt(i))
        if found then return found end
    end
    return nil
end

local function on_init()
    local octree = OcTree.Create()
    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), octree))

    local path = FileUtil.GetAbsPath("glTF-Sample-Assets/Models/Fox/glTF/Fox.gltf")
    local scene = Importer.LoadGltf(path)
    if not scene then
        Editor.Log("error", "play_fox: LoadGltf returned nil")
        Window.Close()
        return
    end
    Scene.SetActive(scene)

    local mesh_node = find_mesh_node(scene:GetRootNode())
    if not mesh_node then
        Editor.Log("error", "play_fox: no mesh node found")
        Window.Close()
        return
    end

    anim = Animator.Create()
    mesh_node:AddComponent(anim)
    anim:Play(clips[1])
    Editor.Log("info", "play_fox: starting with '" .. clips[1] .. "'")

    -- The Fox sample has no lights; add a directional sun so the
    -- deferred-Lambert pipeline has something to shade with.
    local sun = Light.Create()
    sun:SetType(LightType.DIRECTIONAL)
    sun:SetColor(Color(1.0, 1.0, 1.0, 1.0))
    sun:SetIntensity(0.6)
    sun:SetCastShadows(false)
    sun:CalculateAABB()
    local sun_node = SceneNode.Create("sunNode")
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(math.rad(45.0), math.rad(165.0), 0.0))
    sun_node:AddComponent(Transform.Create())
    sun_node:AddComponent(sun)
    sun_node:Recompose(true)
    scene:GetRootNode():AddChild(sun_node)
    scene:GetSceneManager():AddSceneNodeRecursively(sun_node)

    local camera = Camera.Create()
    -- Engine unit = 1 cm. The Fox is ~155 cm long; frame it from
    -- back-above with a cm-scale near/far.
    camera:PerspectiveFov(0.7854, 1.778, 1, 5000)
    local cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(Vector4(0, 400, 1500, 1))
    cam_node:AddComponent(Transform.Create())
    cam_node:AddComponent(camera)
    cam_node:Recompose(true)
    scene:GetRootNode():AddChild(cam_node)
    scene:GetSceneManager():AddSceneNodeRecursively(cam_node)

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    Pipeline.GetActive():SetCurrentCamera(cam_node)
    FileUtil.LoadPipelineFromFile(
        Pipeline.GetActive(),
        FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"))
end

local function on_update(dt)
    timer = timer + dt
    if timer >= 3.0 and anim then
        timer = 0.0
        idx = (idx % #clips) + 1
        anim:CrossFade(clips[idx], 0.3)
        Editor.Log("info", "play_fox: crossfading to '" .. clips[idx] .. "'")
    end
    Pipeline.GetActive():Execute(Scene.GetActive():GetSceneManager())
end

local function on_shutdown() end

Engine.run({ on_init = on_init, on_update = on_update, on_shutdown = on_shutdown })
