-- tests/lua/render_scenario.lua - deterministic render-scenario driver.
--
-- Shared driver for the manifest rendering scenarios. Everything that makes
-- a capture reproducible is pinned here: window resolution, camera pose (or
-- the scene's saved camera), engine clock + ocean wave time every frame,
-- sky time-of-day + cloud coverage, skeletal animation sampled at a fixed
-- time, externally stepped particles, fixed UI overlay values, and semantic
-- content checks that must pass before any pixel is trusted.
--
-- The engine exits through the normal window path after the
-- --screenshot / --screenshot-series capture, so no os.exit is needed
-- on the success path.
--
-- Usage (from examples/, flags are stripped before the Lua arg table):
--   ./fury ../tests/lua/render_scenario.lua <scenario>
--       --rhi=<backend> --screenshot out.png --screenshot-frame 40
--   FURY_SCENARIO_TEMPORAL=1 ... --screenshot-series atlas.png,4,4
--
-- Machine-readable output lines (parsed by tests/render_baseline.py):
--   SCENARIO_SEMANTIC_OK <name>
--   SCENARIO_META name=.. backend=.. size=WxH hdr=.. cam=.. time=.. anim=..

local function fail(msg)
    io.stderr:write("render_scenario FAIL: " .. msg .. "\n")
    os.exit(1)
end

-- ---------------------------------------------------------------- scenarios

local SCENARIOS = {
    feature_hdr_off = {
        scene = "Projects/feature/feature.bin",
        working_dir = "Projects/feature/",
        hdr = false,
        overlay = true,
        camera = "MainCamera",
        fixed_time = 12.5,
        anim_time = 0.8,
        particle_dt = 1.0 / 60.0,
        particle_warmup = { FireEmber = 1.0, SmokePlume = 2.0 },
        semantic = "feature",
    },
    feature_hdr_on = {
        scene = "Projects/feature/feature.bin",
        working_dir = "Projects/feature/",
        hdr = true,
        overlay = true,
        camera = "MainCamera",
        fixed_time = 12.5,
        anim_time = 0.8,
        particle_dt = 1.0 / 60.0,
        particle_warmup = { FireEmber = 1.0, SmokePlume = 2.0 },
        semantic = "feature",
    },
    island = {
        scene = "Projects/ocean/ocean_island.bin",
        working_dir = "Projects/ocean/",
        hdr = nil, -- keep the scene's saved render settings
        overlay = false,
        camera = "MainCamera",
        camera_pose = { 10000.0, 1200.0, 10000.0, 225.0, -12.0 }, -- island and horizon vista
        fixed_time = 12.5,
        tod_hours = 13.0,
        cloud_coverage = 0.5,
        semantic = "island",
    },
}

local name = (arg and arg[1]) or os.getenv("FURY_SCENARIO") or ""
local cfg = SCENARIOS[name]
if not cfg then
    local keys = {}
    for k in pairs(SCENARIOS) do keys[#keys + 1] = k end
    table.sort(keys)
    fail("unknown scenario '" .. name .. "' (have: " .. table.concat(keys, ", ") .. ")")
end

local temporal = os.getenv("FURY_SCENARIO_TEMPORAL") == "1"
local res = os.getenv("FURY_RESOLUTION") or "1280x720"
local res_w, res_h = res:match("^(%d+)x(%d+)$")
res_w, res_h = tonumber(res_w), tonumber(res_h)
if not res_w then fail("bad FURY_RESOLUTION '" .. res .. "'") end

-- ---------------------------------------------------------------- state

local pl = nil
local octree = nil
local scene = nil
local frame = 0
local cam_node = nil
local ocean_ref = nil
local sky_ref = nil
local animators = {}   -- { animator = sample_time }
local particles = {}   -- { system = { dt, steps, steps_per_frame } }
local rs_ref = nil
local meta_printed = false
local anim_dt = 1.0 / 60.0

-- ---------------------------------------------------------------- helpers

local function split_csv(s)
    local out = {}
    for part in string.gmatch(s, "[^,]+") do out[#out + 1] = tonumber(part) end
    return out
end

local function find_node(node, want)
    if node:GetName() == want then return node end
    for i = 0, node:GetChildCount() - 1 do
        local hit = find_node(node:GetChildAt(i), want)
        if hit then return hit end
    end
    return nil
end

local function require_node(root, nodename)
    local node = find_node(root, nodename)
    if not node then fail(name .. ": node missing: " .. nodename) end
    return node
end

-- Pin every animation state to `t` at speed 0 (wall clock frozen, pose re-displayed each frame).
-- Saved states come back disabled; enable so the sample applies.
local function sample_animators(t)
    for animator in pairs(animators) do
        for i = 0, animator:GetStateCount() - 1 do
            local state = animator:GetStateAt(i)
            if state then
                -- SetTime clamps past the clip end; wrap so temporal samples keep moving.
                local len = state:GetLength()
                local tt = t
                if len > 0.0 then tt = t % len end
                state:SetEnabled(true)
                state:SetSpeed(0.0)
                state:SetTime(tt)
            end
        end
    end
end

-- ---------------------------------------------------------------- semantics

local function semantic_feature(root)
    local tanks = require_node(root, "TankGroup")
    local tank_meshes = 0
    for i = 0, tanks:GetChildCount() - 1 do
        if tanks:GetChildAt(i):GetMeshRender() then tank_meshes = tank_meshes + 1 end
    end
    if tank_meshes ~= 6 then fail("expected six tank meshes, got " .. tank_meshes) end

    local lights = {
        Sun = LightType.DIRECTIONAL,
        KeySpot = LightType.SPOT,
        FireLight = LightType.POINT,
    }
    for lname, ltype in pairs(lights) do
        local light = require_node(root, lname):GetLight()
        if not light or light:GetType() ~= ltype then
            fail("light mismatch: " .. lname)
        end
    end

    local renderer_count = 0
    Scene.ForEachNode(scene, function(node)
        local renderer = node:GetParticleRenderer()
        if renderer then
            if not renderer:GetSystem() then
                fail("unresolved particle renderer: " .. node:GetName())
            end
            renderer_count = renderer_count + 1
        end
    end)
    if renderer_count ~= 2 then
        fail("expected two particle renderers, got " .. renderer_count)
    end

    for _, anode in ipairs({ "fox", "JamesNode" }) do
        local animator = require_node(root, anode):GetAnimator()
        if not animator or animator:GetStateCount() < 1 then
            fail("Animator missing: " .. anode)
        end
        animators[animator] = true
    end
    require_node(root, "Campfire")
    require_node(root, "MainCamera"):GetCamera()
end

local function semantic_island(root)
    for _, nodename in ipairs({
        "DefaultSun", "Sky", "Ocean", "Terrain", "MainCamera", "PlayerSpawn",
    }) do
        require_node(root, nodename)
    end
    local terrain = require_node(root, "Terrain"):GetTerrain()
    if not terrain or not terrain:HasHeights() then
        fail("island terrain heights missing")
    end
    local ocean = require_node(root, "Ocean"):GetOceanComponent()
    if not ocean or ocean:GetMode() ~= 1 then fail("infinite ocean missing") end
    if ocean:GetResolvedSource() ~= 1 then
        fail("ocean waves did not resolve: " .. ocean:GetResolvedReason())
    end
    if not require_node(root, "Sky"):GetSkyAtmosphere() then
        fail("Sky atmosphere missing")
    end
    local nonempty = 0
    Scene.ForEachNode(scene, function(node)
        local renderer = node:GetInstancedMeshRender()
        if not renderer then return end
        if renderer:GetInstanceCount() > 0 then nonempty = nonempty + 1 end
    end)
    if nonempty < 1 then fail("nonempty kraut vegetation missing") end
end

-- ---------------------------------------------------------------- init

local function on_init()
    octree = OcTree.Create()
    Scene.SetActive(Scene.Create(name, cfg.working_dir, octree))
    if not Scene.LoadActive(cfg.scene) then
        fail("failed to load " .. cfg.scene)
    end
    scene = Scene.GetActive()
    local root = scene:GetRootNode()
    root:Recompose(true)

    Window.SetResolution(res_w, res_h)

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    pl = Pipeline.GetActive()
    local rs = scene:GetRenderSettings()
    rs_ref = rs
    local pipe_path = rs and rs:GetPipelinePath() or ""
    if pipe_path == "" then pipe_path = "Resource/Pipeline/DefferedLightingPBR.json" end
    FileUtil.LoadPipelineFromFile(pl, FileUtil.GetAbsPath(pipe_path))
    PostProcess.LoadFromDirectory("Resource/PostProcess")
    if rs then
        if cfg.hdr ~= nil then rs:SetHDR(cfg.hdr) end
        Pipeline.ApplyRenderSettings(pl, rs)
        print(string.format("render_scenario: hdr=%s csm=%s pipeline=%s",
            tostring(rs:IsHDR()), tostring(rs:IsCascadedShadowMap()),
            rs:GetPipelinePath()))
    end

    -- sky / ocean pins
    Scene.ForEachNode(scene, function(node)
        local sky = node:GetSkyAtmosphere()
        if sky then sky_ref = sky end
        local ocean = node:GetOceanComponent()
        if ocean then ocean_ref = ocean end
    end)
    if sky_ref then
        sky_ref:SetAutoAdvance(false)
        if cfg.tod_hours then sky_ref:SetTimeHours(cfg.tod_hours) end
        if cfg.cloud_coverage then
            sky_ref:SetCloudCoverage(cfg.cloud_coverage)
        end
    end

    -- camera
    cam_node = require_node(root, cfg.camera)
    if cfg.camera_pose then
        local pose = cfg.camera_pose
        cam_node:SetLocalPosition(Vector4(pose[1], pose[2], pose[3], 1.0))
        cam_node:SetLocalRoattion(
            MathUtil.EulerRadToQuat(math.rad(pose[4]), math.rad(pose[5]), 0.0))
        cam_node:Recompose(false)
        local camera = cam_node:GetCamera()
        if camera then
            camera:PerspectiveFov(0.7854, res_w / res_h, 1.0, 500000.0)
        end
    end
    pl:SetCurrentCamera(cam_node)

    -- animation sample state + particle warmup
    if cfg.semantic == "feature" then semantic_feature(root) end
    if cfg.semantic == "island" then semantic_island(root) end

    if cfg.particle_warmup then
        for pname, warm in pairs(cfg.particle_warmup) do
            local system = scene:GetParticleSystem(pname)
            if not system then fail("particle system missing: " .. pname) end
            system:SetExternallyDriven(true)
            system:SetRandomSeed(pname == "FireEmber" and 11 or 17)
            -- Re-simulated from scratch every frame below; the engine's rand()
            -- is never seeded, so a fixed Update(dt) sequence is reproducible.
            local steps = math.floor(warm / cfg.particle_dt + 0.5)
            particles[system] = {
                dt = cfg.particle_dt,
                steps = steps,
                steps_per_frame = 2,
            }
        end
    end

    -- UI overlay with fixed test values
    if cfg.overlay then
        rmlui:LoadFontFace("Engine/Ui/Fonts/LatoLatin-Regular.ttf")
        rmlui:LoadFontFace("Engine/Ui/Fonts/LatoLatin-Bold.ttf")
        rmlui.contexts["main"]:Update()
        GameUI.LoadDocument("ui/feature_overlay.rml")
        local doc = rmlui.contexts["main"].documents["ui/feature_overlay.rml"]
        if not doc then fail("overlay document did not load") end
        local badge = doc:GetElementById("hdr-badge")
        if badge then
            local on = rs and rs:IsHDR()
            badge.inner_rml = on and "HDR ON" or "HDR OFF"
            badge:SetClass("active", on and true or false)
        end
        local fps = doc:GetElementById("fps")
        if fps then fps.inner_rml = "60" end
        GameUI.Show("ui/feature_overlay.rml")
        rmlui.contexts["main"]:Update()
        rmlui.contexts["main"]:Update()
    end

    print("SCENARIO_SEMANTIC_OK " .. name)
end

-- ---------------------------------------------------------------- update

local function on_update(dt)
    frame = frame + 1

    -- engine clock + wave time stay pinned for the whole run
    Engine.SetTime(cfg.fixed_time)
    if ocean_ref then ocean_ref:SetWaveTime(cfg.fixed_time) end

    -- animation: pinned sample time, or a fixed-dt ramp in temporal mode
    if next(animators) then
        local t = cfg.anim_time or 0.0
        if temporal then t = t + frame * anim_dt end
        sample_animators(t)
    end

    -- particles: re-simulated from a reset state every frame, so the rendered
    -- frame only ever sees this deterministic state (the wall-clock tick lands
    -- after pl:Execute).
    for system, pc in pairs(particles) do
        system:Reset()
        local total = pc.steps
        if temporal then
            total = pc.steps + (frame - 1) * pc.steps_per_frame
        end
        for _ = 1, total do system:Update(pc.dt) end
    end

    pl:Execute(octree)

    if not meta_printed and frame >= 2 then
        local w, h = Window.GetResolution()
        local backend = Engine.GetRhiBackend()
        local alive = 0
        for system in pairs(particles) do
            alive = alive + system:GetAliveCount()
        end
        print(string.format(
            "SCENARIO_META name=%s backend=%s size=%dx%d hdr=%s cam=%s time=%.3f anim=%.3f temporal=%d particles=%d",
            name, backend, w, h, tostring(rs_ref and rs_ref:IsHDR()),
            cfg.camera, cfg.fixed_time, cfg.anim_time or 0.0, temporal and 1 or 0,
            alive))
        meta_printed = true
    end
end

Engine.run({ on_init = on_init, on_update = on_update })
