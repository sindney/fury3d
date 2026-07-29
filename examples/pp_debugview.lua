-- pp debug-view harness: loads a scene with an explicit chain, then
-- enables a buffer debug view (SSAO/SSR) via the pipeline switch.
--   ./furye pp_debugview.lua <scene> <chain> <view:ssao|ssr|none> [pipeline] [dist] --screenshot /tmp/x.png --screenshot-frame 60 --focus
local scene_path = arg[1] or "Projects/tank/scene-hdr.json"
local chain_spec = arg[2] or "SSAO,SSR"
local view = arg[3] or "ssao"
local pipeline_path = arg[4] or "Resource/Pipeline/DefferedLightingPBR.json"
local dist = tonumber(arg[5] or "25")
local hdr = pipeline_path:find("PBR") ~= nil or pipeline_path:find("pbr") ~= nil

local cam_node, camera

local function on_init()
    PostProcess.LoadFromDirectory("Resource/PostProcess")
    local scene = Importer.LoadScene(scene_path)
    if not scene then
        print("pp_debugview: LoadScene failed for " .. scene_path)
        Window.Close()
        return
    end
    Scene.SetActive(scene)

    local rs = scene:GetRenderSettings()
    rs:SetPipelinePath(pipeline_path)
    rs:SetHDR(hdr)
    rs:ClearChain()
    for name in string.gmatch(chain_spec, "[^,]+") do
        rs:AddEffect(name, true)
    end

    camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, dist * 0.01, dist * 100.0)
    cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(Vector4(0, dist * 0.45, dist, 1))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(math.rad(-20.0), 0.0, 0.0))
    cam_node:AddComponent(Transform.Create())
    cam_node:AddComponent(camera)
    cam_node:Recompose(true)
    scene:GetRootNode():AddChild(cam_node)
    scene:GetSceneManager():AddSceneNodeRecursively(cam_node)

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
    FileUtil.LoadPipelineFromFile(Pipeline.GetActive(), FileUtil.GetAbsPath(pipeline_path))
    Pipeline.ApplyRenderSettings(Pipeline.GetActive(), rs)
    print("pp_debugview: scene=" .. scene_path .. " chain=[" .. chain_spec .. "] view=" .. view)
end

local function on_update(dt)
    -- The viewport toolbar re-applies its (false) view switches every
    -- frame during Gui::Render; setting ours right before Execute wins
    -- the frame that matters.
    local p = Pipeline.GetActive()
    p:SetSwitch(7, view == "ssao")  -- SSAO_VIEW
    p:SetSwitch(8, view == "ssr")   -- SSR_VIEW
    p:Execute(Scene.GetActive():GetSceneManager())
end

local function on_shutdown() end

Engine.run({ on_init = on_init, on_update = on_update, on_shutdown = on_shutdown })
