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
    feature_lambert = {
        scene = "Projects/feature/feature.bin",
        working_dir = "Projects/feature/",
        hdr = false,
        pipeline_path = "Resource/Pipeline/DeferredLightingLambert.json",
        semantic_pipeline = "lambert",
        overlay = true,
        camera = "MainCamera",
        fixed_time = 12.5,
        anim_time = 0.8,
        particle_dt = 1.0 / 60.0,
        particle_warmup = { FireEmber = 1.0, SmokePlume = 2.0 },
        semantic = "feature",
    },
    clustered_materials_hdr = {
        build = "clustered_materials",
        working_dir = "",
        hdr = true,
        pipeline_path = "Resource/Pipeline/DeferredLightingPBR.json",
        semantic_pipeline = "pbr",
        chain = { "ACES", "FXAA" },
        overlay = false,
        camera = "GridCamera",
        fixed_time = 12.5,
        semantic = "clustered_materials",
    },
    clustered_normal_mapped_hdr = {
        build = "clustered_materials",
        working_dir = "",
        hdr = true,
        pipeline_path = "Resource/Pipeline/DeferredLightingPBR.json",
        semantic_pipeline = "pbr",
        chain = { "ACES", "FXAA" },
        overlay = false,
        camera = "GridCamera",
        fixed_time = 12.5,
        semantic = "clustered_materials",
        normal_mapped = true,
    },
    clustered_overflow_hdr = {
        build = "clustered_materials",
        working_dir = "",
        hdr = true,
        pipeline_path = "Resource/Pipeline/DeferredLightingPBR.json",
        semantic_pipeline = "pbr",
        chain = { "ACES", "FXAA" },
        overlay = false,
        camera = "GridCamera",
        fixed_time = 12.5,
        semantic = "clustered_materials",
        overflow_lights = true,
        overflow_test = true,
    },
    clustered_dense_hdr = {
        build = "clustered_materials",
        working_dir = "",
        hdr = true,
        pipeline_path = "Resource/Pipeline/DeferredLightingPBR.json",
        semantic_pipeline = "pbr",
        chain = { "ACES", "FXAA" },
        overlay = false,
        camera = "GridCamera",
        fixed_time = 12.5,
        semantic = "clustered_materials",
        overflow_lights = true,
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
local clustered_requested = os.getenv("FURY_CLUSTERED") == "1"
local cluster_debug = os.getenv("FURY_CLUSTER_DEBUG") or ""
local camera_drift = os.getenv("FURY_CAMERA_DRIFT") == "1"
local cluster_debug_cycle = os.getenv("FURY_CLUSTER_DEBUG_CYCLE") == "1"
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
local resolved_pipeline = ""
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
    -- SSAO and SSR must be present AND enabled in the resolved chain.
    local chain = rs_ref and rs_ref:GetChain() or {}
    local enabled_effects = {}
    for i = 1, #chain do enabled_effects[chain[i].effectName] = chain[i].enabled end
    for _, effect in ipairs({ "SSAO", "SSR" }) do
        if enabled_effects[effect] == nil then
            fail("island render chain missing " .. effect)
        end
        if enabled_effects[effect] ~= true then
            fail("island render chain has " .. effect .. " present but disabled")
        end
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

-- Resolved-pipeline proof: the on-disk descriptor at the path the scene
-- settings resolve to must carry the markers of the requested pipeline
-- family (and none of the other family's).
local function assert_pipeline_asset(path, kind)
    local fh = io.open(FileUtil.GetAbsPath(path), "rb")
    if not fh then fail("pipeline descriptor unreadable: " .. path) end
    local text = fh:read("*a")
    fh:close()
    local function has(needle) return string.find(text, needle, 1, true) ~= nil end
    if kind == "lambert" then
        if not has("Lambert/PointLight.glsl") or not has('"ldr_composite"') then
            fail("descriptor is not the Lambert pipeline: " .. path)
        end
        if has("PbrCombine.glsl") or has("hdr_composite") then
            fail("descriptor mixes PBR targets into the Lambert pipeline: " .. path)
        end
    elseif kind == "pbr" then
        if not has("PbrCombine.glsl") or not has('"hdr_light"') then
            fail("descriptor is not the PBR pipeline: " .. path)
        end
    end
end

-- ---------------------------------------------------------------- fixture
-- Procedural HDR/PBR grid: 3x3 spheres (metallic 0/0.5/1 x roughness
-- 0.05/0.35/0.8), one emissive sphere, a shadow-catching ground plane, and
-- four local lights (shadowed/unshadowed point + spot). Textureless
-- color-only materials; the current G-buffer has no normal-map path.

local GRID_METALLIC = { 0.0, 0.5, 1.0 }
local GRID_ROUGHNESS = { 0.05, 0.35, 0.8 }
local GRID_SPACING = 500.0
local GRID_SCALE = 260.0 -- unit sphere radius 0.5 -> 130 world units

-- Spot lights stay in their default (0,-1,0) shine direction; the lights
-- are placed directly above their pools, so no aiming rotation is needed.
local fixture_lights = {} -- name -> { node=, light=, want_shadows= }

local function build_clustered_materials(root)
    local sphere = MeshUtil.CreateSphere(32)
    MeshUtil.CalculateTangent(sphere)
    scene:AddMesh(sphere)

    local ground_mat = Material.Create("cm_ground")
    ground_mat:SetUniform("diffuse_color", { 0.5, 0.5, 0.5 })
    ground_mat:SetUniform("metallic_factor", 0.0)
    ground_mat:SetUniform("roughness_factor", 0.9)
    scene:AddMaterial(ground_mat)
    local ground = SceneNode.Create("Ground")
    ground:AddComponent(MeshRender.Create(ground_mat, MeshUtil.CreateCube()))
    ground:SetLocalPosition(Vector4(250.0, -10.0, 0.0, 1.0))
    ground:SetLocalScale(Vector4(4400.0, 20.0, 2400.0, 1.0))
    root:AddChild(ground)

    for mi, metallic in ipairs(GRID_METALLIC) do
        for ri, roughness in ipairs(GRID_ROUGHNESS) do
            local label = string.format("m%.1f_r%.2f", metallic, roughness)
            local mat = Material.Create("cm_" .. label)
            mat:SetUniform("diffuse_color", { 0.8, 0.8, 0.8 })
            mat:SetUniform("metallic_factor", metallic)
            mat:SetUniform("roughness_factor", roughness)
            scene:AddMaterial(mat)
            local node = SceneNode.Create("Sphere_" .. label)
            node:AddComponent(MeshRender.Create(mat, sphere))
            node:SetLocalPosition(Vector4(
                250.0 + (mi - 2) * GRID_SPACING, GRID_SCALE * 0.5,
                (ri - 2) * GRID_SPACING, 1.0))
            node:SetLocalScale(GRID_SCALE)
            root:AddChild(node)
        end
    end

    -- Emissive input: the opaque deferred writers have no dedicated
    -- emissive channel, so self-illumination renders through the
    -- ambient_color/ambient_factor inputs; the emissive_* slots are set
    -- and verified on the material as the authored emissive input.
    local emat = Material.Create("cm_emissive")
    emat:SetUniform("diffuse_color", { 0.0, 0.0, 0.0 })
    emat:SetUniform("ambient_color", { 1.0, 0.45, 0.1 })
    emat:SetUniform("ambient_factor", 1.0)
    emat:SetUniform("emissive_color", { 1.0, 0.45, 0.1 })
    emat:SetUniform("emissive_factor", 1.0)
    emat:SetUniform("metallic_factor", 0.0)
    emat:SetUniform("roughness_factor", 0.35)
    scene:AddMaterial(emat)
    local enode = SceneNode.Create("Sphere_emissive")
    enode:AddComponent(MeshRender.Create(emat, sphere))
    enode:SetLocalPosition(Vector4(1000.0, GRID_SCALE * 0.5, 0.0, 1.0))
    enode:SetLocalScale(GRID_SCALE)
    root:AddChild(enode)

    if cfg.normal_mapped then
        local nmat = Material.Create("cm_normal_mapped")
        nmat:SetUniform("diffuse_color", { 0.8, 0.8, 0.8 })
        nmat:SetUniform("metallic_factor", 0.5)
        nmat:SetUniform("roughness_factor", 0.2)
        nmat:SetTexture("diffuse_texture", "Projects/feature/unspecified.png")
        nmat:SetTexture("normal_texture", "Projects/feature/normal_wave.png")
        nmat:SetTextureSRGB("normal_texture", false)
        scene:AddMaterial(nmat)
        local nnode = SceneNode.Create("Sphere_normal_mapped")
        nnode:AddComponent(MeshRender.Create(nmat, sphere))
        nnode:SetLocalPosition(Vector4(-750.0, GRID_SCALE * 0.5, 800.0, 1.0))
        nnode:SetLocalScale(GRID_SCALE)
        root:AddChild(nnode)
    end

    local function add_light(lname, ltype, pos, color, intensity, radius, cast)
        local node = SceneNode.Create(lname)
        local light = Light.Create()
        light:SetType(ltype)
        light:SetColor(Color(color[1], color[2], color[3], 1.0))
        light:SetIntensity(intensity)
        light:SetRadius(radius)
        light:SetCastShadows(cast)
        node:AddComponent(light)
        node:SetLocalPosition(Vector4(pos[1], pos[2], pos[3], 1.0))
        root:AddChild(node)
        fixture_lights[lname] = { light = light, cast = cast }
    end

    add_light("PointShadow", LightType.POINT, { -800.0, 1000.0, -800.0 },
        { 1.0, 1.0, 1.0 }, 4.0, 3000.0, true)
    add_light("PointNoShadow", LightType.POINT, { 1300.0, 900.0, 900.0 },
        { 1.0, 1.0, 1.0 }, 4.0, 3000.0, false)
    add_light("SpotShadow", LightType.SPOT, { 0.0, 1400.0, -250.0 },
        { 1.0, 1.0, 1.0 }, 6.0, 3500.0, true)
    add_light("SpotNoShadow", LightType.SPOT, { 500.0, 1400.0, 250.0 },
        { 1.0, 1.0, 1.0 }, 6.0, 3500.0, false)

    if cfg.overflow_lights then
        for i = 1, 130 do
            add_light("OverflowLight_" .. i, LightType.POINT, { 250.0, 1000.0, 250.0 },
                { 1.0, 1.0, 1.0 }, 0.1, 3000.0, false)
        end
    end

    -- Identity rotation: a camera node looks down its -Z axis.
    local cam = SceneNode.Create("GridCamera")
    local camera = Camera.Create()
    cam:AddComponent(camera)
    cam:SetLocalPosition(Vector4(250.0, 500.0, 2400.0, 1.0))
    cam:Recompose(false)
    camera:PerspectiveFov(0.7854, res_w / res_h, 1.0, 500000.0)
    root:AddChild(cam)
end

local function expect_uniform(mat, key, want)
    local got = mat:GetUniform(key)
    if got == nil then
        fail("material " .. mat:GetName() .. " missing uniform " .. key)
    end
    if type(want) == "table" then
        if type(got) ~= "table" or #got ~= #want then
            fail("material " .. mat:GetName() .. " uniform " .. key .. " shape mismatch")
        end
        for i = 1, #want do
            if math.abs(got[i] - want[i]) > 1e-4 then
                fail(string.format("material %s uniform %s[%d]=%f want %f",
                    mat:GetName(), key, i, got[i], want[i]))
            end
        end
    else
        if type(got) ~= "number" or math.abs(got - want) > 1e-4 then
            fail(string.format("material %s uniform %s=%s want %f",
                mat:GetName(), key, tostring(got), want))
        end
    end
end

local function semantic_clustered_materials(root)
    for mi, metallic in ipairs(GRID_METALLIC) do
        for ri, roughness in ipairs(GRID_ROUGHNESS) do
            local label = string.format("m%.1f_r%.2f", metallic, roughness)
            local render = require_node(root, "Sphere_" .. label):GetMeshRender()
            if not render then fail("grid sphere missing MeshRender: " .. label) end
            local mat = render:GetMaterial(0)
            if not mat then fail("grid sphere missing material: " .. label) end
            if mat:GetName() ~= "cm_" .. label then
                fail("grid sphere material mismatch: " .. tostring(mat:GetName()))
            end
            -- Textureless by construction: proves no normal-map path is claimed.
            if mat:GetTextureCount() ~= 0 then
                fail("grid material unexpectedly textured: " .. label)
            end
            expect_uniform(mat, "metallic_factor", metallic)
            expect_uniform(mat, "roughness_factor", roughness)
        end
    end

    local erender = require_node(root, "Sphere_emissive"):GetMeshRender()
    local emat = erender and erender:GetMaterial(0)
    if not emat then fail("emissive sphere material missing") end
    expect_uniform(emat, "emissive_color", { 1.0, 0.45, 0.1 })
    expect_uniform(emat, "emissive_factor", 1.0)
    expect_uniform(emat, "ambient_color", { 1.0, 0.45, 0.1 })

    if cfg.normal_mapped then
        local nrender = require_node(root, "Sphere_normal_mapped"):GetMeshRender()
        local nmat = nrender and nrender:GetMaterial(0)
        if not nmat then fail("normal-mapped material missing") end
        if not nmat:GetTexture("diffuse_texture") then fail("normal-mapped diffuse texture missing") end
        if not nmat:GetTexture("normal_texture") then fail("normal texture missing") end
        if nmat:GetTextureSRGB("normal_texture") then fail("normal texture must be linear") end
    end

    if not require_node(root, "Ground"):GetMeshRender() then
        fail("ground plane missing MeshRender")
    end

    local want_lights = {
        PointShadow = { type = LightType.POINT, cast = true },
        PointNoShadow = { type = LightType.POINT, cast = false },
        SpotShadow = { type = LightType.SPOT, cast = true },
        SpotNoShadow = { type = LightType.SPOT, cast = false },
    }
    for lname, want in pairs(want_lights) do
        local light = require_node(root, lname):GetLight()
        if not light then fail("light missing: " .. lname) end
        if light:GetType() ~= want.type then fail("light type mismatch: " .. lname) end
        if light:GetCastShadows() ~= want.cast then
            fail("light cast_shadows mismatch: " .. lname)
        end
        if light:GetIntensity() <= 0.0 then fail("light intensity zero: " .. lname) end
        if light:GetRadius() < 1000.0 then fail("light radius too small: " .. lname) end
        local c = light:GetColor()
        if c.r <= 0.0 and c.g <= 0.0 and c.b <= 0.0 then
            fail("light color black: " .. lname)
        end
    end

    if cfg.overflow_lights then
        local light = require_node(root, "OverflowLight_130"):GetLight()
        if not light or light:GetType() ~= LightType.POINT then
            fail("overflow light fixture missing")
        end
    end

    -- HDR chain must carry the tone map and AA resolve.
    local chain = rs_ref and rs_ref:GetChain() or {}
    local enabled_effects = {}
    for i = 1, #chain do enabled_effects[chain[i].effectName] = chain[i].enabled end
    for _, effect in ipairs({ "ACES", "FXAA" }) do
        if enabled_effects[effect] ~= true then
            fail("materials chain missing enabled " .. effect)
        end
    end

    require_node(root, "GridCamera"):GetCamera()
end

-- ---------------------------------------------------------------- init

local function on_init()
    octree = OcTree.Create()
    Scene.SetActive(Scene.Create(name, cfg.working_dir, octree))
    if cfg.build then
        scene = Scene.GetActive()
        build_clustered_materials(scene:GetRootNode())
    else
        if not Scene.LoadActive(cfg.scene) then
            fail("failed to load " .. cfg.scene)
        end
        scene = Scene.GetActive()
    end
    local root = scene:GetRootNode()
    root:Recompose(true)
    if cfg.build then octree:AddSceneNodeRecursively(root) end

    Window.SetResolution(res_w, res_h)

    Pipeline.SetActive(DeferredPipeline.Create("pipeline"))
    pl = Pipeline.GetActive()
    local rs = scene:GetRenderSettings()
    rs_ref = rs
    if rs and cfg.pipeline_path then rs:SetPipelinePath(cfg.pipeline_path) end
    if rs and cfg.chain then
        rs:ClearChain()
        for _, effect in ipairs(cfg.chain) do rs:AddEffect(effect, true) end
    end
    local pipe_path = rs and rs:GetPipelinePath() or ""
    if pipe_path == "" then pipe_path = "Resource/Pipeline/DeferredLightingPBR.json" end
    FileUtil.LoadPipelineFromFile(pl, FileUtil.GetAbsPath(pipe_path))
    PostProcess.LoadFromDirectory("Resource/PostProcess")
    if rs then
        if cfg.hdr ~= nil then rs:SetHDR(cfg.hdr) end
        rs:SetClusteredLighting(clustered_requested)
        Pipeline.ApplyRenderSettings(pl, rs)
        print(string.format("render_scenario: hdr=%s csm=%s clustered=%s pipeline=%s",
            tostring(rs:IsHDR()), tostring(rs:IsCascadedShadowMap()),
            tostring(rs:IsClusteredLighting()), rs:GetPipelinePath()))
    end
    if cfg.semantic_pipeline then
        if not rs then fail(name .. ": scene has no render settings") end
        if rs:GetPipelinePath() ~= cfg.pipeline_path then
            fail(name .. ": resolved pipeline " .. rs:GetPipelinePath()
                .. " ~= requested " .. cfg.pipeline_path)
        end
        assert_pipeline_asset(cfg.pipeline_path, cfg.semantic_pipeline)
        resolved_pipeline = cfg.pipeline_path
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
    local camera_pos = os.getenv("FURY_CAMERA_POS")
    if camera_pos then
        local p = split_csv(camera_pos)
        if #p ~= 3 then fail("FURY_CAMERA_POS expects x,y,z") end
        cam_node:SetLocalPosition(Vector4(p[1], p[2], p[3], 1.0))
        cam_node:Recompose(false)
    end
    local near_override = tonumber(os.getenv("FURY_CAMERA_NEAR"))
    local far_override = tonumber(os.getenv("FURY_CAMERA_FAR"))
    if near_override or far_override then
        local camera = cam_node:GetCamera()
        camera:PerspectiveFov(camera:GetFov(), res_w / res_h,
            near_override or camera:GetNear(), far_override or camera:GetFar())
    end

    -- animation sample state + particle warmup
    if cfg.semantic == "feature" then semantic_feature(root) end
    if cfg.semantic == "island" then semantic_island(root) end
    if cfg.semantic == "clustered_materials" then semantic_clustered_materials(root) end

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

    if camera_drift then
        local p = cam_node:GetLocalPosition()
        cam_node:SetLocalPosition(Vector4(p.x + 1.0, p.y, p.z, 1.0))
        cam_node:Recompose(false)
    end
    pl:SetSwitch(12, cluster_debug == "grid" or (cluster_debug_cycle and frame >= 10 and frame < 20))
    pl:SetSwitch(13, cluster_debug == "count" or cfg.overflow_test == true
        or (cluster_debug_cycle and frame >= 20 and frame < 30))
    pl:Execute(octree)

    if cfg.overflow_test and frame >= 6 then
        if clustered_requested then
            if not Pipeline.IsClusteredLightingEffective(pl) then
                fail("overflow scenario did not activate clustered lighting")
            end
            local max_count = Pipeline.GetClusterMaxLightCount(pl)
            local overflow = Pipeline.GetClusterOverflowCount(pl)
            if max_count ~= 128 or overflow <= 0 then
                fail(string.format("overflow saturation mismatch: max=%d overflow=%d", max_count, overflow))
            end
            print(string.format("CLUSTER_OVERFLOW_OK max=%d overflow=%d", max_count, overflow))
        else
            print("CLUSTER_FALLBACK_OK")
        end
        Window.Close()
    end

    if not meta_printed and frame >= 8 then
        local w, h = Window.GetResolution()
        local backend = Engine.GetRhiBackend()
        local alive = 0
        for system in pairs(particles) do
            alive = alive + system:GetAliveCount()
        end
        print(string.format(
            "SCENARIO_META name=%s backend=%s size=%dx%d hdr=%s cam=%s time=%.3f anim=%.3f temporal=%d particles=%d pipeline=%s clustered=%d clustered_effective=%d",
            name, backend, w, h, tostring(rs_ref and rs_ref:IsHDR()),
            cfg.camera, cfg.fixed_time, cfg.anim_time or 0.0, temporal and 1 or 0,
            alive, resolved_pipeline, clustered_requested and 1 or 0,
            Pipeline.IsClusteredLightingEffective(pl) and 1 or 0))
        meta_printed = true
    end
end

Engine.run({ on_init = on_init, on_update = on_update })
