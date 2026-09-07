-- tests/lua/vegetation_perf.lua - perf sanity for the populated island
-- (change: add-kraut-vegetation, task 9.3): ocean_island with ~2300
-- instanced trees must hold interactive frame rates with flat draw calls.
--
-- Usage: ./fury ../tests/lua/vegetation_perf.lua --screenshot /tmp/perf.png --screenshot-frame 130
--
-- Loads Projects/ocean/ocean_island.bin (same pipeline setup as Player.lua),
-- renders 120 frames after a 10-frame warmup, prints avg frame ms + the
-- frame's draw-call count.

local function fail(msg)
    io.stderr:write("vegetation_perf FAIL: " .. msg .. "\n")
    os.exit(1)
end

local octree = nil
local pl = nil
local frame = 0
local t0 = 0

local function on_init()
    octree = OcTree.Create()
    Scene.SetActive(Scene.Create("perf", "Projects/ocean/", octree))
    if not Scene.LoadActive("Projects/ocean/ocean_island.bin") then
        fail("failed to load ocean_island.bin")
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

    -- camera over the palm grove (same vantage as the validation screenshots)
    local cam_node = SceneNode.Create("PerfCam")
    cam_node:AddComponent(Transform.Create())
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1.0, 500000.0)
    cam_node:SetLocalPosition(Vector4(10000.0, 1200.0, 10000.0, 1.0))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(math.rad(225), math.rad(-12), 0.0))
    cam_node:Recompose(false)
    cam_node:AddComponent(camera)
    scene:GetRootNode():AddChild(cam_node)
    octree:AddSceneNodeRecursively(cam_node)
    pl:SetCurrentCamera(cam_node)
end

local function on_update(dt)
    frame = frame + 1
    if frame == 10 then t0 = os.clock() end
    pl:Execute(octree)
    if frame == 130 then
        local ms = (os.clock() - t0) * 1000.0 / 120.0
        local dc = RenderUtil.Instance():GetDrawCall()
        print(string.format("PERF avg_frame_ms=%.2f draw_calls=%d", ms, dc))
        if dc > 400 then
            fail("draw calls not flat: " .. dc)
        end
        print("vegetation_perf OK")
        os.exit(0) -- headless loop is open-ended otherwise
    end
end

Engine.run({ on_init = on_init, on_update = on_update })
