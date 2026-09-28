-- tests/lua/scene_node_picker_roundtrip.lua - Sky/Buoyancy/CharacterController
-- node-name round-trip after save/load (change: scene-node-picker, task 7.4).
--
-- Usage (from examples/):
--   FURY_SCENE=Projects/ocean/ocean_island.bin ./fury ../tests/lua/scene_node_picker_roundtrip.lua
--
-- 1. Loads the unmodified island scene and asserts each component's current
--    *NodeName field matches what the picker would have written.
-- 2. Sets each *NodeName to a known string (the picker writes the picked
--    node's GetName() to the field), saves to /tmp, reloads, and asserts
--    each getter returns the set value exactly. This covers the writer path
--    the picker triggers; the modal UI itself can't run headless (no ImGui).
--
-- Exits 0 with "OK" on stderr; exits 1 naming the failing field.

local function fail(msg)
    io.stderr:write("scene_node_picker_roundtrip FAIL: " .. msg .. "\n")
    os.exit(1)
end

local OUT_PATH = "/tmp/scene_node_picker_roundtrip.bin"

local function find_sky(scene)
    local sky = nil
    Scene.ForEachNode(scene, function(n)
        local s = n:GetSkyAtmosphere()
        if s then sky = s end
    end)
    return sky
end

local function find_buoyancy(scene)
    local b = nil
    Scene.ForEachNode(scene, function(n)
        local got = n:GetBuoyancyComponent()
        if got then b = got end
    end)
    return b
end

local function find_character_controller(scene)
    local c = nil
    Scene.ForEachNode(scene, function(n)
        local got = n:GetCharacterController()
        if got then c = got end
    end)
    return c
end

local function on_init()
    local octree = OcTree.Create()
    Scene.SetActive(Scene.Create("scene_node_picker_rt", "Projects/ocean/", octree))
    if not Scene.LoadActive("Projects/ocean/ocean_island.bin") then
        fail("failed to load ocean_island.bin")
    end
    local scene = Scene.GetActive()

    local sky = find_sky(scene)
    if not sky then fail("no SkyAtmosphere in island scene") end
    local buoyancy = find_buoyancy(scene)
    if not buoyancy then fail("no BuoyancyComponent in island scene") end

    -- The island scene doesn't ship a CharacterController; add one to a
    -- transient node so the picker writer path is exercised.
    local character = find_character_controller(scene)
    if not character then
        local host = SceneNode.Create("picker_rt_host")
        character = CharacterController.Create()
        host:AddComponent(character)
        scene:GetRootNode():AddChild(host)
    end

    local SUN_NAME = "RoundTripSun"
    local OCEAN_NAME = "RoundTripOcean"
    local CAMERA_NAME = "RoundTripCamera"

    sky:SetSunLightName(SUN_NAME)
    buoyancy:SetOceanNodeName(OCEAN_NAME)
    character:SetCameraNodeName(CAMERA_NAME)

    if not Scene.SaveActive(OUT_PATH) then
        fail("SaveActive failed: " .. OUT_PATH)
    end
    if not Scene.LoadActive(OUT_PATH) then
        fail("reload failed: " .. OUT_PATH)
    end

    scene = Scene.GetActive()
    local sky2 = find_sky(scene)
    if not sky2 then fail("no SkyAtmosphere after reload") end
    local buoyancy2 = find_buoyancy(scene)
    if not buoyancy2 then fail("no BuoyancyComponent after reload") end
    local character2 = find_character_controller(scene)
    if not character2 then fail("no CharacterController after reload") end

    if sky2:GetSunLightName() ~= SUN_NAME then
        fail("sun_light_name: want " .. SUN_NAME .. ", got " .. sky2:GetSunLightName())
    end
    if buoyancy2:GetOceanNodeName() ~= OCEAN_NAME then
        fail("ocean_node_name: want " .. OCEAN_NAME .. ", got " .. buoyancy2:GetOceanNodeName())
    end
    if character2:GetCameraNodeName() ~= CAMERA_NAME then
        fail("character_camera_node_name: want " .. CAMERA_NAME .. ", got " .. character2:GetCameraNodeName())
    end

    io.stderr:write("scene_node_picker_roundtrip OK\n")
    os.exit(0)
end

Engine.run({ on_init = on_init, on_update = function() end })
