-- Demo.lua — Lua port of the former examples/Demo.cpp.
-- Loaded by the `fury` executable. Reproduces the same scene, camera,
-- and pipeline setup as the previous C++ demo.

local octree = nil
local cam_node = nil

local function on_init()
    octree = OcTree.Create(
        Vector4(-1000, -1000, -1000, 1),
        Vector4( 1000,  1000,  1000, 1),
        2)

    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), octree))
    FileUtil.LoadSceneFromCompressedFile(
        Scene.GetActive(),
        FileUtil.GetAbsPath("Resource/Scene/scene.bin"))

    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1, 100)
    camera:SetShadowFar(30)
    camera:SetShadowBounds(Vector4(-5), Vector4(5))

    cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(Vector4(0.0, 10.0, 25.0, 1.0))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, -MathUtil.DegToRad * 30.0, 0.0))
    cam_node:Recompose(false)
    cam_node:AddComponent(Transform.Create())
    cam_node:AddComponent(camera)
    cam_node:Recompose(true)

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    Pipeline.GetActive():SetCurrentCamera(cam_node)
    FileUtil.LoadPipelineFromFile(
        Pipeline.GetActive(),
        FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"))
end

local function on_update(dt)
    Gui.ShowDefault(dt)
    Gui.Render()
    Pipeline.GetActive():Execute(octree)
end

local function on_shutdown()
    octree = nil
    cam_node = nil
end

Engine.run({
    on_init     = on_init,
    on_update   = on_update,
    on_shutdown = on_shutdown,
})

-- Example: cap to 60 FPS and bump UI scale a little
-- Engine.run({
--     on_init     = on_init,
--     on_update   = on_update,
--     on_shutdown = on_shutdown,
-- }, {
--     max_fps        = 60,
--     gui_scale      = 1.25,
--     gui_font_scale = 1.25,
-- })
