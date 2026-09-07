-- tests/lua/pipeline_compile_check.lua - loads both pipeline JSONs and
-- renders one frame of the cube-field scene on each; every shader variant
-- compiles at load, so any [EROR] "compile failed" in the log fails the
-- check (change: add-kraut-vegetation, task 9.2).
--
-- Usage: ./fury ../tests/lua/pipeline_compile_check.lua --screenshot /tmp/pcc.png --screenshot-frame 4

local octree = nil
local pl = nil
local frame = 0

local function on_init()
    octree = OcTree.Create()
    local scene = Scene.Create("pcc", "", octree)
    Scene.SetActive(scene)
    local root = scene:GetRootNode()

    local mat = Material.Create("pcc_mat")
    mat:SetUniform("diffuse_color", { 0.6, 0.7, 0.4 })
    scene:AddMaterial(mat)
    local mesh = MeshUtil.CreateCube()
    scene:AddMesh(mesh)
    local node = SceneNode.Create("Cube")
    node:AddComponent(MeshRender.Create(mat, mesh))
    node:SetLocalScale(200.0)
    node:SetLocalPosition(Vector4(0.0, 100.0, 0.0, 1.0))
    root:AddChild(node)

    local cam_node = SceneNode.Create("Camera")
    cam_node:AddComponent(Transform.Create())
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1.0, 100000.0)
    cam_node:SetLocalPosition(Vector4(0.0, 150.0, 800.0, 1.0))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, -0.15, 0.0))
    cam_node:Recompose(false)
    cam_node:AddComponent(camera)
    root:AddChild(cam_node)

    octree:AddSceneNodeRecursively(root)

    -- pipeline under test: PBR first, Lambert on the second pass
    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    pl = Pipeline.GetActive()
    FileUtil.LoadPipelineFromFile(pl, FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingPBR.json"))
    PostProcess.LoadFromDirectory("Resource/PostProcess")
    pl:SetCurrentCamera(cam_node)
end

local function on_update(dt)
    frame = frame + 1
    pl:Execute(octree)
    if frame == 3 then
        -- swap to the Lambert pipeline (recompiles all its variants)
        FileUtil.LoadPipelineFromFile(pl, FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"))
        print("pipeline_compile_check: swapped to Lambert")
    end
    if frame == 6 then
        print("pipeline_compile_check OK")
    end
end

Engine.run({ on_init = on_init, on_update = on_update })
