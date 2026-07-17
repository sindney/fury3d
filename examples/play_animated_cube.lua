-- Plays the AnimatedCube glTF sample's rotation clip.
-- Requires glTF-Sample-Assets/Models/AnimatedCube/glTF/AnimatedCube.gltf
-- (fetch separately). Run: ./furye play_animated_cube.lua
local function on_init()
    local octree = OcTree.Create()
    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), octree))

    local path = FileUtil.GetAbsPath("glTF-Sample-Assets/Models/AnimatedCube/glTF/AnimatedCube.gltf")
    local scene = Importer.LoadGltf(path)
    if not scene then
        Editor.Log("error", "play_animated_cube: LoadGltf returned nil")
        Window.Close()
        return
    end
    Scene.SetActive(scene)

    -- AnimatedCube targets the cube node itself (node-level path), so
    -- attach the Animator to the root's first mesh-bearing descendant.
    local function find_mesh_node(node)
        if node:GetMeshRender() then return node end
        for i = 0, node:GetChildCount() - 1 do
            local found = find_mesh_node(node:GetChildAt(i))
            if found then return found end
        end
        return nil
    end
    local mesh_node = find_mesh_node(scene:GetRootNode())
    if not mesh_node then
        Editor.Log("error", "play_animated_cube: no mesh node found")
        Window.Close()
        return
    end

    local anim = Animator.Create()
    mesh_node:AddComponent(anim)
    anim:Play("animation_AnimatedCube")
    Editor.Log("info", "play_animated_cube: playing animation_AnimatedCube")

    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 0.1, 100)
    local cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(Vector4(0, 3, 6, 1))
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
    Pipeline.GetActive():Execute(Scene.GetActive():GetSceneManager())
end

local function on_shutdown() end

Engine.run({ on_init = on_init, on_update = on_update, on_shutdown = on_shutdown })
