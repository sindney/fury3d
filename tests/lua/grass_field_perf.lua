-- tests/lua/grass_field_perf.lua - dense grass scatter perf probe
-- (change: add-kraut-vegetation, task 10.5): N grass clump instances in one
-- HISM InstancedMeshRender; measures avg frame ms (includes the per-instance
-- cull/bucket pass) and draw-call flatness vs N.
--
-- Usage:
--   ./fury ../tests/lua/grass_field_perf.lua [--screenshot out.png --screenshot-frame 60]
-- Env: FURY_GRASS_N=<count> (default 12000), FURY_WIND_TIME pins time.

local function fail(msg)
    io.stderr:write("grass_field_perf FAIL: " .. msg .. "\n")
    os.exit(1)
end

local octree = nil
local pl = nil
local frame = 0
local t0 = 0

local function on_init()
    octree = OcTree.Create()
    local scene = Scene.Create("grass_field", "Resource/Trees/Grass/", octree)
    Scene.SetActive(scene)
    if not Scene.LoadActive("Resource/Trees/Grass/GrassClump.bin") then
        fail("failed to load GrassClump.bin (run tools/gen_grass_assets.py)")
    end
    scene = Scene.GetActive()
    local root = scene:GetRootNode()

    local mesh, materials = nil, {}
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
    if not mesh then fail("no grass mesh found") end

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    pl = Pipeline.GetActive()
    FileUtil.LoadPipelineFromFile(pl, FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingPBR.json"))
    PostProcess.LoadFromDirectory("Resource/PostProcess")

    local sun_node = SceneNode.Create("Sun")
    local sun = Light.Create()
    sun:SetType(0)
    sun:SetIntensity(3.0)
    sun:SetCastShadows(false)
    sun_node:AddComponent(sun)
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(-1.0, 0.5, 0.0))
    root:AddChild(sun_node)

    -- dense field: random disc, deterministic (fixed LCG), radius ~60 m
    local n = tonumber(os.getenv("FURY_GRASS_N") or "12000")
    local inst_node = SceneNode.Create("GrassField")
    inst_node:AddComponent(Transform.Create())
    local imr = InstancedMeshRender.Create(mesh, materials[1])
    for i = 2, #materials do imr:SetMaterial(materials[i], i - 1) end
    imr:SetCastShadows(false)
    local s = 12345
    local function rnd()
        s = (s * 1103515245 + 12345) % 2147483648
        return s / 2147483648.0
    end
    for k = 1, n do
        local r = math.sqrt(rnd()) * 6000.0
        local a = rnd() * math.pi * 2.0
        imr:AddInstance(Vector4(math.cos(a) * r, 0.0, math.sin(a) * r - 3000.0, 1.0),
            rnd() * math.pi * 2.0, 100.0)
    end
    inst_node:AddComponent(imr)
    root:AddChild(inst_node)
    print(string.format("PHASE scatter_done t=%.2f", os.clock()))

    local cam_node = SceneNode.Create("Camera")
    cam_node:AddComponent(Transform.Create())
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1.0, 500000.0)
    cam_node:SetLocalPosition(Vector4(0.0, 180.0, 800.0, 1.0))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, -0.12, 0.0))
    cam_node:Recompose(false)
    cam_node:AddComponent(camera)
    root:AddChild(cam_node)
    pl:SetCurrentCamera(cam_node)

    octree:AddSceneNodeRecursively(sun_node)
    print(string.format("PHASE sun_added t=%.2f", os.clock()))
    octree:AddSceneNodeRecursively(inst_node)
    print(string.format("PHASE wall_added t=%.2f", os.clock()))
    octree:AddSceneNodeRecursively(cam_node)

    local wt = os.getenv("FURY_WIND_TIME")
    if wt then Engine.SetTime(tonumber(wt)) end
    print(string.format("FIELD instances=%d", imr:GetInstanceCount()))
end

local frame = 0
local t0 = 0
local last_t = os.clock()

local function on_update(dt)
    frame = frame + 1
    local ft0 = os.clock()
    if frame == 10 then t0 = os.clock() end
    pl:Execute(octree)
    if frame <= 15 then
        local now = os.clock()
        print(string.format("FRAME %d: %.1f ms (delta %.1f)", frame, (now - ft0) * 1000, (now - last_t) * 1000))
        last_t = now
    end
    if frame == 130 then
        local ms = (os.clock() - t0) * 1000.0 / 120.0
        local dc = RenderUtil.Instance():GetDrawCall()
        print(string.format("PERF avg_frame_ms=%.2f draw_calls=%d", ms, dc))
        print("grass_field_perf OK")
        os.exit(0) -- headless loop is open-ended without --screenshot-frame
    end
end

Engine.run({ on_init = on_init, on_update = on_update })
