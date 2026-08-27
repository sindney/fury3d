-- setup_ocean_island.lua - derive ocean_island.bin from ocean_base.bin
-- (change: add-fft-ocean, task 9.4). NEVER writes the source. Re-runnable:
-- always starts from ocean_base.bin.
--
--   ./fury exec Projects/ocean/ocean_base.bin Projects/ocean/setup_ocean_island.lua
--
-- Result: Projects/ocean/ocean_island.bin - TerrainIsland heightmap
-- (tools/gen_terrain_assets.py --island 0.7 --height-scale 4000: 18 m peak,
-- beach falling to sea floor at the map edge) with a static heightfield
-- body, the base infinite ocean raised to 400 cm so the island shoreline
-- cuts a foam ring, and a buoyant cube on the beach shallows.

local function fail(msg)
    io.stderr:write("setup_ocean_island FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene (exec must load ocean_base.bin)") end
local root = scene:GetRootNode()

-- -- 1. Terrain (island) ------------------------------------------------------
local terrain_node = SceneNode.Create("Terrain")
local terrain = Terrain.Create()
terrain:SetHeightmapPath("TerrainIsland/height.r16")
terrain:SetSplatmapPath("TerrainIsland/splat.png")
terrain:SetChunkCount(16)
terrain:SetLodCount(4)
terrain:SetLayer(0, "grass", "TerrainIsland/grass.png", 900.0)
terrain:SetLayer(1, "rock",  "TerrainIsland/rock.png",  1200.0)
terrain:SetLayer(2, "mud",   "TerrainIsland/mud.png",   700.0)
terrain:SetLayer(3, "snow",  "TerrainIsland/snow.png",  1000.0)
root:AddChild(terrain_node)
terrain_node:AddComponent(terrain)
if not terrain:HasHeights() then
    fail("TerrainIsland heightmap did not load (run gen_terrain_assets.py --island 0.7 --height-scale 4000 --world-size-x 102400 --world-size-z 102400 --out examples/Projects/ocean/TerrainIsland)")
end

local terrain_body = BodySetup.Create()
terrain_body:SetShapeType(3) -- heightfield, static
terrain_node:AddComponent(terrain_body)

-- -- 2. Ocean: infinite, raised to the beach ----------------------------------
local ocean_node = root:FindChildRecursively("Ocean")
if not ocean_node then fail("ocean_base.bin has no Ocean node") end
local ocean = ocean_node:GetOceanComponent()
if not ocean then fail("Ocean node has no OceanComponent") end

ocean:SetMode(1)                -- Infinite
ocean:SetWaterLevel(400.0)      -- shoreline cuts the beach ~180 m out
ocean:SetShoreFoamDepthCm(250.0) -- 2.5 m foam band on the gentle beach

-- -- 3. A buoyant cube on the shallows (shore foam + buoyancy demo) -----------
local cube_mesh = MeshUtil.CreateCube()
cube_mesh:SetName("ocean_crate_50")
MeshUtil.TransformMesh(cube_mesh, { 50.0, 0, 0, 0, 0, 50.0, 0, 0, 0, 0, 50.0, 0, 0, 0, 0, 1 })
scene:AddMesh(cube_mesh)
local mat = Material.Create("island_crate")
mat:SetUniform("diffuse_color", { 0.85, 0.30, 0.12 })
mat:SetUniform("roughness_factor", 0.3)
mat:SetUniform("metallic_factor", 0.0)
scene:AddMaterial(mat)

-- on the waterline: terrain(19500, 0) = 296 -> ~1 m of water over the sand;
-- spawns 1.5 m up so it splashes, never inside the heightfield
local crate = SceneNode.Create("BeachCrate")
crate:SetLocalPosition(Vector4(19500.0, 550.0, 0.0, 1.0))
crate:AddComponent(MeshRender.Create(mat, cube_mesh))
local crate_body = BodySetup.Create()
crate_body:SetShapeType(1)
crate_body:SetMotionType(1)
crate_body:SetMass(8.0)
crate_body:SetHalfExtents(Vector4(25.0, 25.0, 25.0, 0.0))
crate:AddComponent(crate_body)
local floaty = BuoyancyComponent.Create()
for _, sx in ipairs({ -25.0, 25.0 }) do
    for _, sz in ipairs({ -25.0, 25.0 }) do
        floaty:AddFloatPoint(sx, -25.0, sz, 25.0)
    end
end
floaty:SetWaterDensity(2.5)
floaty:SetLinearDrag(2.0)
floaty:SetAngularDrag(1.5)
floaty:SetRightingStrength(20.0)
floaty:SetOceanNodeName("Ocean")
crate:AddComponent(floaty)
root:AddChild(crate)

-- -- 4. Camera: beach shot toward the island peak -------------------------------
local cam = root:FindChildRecursively("MainCamera")
if cam then
    cam:SetLocalPosition(Vector4(22000.0, 900.0, 0.0, 1.0))
    local dir = Vector4(-1.0, (400.0 - 900.0) / 22000.0, 0.0, 0.0):Normalized()
    local yaw = math.atan(-dir.x, -dir.z)
    local pitch = math.asin(math.max(-1.0, math.min(1.0, dir.y)))
    cam:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
end

root:Recompose(true)

local out = "Projects/ocean/ocean_island.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(out)) then
    fail("save failed: " .. out)
end
print("setup_ocean_island: wrote " .. out)
print("setup_ocean_island: DONE")
