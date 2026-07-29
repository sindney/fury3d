-- pp chain A/B harness (task 9.7): loads a scene, forces HDR + an explicit
-- postprocess chain from argv, renders headless screenshots.
--   ./fury  pp_matrix.lua <scene> <chain> [pipeline] [dist] --screenshot /tmp/x.png --screenshot-frame 30 --focus
--   ./furye pp_matrix.lua <scene> <chain> [pipeline] [dist] --screenshot /tmp/x.png --screenshot-frame 30 --focus
-- chain = comma-separated effect names, e.g. "ACES" / "FXAA,ACES" / "" (none)
local scene_path = arg[1] or "Projects/tank/scene-hdr.json"
local chain_spec = arg[2] or "ACES"
local pipeline_path = arg[3] or "Resource/Pipeline/DefferedLightingPBR.json"
local dist = tonumber(arg[4] or "25")
local csm = arg[5] ~= "nocsm"  -- arg[5]="nocsm" disables cascaded shadow maps
-- HDR is inferred from pipeline name (Lambert=LDR, PBR=HDR). The
-- launcher strips --flags from argv before Lua sees it, so trying to
-- squeeze another positional arg here races with --screenshot/--focus.
local hdr = pipeline_path:find("PBR") ~= nil or pipeline_path:find("pbr") ~= nil

local function on_init()
    PostProcess.LoadFromDirectory("Resource/PostProcess")

    local scene = Importer.LoadScene(scene_path)
    if not scene then
        print("pp_matrix: LoadScene failed for " .. scene_path)
        Window.Close()
        return
    end
    Scene.SetActive(scene)

    -- Force HDR + explicit chain so scene files can't drift between runs.
    local rs = scene:GetRenderSettings()
    rs:SetPipelinePath(pipeline_path)
    rs:SetHDR(hdr)
    rs:SetCascadedShadowMap(csm)
    rs:ClearChain()
    for name in string.gmatch(chain_spec, "[^,]+") do
        rs:AddEffect(name, true)
    end
    print("pp_matrix: scene=" .. scene_path .. " chain=[" .. chain_spec .. "] pipeline=" .. pipeline_path .. " csm=" .. tostring(csm))

    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, dist * 0.01, dist * 100.0)
    local cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(Vector4(0, dist * 0.45, dist, 1))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(math.rad(-20.0), 0.0, 0.0))
    cam_node:AddComponent(Transform.Create())
    cam_node:AddComponent(camera)
    cam_node:Recompose(true)
    scene:GetRootNode():AddChild(cam_node)
    scene:GetSceneManager():AddSceneNodeRecursively(cam_node)

    -- --focus: reframe on the scene's combined bounds.
    if Launcher and Launcher.GetFlag and Launcher.GetFlag("auto_focus") then
        local bmin, bmax = scene:ComputeWorldAABB()
        if bmin and bmax then
            local center = (bmin + bmax) * 0.5
            local size = bmax - bmin
            local radius = math.max(size.x, size.y, size.z) * 0.5
            local distance = math.max(radius / math.tan(0.7854 * 0.5) * 1.1, 0.1)
            camera:PerspectiveFov(0.7854, 1.778, math.max(distance * 0.001, 0.01), distance * 4.0)
            local dl = math.sqrt(2.36)
            local eye = Vector4(center.x + distance / dl, center.y + 0.6 * distance / dl, center.z + distance / dl, 1.0)
            local dx, dy, dz = eye.x - center.x, eye.y - center.y, eye.z - center.z
            local horiz = math.sqrt(dx * dx + dz * dz)
            local fyaw = (horiz < 1e-6) and 0.0 or math.atan(dx, dz)
            local fpitch = math.atan(-dy, horiz)
            cam_node:SetLocalPosition(eye)
            cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(fyaw, fpitch, 0.0))
            cam_node:Recompose(true)
        end
    end

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    Pipeline.GetActive():SetCurrentCamera(cam_node)
    FileUtil.LoadPipelineFromFile(
        Pipeline.GetActive(),
        FileUtil.GetAbsPath(pipeline_path))

    -- Push HDR + chain to the active pipeline AFTER loading the pipeline
    -- entity. Without this, renderSettings changes never reach the pipeline
    -- and every chain variant produces the same screenshot (no chain at all).
    -- Note: chain ORDER is engine-owned (ApplyRenderSettings sorts by
    -- stage/order/name) and tonemapping follows the HDR flag — in HDR an
    -- ACES entry is auto-injected even when chain_spec omits it, and in
    -- LDR any ACES entry is stripped. So e.g. "ACES,FXAA" and "FXAA" in
    -- HDR both resolve to the same runtime chain [FXAA, ACES].
    Pipeline.ApplyRenderSettings(Pipeline.GetActive(), rs)
end

local function on_update(dt)
    Pipeline.GetActive():Execute(Scene.GetActive():GetSceneManager())
end

local function on_shutdown() end

Engine.run({ on_init = on_init, on_update = on_update, on_shutdown = on_shutdown })
