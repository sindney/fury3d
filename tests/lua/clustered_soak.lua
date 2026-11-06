local function fail(msg)
    error("clustered_soak FAIL: " .. msg)
end

local function find_node(node, want)
    if node:GetName() == want then return node end
    for i = 0, node:GetChildCount() - 1 do
        local hit = find_node(node:GetChildAt(i), want)
        if hit then return hit end
    end
    return nil
end

local scene_path = "Projects/feature/feature.bin"
local octree = OcTree.Create()
Scene.SetActive(Scene.Create("clustered_soak", "Projects/feature/", octree))
if not Scene.LoadActive(scene_path) then fail("initial scene load") end
local scene = Scene.GetActive()
local rs = scene:GetRenderSettings()
rs:SetPipelinePath("Resource/Pipeline/DeferredLightingPBR.json")
rs:SetHDR(true)
rs:SetClusteredLighting(false)

local pl = DeferredPipeline.Create("pipeline")
Pipeline.SetActive(pl)
pl = Pipeline.GetActive()
FileUtil.LoadPipelineFromFile(pl, FileUtil.GetAbsPath(rs:GetPipelinePath()))
PostProcess.LoadFromDirectory("Resource/PostProcess")
Pipeline.ApplyRenderSettings(pl, rs)

local cam_node = find_node(scene:GetRootNode(), "MainCamera")
if not cam_node then fail("MainCamera missing") end
pl:SetCurrentCamera(cam_node)

local frame = 0
local reloads = 0
local toggles = 0
local resizes = 0

local function on_update()
    frame = frame + 1
    if frame % 10 == 0 then
        rs:SetClusteredLighting(not rs:IsClusteredLighting())
        Pipeline.ApplyRenderSettings(pl, rs)
        toggles = toggles + 1
    end
    if frame % 5 == 0 then
        local sizes = { { 960, 540 }, { 1280, 720 }, { 1600, 900 } }
        local size = sizes[(resizes % #sizes) + 1]
        Window.SetResolution(size[1], size[2])
        local camera = cam_node:GetCamera()
        camera:PerspectiveFov(camera:GetFov(), size[1] / size[2],
            frame % 2 == 0 and 0.5 or camera:GetNear(), frame % 2 == 0 and 5000.0 or camera:GetFar())
        resizes = resizes + 1
    end
    if frame % 30 == 0 then
        local requested = rs:IsClusteredLighting()
        Scene.GetActive():Clear()
        if not Scene.LoadActive(scene_path) then fail("scene reload " .. frame) end
        scene = Scene.GetActive()
        rs = scene:GetRenderSettings()
        rs:SetClusteredLighting(requested)
        cam_node = find_node(scene:GetRootNode(), "MainCamera")
        if not cam_node then fail("camera missing after reload") end
        pl:SetCurrentCamera(cam_node)
        Pipeline.ApplyRenderSettings(pl, rs)
        reloads = reloads + 1
    end
    pl:Execute(octree)
    if frame >= 120 then
        print(string.format("CLUSTERED_SOAK_OK toggles=%d reloads=%d resizes=%d", toggles, reloads, resizes))
        Window.Close()
    end
end

Engine.run({ on_update = on_update })
