-- Plays the first clip of the FBX-converted james skinned mesh.
-- Run: ./furye play_james.lua
local frame = 0

local function find_mesh_node(node)
    if node:GetMeshRender() then return node end
    for i = 0, node:GetChildCount() - 1 do
        local found = find_mesh_node(node:GetChildAt(i))
        if found then return found end
    end
    return nil
end

local anim_state = { clip = nil, anim = nil }

local function on_init()
    local octree = OcTree.Create()
    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), octree))

    -- FBX -> glTF -> Scene. Clips land in the EntityManager.
    local scene = Importer.LoadFbx(FileUtil.GetAbsPath("Resource/Scene/james.fbx"))
    if not scene then
        Editor.Log("error", "play_james: Importer.LoadFbx returned nil")
        Window.Close()
        return
    end
    Scene.SetActive(scene)

    local root = scene:GetRootNode()
    local mesh_node = find_mesh_node(root)
    if not mesh_node then
        Editor.Log("error", "play_james: no mesh node found")
        Window.Close()
        return
    end

    -- Attach an Animator and play the first registered clip.
    local anim = Animator.Create()
    mesh_node:AddComponent(anim)
    anim_state.anim = anim
    Scene.ForEachAnimationClip(scene, function(clip)
        if not anim_state.clip then anim_state.clip = clip:GetName() end
    end)
    if anim_state.clip then
        anim:Play(anim_state.clip)
        Editor.Log("info", "play_james: playing '" .. anim_state.clip .. "'")
    end

    -- Camera positioned to frame the mesh's world AABB.
    local aabb = mesh_node:GetWorldAABB()
    local center = aabb:GetCenter()
    local size = aabb:GetSize()
    local radius = 0.5 * math.sqrt(size.x * size.x + size.y * size.y + size.z * size.z)
    if radius < 0.001 then radius = 5.0 end
    local cam_pos = Vector4(center.x, center.y + radius * 0.3, center.z + radius * 2.0, 1)

    local camera = Camera.Create()
    camera:PerspectiveFov(1.0, 1.778, 0.01, radius * 50.0)
    local cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(cam_pos)
    cam_node:AddComponent(Transform.Create())
    cam_node:AddComponent(camera)
    cam_node:Recompose(true)
    scene:GetRootNode():AddChild(cam_node)
    scene:GetSceneManager():AddSceneNodeRecursively(cam_node)

    Editor.Log("info", string.format("play_james: mesh AABB center=(%.2f,%.2f,%.2f) radius=%.2f",
        center.x, center.y, center.z, radius))

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    FileUtil.LoadPipelineFromFile(
        Pipeline.GetActive(),
        FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"))
    Pipeline.GetActive():SetCurrentCamera(cam_node)
end

local function on_update(dt)
    frame = frame + 1
    Pipeline.GetActive():Execute(Scene.GetActive():GetSceneManager())
end

local function on_shutdown() end

Engine.run({ on_init = on_init, on_update = on_update, on_shutdown = on_shutdown })
