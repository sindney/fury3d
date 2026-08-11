-- tests/lua/terrain_sky_scene.lua - round-trip + physics verification for
-- the outdoor_terrain demo scene (change: add-sky-atmosphere-terrain,
-- task 10.2). Run AFTER Projects/outdoor/setup_terrain_sky_scene.lua.
--
--   ./fury exec Projects/outdoor/outdoor_terrain.bin tests/lua/terrain_sky_scene.lua
--
-- Asserts: Sky + Terrain components present with the authored params; the
-- chunk meshes stay out of the serialized scene; a dynamic crate dropped
-- near the village rests at Terrain:GetHeight (Jolt heightfield agrees
-- with the render/query heights).

local function fail(msg)
    io.stderr:write("terrain_sky_scene FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene (exec must load outdoor_terrain.bin)") end
local root = scene:GetRootNode()

-- components present with authored params
local sky_node = root:FindChildRecursively("Sky")
if not sky_node then fail("Sky node missing") end
local sky = sky_node:GetSkyAtmosphere()
if not sky then fail("SkyAtmosphere missing") end
if sky:GetSunLightName() ~= "DefaultSun" then fail("sun light name lost") end
-- not an exact value: users legitimately scrub TOD in the editor and save
local hours = sky:GetTimeHours()
if hours < 0.0 or hours >= 24.0 then fail("time_hours out of range: " .. hours) end
if not sky:GetCloudsEnabled() then fail("clouds flag lost") end

local terrain_node = root:FindChildRecursively("Terrain")
if not terrain_node then fail("Terrain node missing") end
local terrain = terrain_node:GetTerrain()
if not terrain then fail("Terrain component missing") end
if not terrain:HasHeights() then fail("heights not loaded") end
if terrain:GetChunkCount() ~= 16 then fail("chunk count lost") end

-- chunks rebuilt on load
local container = terrain_node:FindChildRecursively("__terrain_chunks")
if not container then fail("chunk container missing after load") end
if container:GetChildCount() ~= 256 then
    fail("expected 256 chunk nodes, got " .. container:GetChildCount())
end

-- the Grid ground is gone
if root:FindChildRecursively("Grid") then fail("Grid should be removed") end

-- serialization: save as JSON, chunk nodes must not appear
local path = "/tmp/outdoor_terrain_rt.json"
if not FileUtil.SaveFile(scene, path) then fail("save failed") end
local f = io.open(path, "r")
local text = f:read("*a")
f:close()
if text:find("terrain_chunk", 1, true) then
    fail("chunk meshes leaked into the serialized scene")
end
if not text:find('"SkyAtmosphere"', 1, true) then fail("sky missing from save") end
if not text:find('"Terrain"', 1, true) then fail("terrain missing from save") end

-- physics: drop a crate near the village; it must rest on the heightfield
local cube = MeshUtil.CreateCube()
scene:AddMesh(cube)
local mat = Material.Create("terrain_rt_mat")
scene:AddMaterial(mat)

local crate = SceneNode.Create("RtCrate")
crate:SetLocalPosition(Vector4(20.0, 400.0, -380.0, 1.0))
crate:SetLocalScale(40.0)
crate:AddComponent(MeshRender.Create(mat, cube))
local crate_body = BodySetup.Create()
crate_body:SetShapeType(1)
crate_body:SetMotionType(1)
crate_body:SetMass(6.0)
crate:AddComponent(crate_body)
crate_body:AutoFitFromMesh()
root:AddChild(crate)

Physics.SetEnabled(true)
Physics.Step(1)
local terrain_body = terrain_node:GetBodySetup()
if not terrain_body or not terrain_body:HasBody() then
    fail("terrain heightfield body not created")
end

Physics.Step(200) -- 8 s at 25 Hz
local pos = crate:GetWorldPosition()
local ground = terrain:GetHeight(pos.x, pos.z)
print(string.format("terrain_sky_scene: crate rest (%.1f, %.1f, %.1f), ground %.1f",
    pos.x, pos.y, pos.z, ground))
if pos.y < ground + 10.0 or pos.y > ground + 40.0 then
    fail(string.format("crate should rest on terrain (ground %.1f), got %.1f", ground, pos.y))
end

print("terrain_sky_scene: PASS")
