-- tests/lua/lod_transition_check.lua - dithered LOD transition validation
-- (change: add-kraut-vegetation, task 10.2)
--
-- Builds an arc of InstancedMeshRender palms all at the SAME distance from
-- the camera (env FURY_WALL_DIST, cm), so every instance shares the exact
-- same screen coverage: with LOD jitter ON (default) neighbors straddle a
-- threshold boundary and swap tiers at different distances (dithered
-- transition); FURY_LOD_JITTER=0 restores lockstep swapping for A/B.
--
-- Usage:
--   ./fury tests/lua/lod_transition_check.lua --screenshot out.png --screenshot-frame 8
-- Env: FURY_WALL_DIST=<cm> (default 3000)
--      FURY_LOD_DEBUG=1  -> LOD_DEBUG_COLORS (tier palette per instance)
--      FURY_LOD_JITTER=0 -> disable the dither band (A/B baseline)

local function fail(msg)
    io.stderr:write("lod_transition_check FAIL: " .. msg .. "\n")
    os.exit(1)
end

local octree = nil
local pl = nil

local function on_init()
    octree = OcTree.Create()
    local scene = Scene.Create("lod_transition", "Resource/Trees/PalmTree2/", octree)
    Scene.SetActive(scene)
    if not Scene.LoadActive("Resource/Trees/PalmTree2/PalmTree2.bin") then
        fail("failed to load PalmTree2.bin")
    end
    scene = Scene.GetActive()
    local root = scene:GetRootNode()

    -- grab the palm mesh + materials, then park the source tree underground
    local mesh = nil
    local materials = {}
    Scene.ForEachNode(scene, function(n)
        local mr = n:GetMeshRender()
        if mr and mr:GetMesh() and not mesh then
            mesh = mr:GetMesh()
            for i = 0, mr:GetMaterialCount() - 1 do
                materials[#materials + 1] = mr:GetMaterial(i)
            end
        end
        if mr then n:SetLocalPosition(Vector4(0.0, -100000.0, 0.0, 1.0)) end
    end)
    if not mesh then fail("no palm mesh found") end

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    pl = Pipeline.GetActive()
    FileUtil.LoadPipelineFromFile(pl, FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingPBR.json"))
    PostProcess.LoadFromDirectory("Resource/PostProcess")

    -- rigid trees: sway would add noise to the frame-diff metric
    scene:GetRenderSettings():SetWindParams(Vector4(1.0, 0.0, 0.0, 1.0))

    local sun_node = SceneNode.Create("Sun")
    local sun = Light.Create()
    sun:SetType(0)
    sun:SetIntensity(3.0)
    sun:SetCastShadows(true)
    sun_node:AddComponent(sun)
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(-1.0, 0.5, 0.0))
    root:AddChild(sun_node)

    -- line of palms perpendicular to the view axis: identical view-z ->
    -- identical coverage for every instance (the coverage metric reads
    -- view-space depth, so an arc would smear coverage across the arc).
    -- instance scale 100: the kraut mesh is meter-scale (the fragment's
    -- root node normally carries the m->cm conversion)
    local dist = tonumber(os.getenv("FURY_WALL_DIST") or "3000")
    local inst_node = SceneNode.Create("PalmWall")
    inst_node:AddComponent(Transform.Create())
    local imr = InstancedMeshRender.Create(mesh, materials[1])
    for i = 2, #materials do imr:SetMaterial(materials[i], i - 1) end
    for k = 0, 12 do
        -- yaw 0: a rotated mesh AABB inflates the axis-aligned world box
        -- and perturbs coverage; keep the wall uniform so only the jitter
        -- hash (position-keyed) separates instances
        imr:AddInstance(Vector4((k - 6) * 600.0, 0.0, -dist, 1.0), 0.0, 100.0)
    end
    inst_node:AddComponent(imr)
    root:AddChild(inst_node)

    local cam_node = SceneNode.Create("Camera")
    cam_node:AddComponent(Transform.Create())
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1.0, 500000.0)
    cam_node:SetLocalPosition(Vector4(0.0, 250.0, 0.0, 1.0))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, 0.0, 0.0))
    cam_node:Recompose(false)
    cam_node:AddComponent(camera)
    root:AddChild(cam_node)
    pl:SetCurrentCamera(cam_node)

    octree:AddSceneNodeRecursively(sun_node)
    octree:AddSceneNodeRecursively(inst_node)
    octree:AddSceneNodeRecursively(cam_node)

    if os.getenv("FURY_LOD_DEBUG") == "1" then
        pl:SetSwitch(5, true) -- PipelineSwitch::LOD_DEBUG_COLORS
    end
    Engine.SetTime(0.0)
end

local frame = 0
local function on_update(dt)
    pl:Execute(octree)
    frame = frame + 1
    if frame == 8 then
        Scene.ForEachNode(Scene.GetActive(), function(n)
            if n:GetName() == "PalmWall" then
                local imr = n:GetInstancedMeshRender()
                local bc = imr:GetBatchCount()
                print(string.format("WALL instances=%d renderable=%s batches=%d drawcalls=%d",
                    imr:GetInstanceCount(), tostring(imr:GetRenderable()), bc,
                    RenderUtil.Instance():GetDrawCall()))
                for i = 0, bc - 1 do
                    local tier, bb, count = imr:GetBatchInfo(i)
                    print(string.format("  batch %d: tier=%d billboard=%s instances=%d",
                        i, tier, tostring(bb), count))
                end
            end
        end)
    end
end

Engine.run({ on_init = on_init, on_update = on_update })
