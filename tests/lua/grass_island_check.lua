-- tests/lua/grass_island_check.lua - render/perf validation for the
-- ocean_island grass layer (change: add-kraut-vegetation, task 10.6).
--
-- Usage:
--   ./fury ../tests/lua/grass_island_check.lua --screenshot out.png --screenshot-frame 60
-- Env: FURY_SCENE=<bin>          scene to load (default ocean_island_grass_test.bin)
--      FURY_CAM=x,y,z,yaw,pitch  camera pose (deg for yaw/pitch)
--      FURY_SUN_DYAW=<deg>       rotate DefaultSun by this much (ToD B)
--      FURY_WIND_TIME=<seconds>  pin wind time

local function fail(msg)
    io.stderr:write("grass_island_check FAIL: " .. msg .. "\n")
    os.exit(1)
end

local octree = nil
local pl = nil
local frame = 0
local t0 = 0

local function split_csv(s)
    local out = {}
    for part in string.gmatch(s, "[^,]+") do out[#out + 1] = tonumber(part) end
    return out
end

local function on_init()
    octree = OcTree.Create()
    Scene.SetActive(Scene.Create("check", "Projects/ocean/", octree))
    local scene_path = os.getenv("FURY_SCENE") or "Projects/ocean/ocean_island.bin"
    if not Scene.LoadActive(scene_path) then
        fail("failed to load " .. scene_path)
    end
    local scene = Scene.GetActive()

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    pl = Pipeline.GetActive()
    local rs = scene:GetRenderSettings()
    local pipe_path = rs and rs:GetPipelinePath() or ""
    if pipe_path == "" then pipe_path = "Resource/Pipeline/DefferedLightingPBR.json" end
    FileUtil.LoadPipelineFromFile(pl, FileUtil.GetAbsPath(pipe_path))
    PostProcess.LoadFromDirectory("Resource/PostProcess")
    if rs then Pipeline.ApplyRenderSettings(pl, rs) end

    -- camera: grove vista by default; FURY_CAM overrides
    local cam_node = SceneNode.Create("CheckCam")
    cam_node:AddComponent(Transform.Create())
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1.0, 500000.0)
    local pose = { 10000.0, 1200.0, 10000.0, 225.0, -12.0 }
    local env_cam = os.getenv("FURY_CAM")
    if env_cam then
        local v = split_csv(env_cam)
        if #v == 5 then pose = v end
    end
    cam_node:SetLocalPosition(Vector4(pose[1], pose[2], pose[3], 1.0))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(math.rad(pose[4]), math.rad(pose[5]), 0.0))
    cam_node:Recompose(false)
    cam_node:AddComponent(camera)
    scene:GetRootNode():AddChild(cam_node)
    pl:SetCurrentCamera(cam_node)

    -- A/B experiment hook: strip two-sided from all instanced materials
    if os.getenv("FURY_TWOSIDED_OFF") == "1" then
        local n = 0
        Scene.ForEachNode(scene, function(node)
            local imr = node:GetInstancedMeshRender()
            if imr then
                for i = 0, imr:GetMaterialCount() - 1 do
                    local m = imr:GetMaterial(i)
                    if m and m:GetTwoSided() then m:SetTwoSided(false); n = n + 1 end
                end
            end
        end)
        print("TWOSIDED off on " .. n .. " materials")
    end

    -- ToD variant: the scene's SkyAtmosphere drives the sun node from its
    -- time-of-day (SunFromTod) -- rotating the sun node directly gets
    -- overwritten every frame. Set the sky's hours instead.
    local tod = os.getenv("FURY_TOD_HOURS")
    if tod then
        Scene.ForEachNode(scene, function(n)
            local sky = n:GetSkyAtmosphere()
            if sky then
                sky:SetTimeHours(tonumber(tod))
                sky:SetAutoAdvance(false)
                print(string.format("SKY time_hours=%s on %s", tod, n:GetName()))
            end
        end)
    end

    octree:AddSceneNodeRecursively(cam_node)

    local wt = os.getenv("FURY_WIND_TIME")
    if wt then Engine.SetTime(tonumber(wt)) end
end

local function on_update(dt)
    frame = frame + 1
    if frame == 10 then t0 = os.clock() end
    pl:Execute(octree)
    if frame == 60 then
        local ms = (os.clock() - t0) * 1000.0 / 50.0
        local dc = RenderUtil.Instance():GetDrawCall()
        print(string.format("PERF avg_frame_ms=%.2f draw_calls=%d", ms, dc))
        Scene.ForEachNode(Scene.GetActive(), function(n)
            if n:GetName() == "GrassField" then
                local imr = n:GetInstancedMeshRender()
                local bc = imr:GetBatchCount()
                local tiers = {}
                for i = 0, bc - 1 do
                    local tier, bb, count = imr:GetBatchInfo(i)
                    tiers[#tiers + 1] = string.format("t%d%s=%d", tier, bb and "b" or "", count)
                end
                print("GRASS batches: " .. table.concat(tiers, " "))
            end
        end)
        print("grass_island_check OK")
        os.exit(0)
    end
end

Engine.run({ on_init = on_init, on_update = on_update })
