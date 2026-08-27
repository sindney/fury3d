-- setup_ocean_lake.lua - derive ocean_lake.bin from ocean_base.bin
-- (change: add-fft-ocean, task 9.3). NEVER writes the source. Re-runnable:
-- always starts from ocean_base.bin.
--
--   ./fury exec Projects/ocean/ocean_base.bin Projects/ocean/setup_ocean_lake.lua
--
-- Result: Projects/ocean/ocean_lake.bin - TerrainLake basin heightmap
-- (tools/gen_terrain_assets.py --seed 12 --flatten-radius 15000: flat bed at
-- 2795 cm, banks 3200-3700 cm) with a static heightfield body, and the base
-- scene's ocean switched to FINITE mode at 3095 cm (3 m deep at center).

local function fail(msg)
    io.stderr:write("setup_ocean_lake FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene (exec must load ocean_base.bin)") end
local root = scene:GetRootNode()

-- -- 1. Terrain (basin) -----------------------------------------------------
local terrain_node = SceneNode.Create("Terrain")
local terrain = Terrain.Create()
terrain:SetHeightmapPath("TerrainLake/height.r16")
terrain:SetSplatmapPath("TerrainLake/splat.png")
terrain:SetChunkCount(16)
terrain:SetLodCount(4)
terrain:SetLayer(0, "grass", "TerrainLake/grass.png", 900.0)
terrain:SetLayer(1, "rock",  "TerrainLake/rock.png",  1200.0)
terrain:SetLayer(2, "mud",   "TerrainLake/mud.png",   700.0)
terrain:SetLayer(3, "snow",  "TerrainLake/snow.png",  1000.0)
root:AddChild(terrain_node)
terrain_node:AddComponent(terrain) -- OnAttaching builds chunks headlessly
if not terrain:HasHeights() then
    fail("TerrainLake heightmap did not load (run gen_terrain_assets.py --seed 12 --flatten-radius 15000 --out examples/Projects/ocean/TerrainLake)")
end

local terrain_body = BodySetup.Create()
terrain_body:SetShapeType(3) -- heightfield, static
terrain_node:AddComponent(terrain_body)

-- -- 2. Ocean: finite grid in the basin --------------------------------------
local ocean_node = root:FindChildRecursively("Ocean")
if not ocean_node then fail("ocean_base.bin has no Ocean node") end
local ocean = ocean_node:GetOceanComponent()
if not ocean then fail("Ocean node has no OceanComponent") end

ocean:SetMode(0)             -- Finite
ocean:SetWaterLevel(3095.0)  -- bed 2795 + 300 cm of water
ocean:SetFiniteSizeCm(40000.0) -- 400 m grid, covers the basin
ocean:SetFiniteResolution(192)
ocean:SetDebugView(0)

-- -- 3. Camera: bank viewpoint over the lake ----------------------------------
local cam = root:FindChildRecursively("MainCamera")
if cam then
    cam:SetLocalPosition(Vector4(0.0, 3500.0, 11000.0, 1.0))
    -- look at the lake center (0, 3095, 0)
    local dir = Vector4(0.0, 3095.0 - 3500.0, -11000.0, 0.0):Normalized()
    local yaw = math.atan(-dir.x, -dir.z)
    local pitch = math.asin(math.max(-1.0, math.min(1.0, dir.y)))
    cam:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
end

root:Recompose(true)

local out = "Projects/ocean/ocean_lake.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(out)) then
    fail("save failed: " .. out)
end
print("setup_ocean_lake: wrote " .. out)
print("setup_ocean_lake: DONE")
