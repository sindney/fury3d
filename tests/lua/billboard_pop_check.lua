-- tests/lua/billboard_pop_check.lua - dolly across the mesh->billboard
-- boundary while --screenshot-series captures a frame series (the contact
-- sheet shows whether the swap pops). PalmTree2 at fixed world position,
-- camera dollies away along -Z.
--
-- Usage:
--   ./fury ../tests/lua/billboard_pop_check.lua [tree.bin] --screenshot-series "out.png,8,8" --screenshot-frame 10
-- Env: FURY_DOLLY_FROM / FURY_DOLLY_TO (cm; default 2500 -> 9000 crosses the
--      old ~38 m and new ~78 m boundaries), FURY_WIND_TIME pins wind.

local function fail(msg)
    io.stderr:write("billboard_pop_check FAIL: " .. msg .. "\n")
    os.exit(1)
end

local tree_path = arg and arg[1] or "Resource/Trees/PalmTree2/PalmTree2.bin"
local octree = nil
local pl = nil
local cam_node = nil
local tree_center = nil
local dolly_from = tonumber(os.getenv("FURY_DOLLY_FROM") or "2500")
local dolly_to = tonumber(os.getenv("FURY_DOLLY_TO") or "9000")
local frame = 0

local function on_init()
    octree = OcTree.Create()
    local bin_dir = tree_path:match("^(.*[/\\])") or ""
    local scene = Scene.Create("pop", bin_dir, octree)
    Scene.SetActive(scene)
    if not Scene.LoadActive(tree_path) then fail("failed to load " .. tree_path) end
    scene = Scene.GetActive()
    local root = scene:GetRootNode()

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    pl = Pipeline.GetActive()
    FileUtil.LoadPipelineFromFile(pl, FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingPBR.json"))
    PostProcess.LoadFromDirectory("Resource/PostProcess")

    local sun_node = SceneNode.Create("Sun")
    local sun = Light.Create()
    sun:SetType(0)
    sun:SetIntensity(3.0)
    sun:SetCastShadows(true)
    sun_node:AddComponent(sun)
    -- front-lit: sun over the camera's shoulder so mesh and billboard
    -- reads are both visible (the pop check compares brightness jumps)
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, tonumber(os.getenv("FURY_SUN_PITCH") or "0.3"), 0.0))
    root:AddChild(sun_node)

    local mn, mx = scene:ComputeWorldAABB()
    local center = Vector4(0.0, 250.0, 0.0, 1.0)
    if mn then center = (mn + mx) * 0.5 end

    cam_node = SceneNode.Create("Cam")
    cam_node:AddComponent(Transform.Create())
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1.0, 500000.0)
    cam_node:AddComponent(camera)
    root:AddChild(cam_node)
    pl:SetCurrentCamera(cam_node)
    tree_center = center

    octree:AddSceneNodeRecursively(sun_node)
    octree:AddSceneNodeRecursively(cam_node)

    local wt = os.getenv("FURY_WIND_TIME")
    if wt then Engine.SetTime(tonumber(wt)) end
end

local function on_update(dt)
    -- dolly 0..1 over frames 10..90 (80 frames of motion)
    local t = math.min(1.0, math.max(0.0, (frame - 10) / 120.0))
    local dist = dolly_from + (dolly_to - dolly_from) * t
    cam_node:SetLocalPosition(Vector4(tree_center.x, tree_center.y + dist * 0.2, tree_center.z + dist, 1.0))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, -math.atan(dist * 0.2, dist), 0.0))
    cam_node:Recompose(false)
    pl:Execute(octree)
    frame = frame + 1
end

Engine.run({ on_init = on_init, on_update = on_update })
