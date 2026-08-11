-- Player.lua - runtime play-mode bootstrap (change: add-jolt-physics-play-mode).
--
-- Usage:
--   ./fury Player.lua <scene.bin>          (what the editor's Play button spawns)
--   ./fury Player.lua Projects/outdoor/outdoor_physics.bin
--
-- Contract (play-mode spec):
--   * load the scene named by arg[1]
--   * apply its renderSettings to a fresh pipeline (HDR/CSM/postfx chain)
--   * activate the first enabled PlayerController - its bound camera renders
--   * no enabled controller -> spawn a free-fly camera framed on the scene
--   * no editor camera, no Editor.* calls (they no-op outside furye anyway)
--   * Escape closes the window (ends the play session)

local octree = nil

local function on_init()
    octree = OcTree.Create()

    local scene_path = arg and arg[1]
    if not scene_path or scene_path == "" then
        io.stderr:write("Player.lua: expected scene path as arg[1]\n")
        Window.Close()
        return
    end

    -- The scene's working dir is where bare texture paths ("fire.png")
    -- resolve against. It must be the scene file's own directory:
    -- textures upload at scene-load time via Scene::Path.
    local scene_dir = scene_path:match("^(.*[/\\])") or ""
    Scene.SetActive(Scene.Create("play", scene_dir, octree))

    if not Scene.LoadActive(scene_path) then
        io.stderr:write("Player.lua: failed to load scene " .. scene_path .. "\n")
        Window.Close()
        return
    end

    local scene = Scene.GetActive()

    -- Pipeline from the scene's renderSettings (fall back to the default
    -- LDR pipeline when the scene doesn't name one).
    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    local pl = Pipeline.GetActive()
    local rs = scene:GetRenderSettings()
    local pipe_path = rs and rs:GetPipelinePath() or ""
    if pipe_path == "" then
        pipe_path = "Resource/Pipeline/DefferedLightingLambert.json"
    end
    local full_pipe = FileUtil.GetAbsPath(pipe_path)
    if FileUtil.FileExist(full_pipe) then
        FileUtil.LoadPipelineFromFile(pl, full_pipe)
    else
        io.stderr:write("Player.lua: pipeline not found: " .. full_pipe .. "\n")
    end

    PostProcess.LoadFromDirectory("Resource/PostProcess")
    if rs then
        Pipeline.ApplyRenderSettings(pl, rs)
    end

    -- FURY_TOD=hours: override the scene sky's time-of-day (screenshot
    -- matrix hook; no-op on scenes without a Sky node).
    local tod_env = os.getenv("FURY_TOD")
    if tod_env then
        local sky_node = scene:GetRootNode():FindChildRecursively("Sky")
        local sky = sky_node and sky_node:GetSkyAtmosphere()
        if sky then sky:SetTimeHours(tonumber(tod_env)) end
    end

    -- First enabled controller wins; its bound camera becomes the render
    -- camera (Activate does Pipeline.SetCurrentCamera).
    -- Headless verify hook: FURY_CAM="px,py,pz,yawDeg,pitchDeg" skips
    -- controller activation and pins a free-fly camera at the pose
    -- (mirrors Editor.lua's hook; the character boom would otherwise
    -- overwrite a pinned camera every frame).
    local cam_env = os.getenv("FURY_CAM")
    local controller = nil
    if not cam_env then
        controller = PlayerController.ActivateFirst(scene:GetRootNode())
    end

    if not controller then
        -- Free-fly fallback: every scene is navigable in play mode.
        if not cam_env then
            print("Player.lua: no enabled PlayerController - spawning free-fly camera")
        end
        local cam_node = SceneNode.Create("PlayFlyCamera")
        cam_node:AddComponent(Transform.Create())
        local camera = Camera.Create()

        local eye = Vector4(0.0, 170.0, 400.0, 1.0)
        local yaw, pitch = 0.0, -0.4
        local far = 5000.0

        if cam_env then
            local px, py, pz, yawd, pitchd = cam_env:match("^([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)$")
            if px then
                eye = Vector4(tonumber(px), tonumber(py), tonumber(pz), 1.0)
                yaw = math.rad(tonumber(yawd))
                pitch = math.rad(tonumber(pitchd))
            else
                print("Player.lua FURY_CAM: could not parse '" .. cam_env .. "' (want px,py,pz,yawDeg,pitchDeg)")
            end
        else
            local mn, mx = scene:ComputeWorldAABB()
            if mn then
                local center = (mn + mx) * 0.5
                center.w = 1.0
                local size = mx - mn
                local radius = math.max(size.x, math.max(size.y, size.z)) * 0.5
                if radius < 1.0 then radius = 1.0 end
                far = math.max(5000.0, radius * 6.0)
                eye = center + Vector4(radius * 0.6, radius * 0.45, radius * 1.1, 0.0)
                local dir = (center - eye):Normalized()
                yaw = math.atan(-dir.x, -dir.z)
                pitch = math.asin(math.max(-1.0, math.min(1.0, dir.y)))
            end
        end

        camera:PerspectiveFov(0.7854, 1.778, 1.0, far)
        cam_node:SetLocalPosition(eye)
        cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
        cam_node:Recompose(false)
        cam_node:AddComponent(camera)
        scene:GetRootNode():AddChild(cam_node)

        if cam_env then
            -- Pinned verification camera: no controller, so the scene's
            -- character controller can't reclaim the render camera.
            Pipeline.GetActive():SetCurrentCamera(cam_node)
        else
            cam_node:AddComponent(FreeFlyController.Create())
            PlayerController.ActivateFirst(scene:GetRootNode())
        end
    end
end

local function on_update(dt)
    if InputUtil.Instance():GetKeyDown(Key.Escape) then
        Window.Close()
        return
    end
    Pipeline.GetActive():Execute(octree)
end

Engine.run({
    on_init     = on_init,
    on_update   = on_update,
})
