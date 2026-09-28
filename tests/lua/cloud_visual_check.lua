-- tests/lua/cloud_visual_check.lua - volumetric cloud visual check
-- (change: cloud-quality-veg-prepass). Renders the island sky with the
-- volumetric cloud model at deterministic settings for screenshot review.
--
-- Usage (from examples/):
--   FURY_CLOUD_FREEZE=1 ./fury ../tests/lua/cloud_visual_check.lua --screenshot /tmp/clouds.png --screenshot-frame 30
-- Env: FURY_SCENE=<bin>          scene (default ocean_island.bin)
--      FURY_CAM=x,y,z,yaw,pitch  camera pose (deg)
--      FURY_TOD_HOURS=<h>        time of day override (sun via sky)
--      FURY_CLOUD_COVERAGE=<f>   coverage override
--      FURY_CLOUD_DEBUG=<n>      debug view (1 steps, 2 transmittance)

local function fail(msg)
    io.stderr:write("cloud_visual_check FAIL: " .. msg .. "\n")
    os.exit(1)
end

local pl = nil
local octree = nil
local frame = 0
local t0 = 0
local exit_frame = tonumber(os.getenv("FURY_EXIT_FRAME") or "130")
local cam_ref = nil
local base_yaw = 0.0
local base_pitch = 0.0
-- sweep = camera pans every frame (worst case: cloud target re-renders
-- per frame, like real gameplay) and the wall clock starts at spawn
local sweep = os.getenv("FURY_CLOUD_SWEEP") == "1"

local function split_csv(s)
    local out = {}
    for part in string.gmatch(s, "[^,]+") do out[#out + 1] = tonumber(part) end
    return out
end

local function on_init()
    octree = OcTree.Create()
    Scene.SetActive(Scene.Create("cloudcheck", "Projects/ocean/", octree))
    local scene_path = os.getenv("FURY_SCENE") or "Projects/ocean/ocean_island.bin"
    if not Scene.LoadActive(scene_path) then
        fail("failed to load " .. scene_path)
    end
    local scene = Scene.GetActive()

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    pl = Pipeline.GetActive()
    local rs = scene:GetRenderSettings()
    local pipe_path = os.getenv("FURY_PIPELINE") or (rs and rs:GetPipelinePath() or "")
    if pipe_path == "" then pipe_path = "Resource/Pipeline/DefferedLightingPBR.json" end
    FileUtil.LoadPipelineFromFile(pl, FileUtil.GetAbsPath(pipe_path))
    PostProcess.LoadFromDirectory("Resource/PostProcess")
    if rs then Pipeline.ApplyRenderSettings(pl, rs) end

    -- sky + volumetric cloud params
    local sky = nil
    Scene.ForEachNode(scene, function(n)
        local s = n:GetSkyAtmosphere()
        if s then sky = s end
    end)
    if not sky then fail("no SkyAtmosphere in scene") end
    sky:SetCloudsEnabled(os.getenv("FURY_CLOUD_OFF") ~= "1")
    sky:SetCloudAltitudeKm(tonumber(os.getenv("FURY_CLOUD_ALT") or "1.5"))
    sky:SetCloudThicknessKm(tonumber(os.getenv("FURY_CLOUD_THICK") or "2.5"))
    sky:SetCloudFadeKm(tonumber(os.getenv("FURY_CLOUD_FADE") or "20.0"))
    sky:SetCloudDensity(tonumber(os.getenv("FURY_CLOUD_DENSITY") or "14.0"))
    sky:SetCloudScale(tonumber(os.getenv("FURY_CLOUD_SCALE") or "0.15"))
    sky:SetCloudTypeBias(tonumber(os.getenv("FURY_CLOUD_TYPE") or "0.15"))
    sky:SetCloudErosion(tonumber(os.getenv("FURY_CLOUD_EROSION") or "0.5"))
    local cov = tonumber(os.getenv("FURY_CLOUD_COVERAGE") or "0.35")
    sky:SetCloudCoverage(cov)
    local dbg = tonumber(os.getenv("FURY_CLOUD_DEBUG") or "0")
    sky:SetCloudDebugMode(dbg)
    sky:SetCirrusEnabled(os.getenv("FURY_CIRRUS_OFF") ~= "1")
    sky:SetCirrusCoverage(tonumber(os.getenv("FURY_CIRRUS_COVERAGE") or sky:GetCirrusCoverage()))
    sky:SetCirrusAltKm(tonumber(os.getenv("FURY_CIRRUS_ALT") or sky:GetCirrusAltKm()))
    sky:SetCirrusScale(tonumber(os.getenv("FURY_CIRRUS_SCALE") or sky:GetCirrusScale()))
    sky:SetCirrusDensity(tonumber(os.getenv("FURY_CIRRUS_DENSITY") or sky:GetCirrusDensity()))
    sky:SetAutoAdvance(false)
    local tod = os.getenv("FURY_TOD_HOURS")
    if tod then sky:SetTimeHours(tonumber(tod)) end
    -- perf mode: wind scrolls every frame -> the cloud target re-renders
    -- every frame (TOD effectively pinned by the huge day length)
    if os.getenv("FURY_CLOUD_ANIM") == "1" then
        sky:SetAutoAdvance(true)
        sky:SetDayLengthMinutes(100000.0)
    end

    -- the bin's foliage materials predate the Kraut importer's PreZ flag;
    -- flag wind-enabled materials here so FURY_VEG_PREZ A/B tests bite
    Scene.ForEachNode(scene, function(n)
        local imr = n:GetInstancedMeshRender()
        if imr then
            for i = 0, imr:GetMaterialCount() - 1 do
                local m = imr:GetMaterial(i)
                if m and m:GetWindEnabled() and not m:GetPreZ() then
                    m:SetPreZ(true)
                end
            end
        end
    end)

    -- camera: beach-level, looking slightly up at the sky over the sea
    local cam_node = SceneNode.Create("CloudCam")
    cam_node:AddComponent(Transform.Create())
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1.0, 500000.0)
    local pose = { 10000.0, 600.0, 10000.0, 225.0, 8.0 }
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
    octree:AddSceneNodeRecursively(cam_node)
    cam_ref = cam_node
    base_yaw = pose[4]
    base_pitch = pose[5]
end

local function on_update(dt)
    frame = frame + 1
    if sweep and cam_ref then
        local yaw = base_yaw + math.sin(frame * 0.03) * 20.0
        local pitch = base_pitch + math.sin(frame * 0.017) * 8.0
        cam_ref:SetLocalRoattion(MathUtil.EulerRadToQuat(math.rad(yaw), math.rad(pitch), 0.0))
        cam_ref:Recompose(false)
    end
    if frame == 10 then t0 = os.clock() end
    pl:Execute(octree)
    if frame == exit_frame then
        local ms = (os.clock() - t0) * 1000.0 / (exit_frame - 10)
        io.stderr:write(string.format("PERF avg_frame_ms=%.2f\n", ms))
        io.stderr:write("cloud_visual_check OK\n")
        os.exit(0)
    end
end

Engine.run({ on_init = on_init, on_update = on_update })
