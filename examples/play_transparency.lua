-- Validation harness for transparency-and-postfx:
-- loads a saved scene JSON, frames it with a camera + sun, runs the
-- HDR PBR pipeline (the scene's renderSettings seeds HDR + the
-- postprocess chain). Run from examples/:
--   ./furye play_transparency.lua Resource/Scene/GlassVaseFlowers.json --screenshot /tmp/vase.png --screenshot-frame 30
local scene_path = arg[1] or "Resource/Scene/GlassVaseFlowers.json"

local function on_init()
    PostProcess.LoadFromDirectory("Resource/PostProcess")

    local scene = Importer.LoadScene(scene_path)
    if not scene then
        Editor.Log("error", "play_transparency: LoadScene failed for " .. scene_path)
        Window.Close()
        return
    end
    Scene.SetActive(scene)

    -- Sun: the saved scenes have no lights.
    local sun = Light.Create()
    sun:SetType(LightType.DIRECTIONAL)
    sun:SetColor(Color(1.0, 1.0, 1.0, 1.0))
    sun:SetIntensity(1.2)
    sun:SetCastShadows(false)
    sun:CalculateAABB()
    local sun_node = SceneNode.Create("sunNode")
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(math.rad(50.0), math.rad(30.0), 0.0))
    sun_node:AddComponent(Transform.Create())
    sun_node:AddComponent(sun)
    sun_node:Recompose(true)
    scene:GetRootNode():AddChild(sun_node)
    scene:GetSceneManager():AddSceneNodeRecursively(sun_node)

    -- Models import in meters here (~0.16 units tall vase). dist is
    -- arg[2]; near scales with it so small models aren't clipped.
    local dist = tonumber(arg[2] or "0.35")
    local camx = tonumber(arg[3] or "0")
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, dist * 0.01, dist * 100.0)
    local cam_node = SceneNode.Create("camNode")
    cam_node:SetLocalPosition(Vector4(camx, dist * 0.45, dist, 1))
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
        FileUtil.GetAbsPath(arg[4] or "Resource/Pipeline/DefferedLightingPBR.json"))
end

local function on_update(dt)
    Pipeline.GetActive():Execute(Scene.GetActive():GetSceneManager())
end

local function on_shutdown() end

Engine.run({ on_init = on_init, on_update = on_update, on_shutdown = on_shutdown })
