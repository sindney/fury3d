-- tests/lua/instancing_smoke.lua - headless ISM/HISM instancing spec
-- (change: add-kraut-vegetation, section 6 / task 9.3 prereq).
--
-- Usage:
--   ./fury tests/lua/instancing_smoke.lua --screenshot /tmp/instancing.png --screenshot-frame 8
--
-- Builds a scene procedurally: a cube mesh drawn by one InstancedMeshRender
-- (ISM mode) at 2500 transforms, plus a reference MeshRender cube, a
-- shadow-casting directional light, and a pinned camera. Asserts:
--   * the component reports all instances
--   * draw calls stay flat (instanced, not per-instance): gbuffer issues
--     1 draw per instanced component + shadow 1 + the reference cube's
--     per-mesh draws -- two orders of magnitude below instance count
--   * a screenshot lands (checked by the caller via file existence)

local function fail(msg)
    io.stderr:write("instancing_smoke FAIL: " .. msg .. "\n")
    os.exit(1)
end

local octree = nil
local pl = nil
local imr = nil

local function on_init()
    octree = OcTree.Create()
    local scene = Scene.Create("instancing_smoke", "", octree)
    Scene.SetActive(scene)
    local root = scene:GetRootNode()

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    pl = Pipeline.GetActive()
    local full_pipe = FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingPBR.json")
    if not FileUtil.FileExist(full_pipe) then
        fail("pipeline not found: " .. full_pipe)
    end
    FileUtil.LoadPipelineFromFile(pl, full_pipe)
    PostProcess.LoadFromDirectory("Resource/PostProcess")

    -- Mesh + material (registered with the scene so the component resolves).
    local mat = Material.Create("inst_mat")
    mat:SetUniform("diffuse_color", { 0.85, 0.45, 0.15 })
    mat:SetUniform("roughness_factor", 0.7)
    scene:AddMaterial(mat)
    local mesh = MeshUtil.CreateCube()
    scene:AddMesh(mesh)

    -- Instanced cube field: 50 x 50 grid, 3 m spacing, ISM single-tier mode.
    local inst_node = SceneNode.Create("CubeField")
    imr = InstancedMeshRender.Create(mesh, mat)
    imr:SetHierarchical(false)
    for i = 0, 49 do
        for j = 0, 49 do
            imr:AddInstance(Vector4(i * 300.0, 0.0, j * 300.0, 1.0), 0.0, 200.0)
        end
    end
    inst_node:AddComponent(imr)
    root:AddChild(inst_node)
    print("instances=" .. imr:GetInstanceCount())
    if imr:GetInstanceCount() ~= 2500 then
        fail("expected 2500 instances, got " .. imr:GetInstanceCount())
    end
    if not imr:GetRenderable() then
        fail("component not renderable (mesh/material missing?)")
    end

    -- Reference plain MeshRender cube next to the field (non-instanced path
    -- must be unaffected).
    local ref_node = SceneNode.Create("RefCube")
    ref_node:SetLocalPosition(Vector4(-600.0, 150.0, 0.0, 1.0))
    ref_node:SetLocalScale(200.0)
    ref_node:AddComponent(MeshRender.Create(mat, mesh))
    root:AddChild(ref_node)

    -- Directional light with shadows (exercises the instanced shadow path).
    local sun_node = SceneNode.Create("Sun")
    local sun = Light.Create()
    sun:SetType(0) -- directional
    sun:SetIntensity(3.0)
    sun:SetCastShadows(true)
    sun_node:AddComponent(sun)
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(-0.9, 0.6, 0.0))
    root:AddChild(sun_node)

    -- Pinned camera looking at the field center.
    local cam_node = SceneNode.Create("Camera")
    cam_node:AddComponent(Transform.Create())
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1.0, 500000.0)
    cam_node:SetLocalPosition(Vector4(7500.0, 6000.0, 17000.0, 1.0))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, -0.56, 0.0))
    cam_node:Recompose(false)
    cam_node:AddComponent(camera)
    root:AddChild(cam_node)
    pl:SetCurrentCamera(cam_node)

    -- Procedural nodes are NOT auto-registered with the octree (that's a
    -- scene-load step) -- register the whole tree or the render query sees
    -- nothing.
    octree:AddSceneNodeRecursively(root)

    -- Round-trip through .bin to verify component serialization (task 6.1)
    -- and to let Player.lua render the same content.
    if not Scene.SaveActive("/tmp/instancing_smoke.bin") then
        fail("scene save failed")
    end
    print("scene saved")
end

local frame = 0
local function on_update(dt)
    frame = frame + 1
    pl:Execute(octree)
    if frame == 5 then
        local dc = RenderUtil.Instance():GetDrawCall()
        print("DRAWCALLS=" .. dc)
        -- Flat draw-call count: 2500 instances must NOT produce
        -- per-instance draws. Rough budget: gbuffer 1 (field) + 1 (ref
        -- cube) + CSM shadow ~4 cascades x (1+1) + light/quad/sky/postfx
        -- passes (<40). Per-instance submission would be >2500.
        if dc > 200 then
            fail("draw calls not flat: " .. dc .. " (per-instance submission suspected)")
        end
        print("instancing_smoke OK")
    end
end

Engine.run({
    on_init     = on_init,
    on_update   = on_update,
})
