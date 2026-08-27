-- tests/lua/ocean_scene_roundtrip.lua - scene save/load round-trip for
-- OceanComponent + BuoyancyComponent (change: add-fft-ocean, task 10.1;
-- doubles as the editor 8.3 round-trip verification - the inspector writes
-- the same fields through the same setters).
--
--   ./fury exec Projects/outdoor/outdoor_water.bin tests/lua/ocean_scene_roundtrip.lua
-- (the exec scene arg is required by the CLI but unused - the script builds
-- its own scene; run from examples/ so the mini fixture resolves)
--
-- Every ocean/buoyancy field is set non-default, saved, reloaded, asserted.
-- No physics re-simulation after reload (see buoyancy_smoke.lua for why).

local function fail(msg)
    io.stderr:write("ocean_scene_roundtrip FAIL: " .. msg .. "\n")
    os.exit(1)
end

local function near(a, b, eps, what)
    if math.abs(a - b) > (eps or 0.001) then
        fail(string.format("%s: want %s, got %s", what, tostring(b), tostring(a)))
    end
end

local scene = Scene.Create("ocean_rt", "", OcTree.Create())
Scene.SetActive(scene)
local root = scene:GetRootNode()

-- -- ocean with every field non-default --------------------------------------
local ocean_node = SceneNode.Create("Ocean")
local ocean = OceanComponent.Create()
ocean:SetMode(0) -- Finite (default is Infinite)
ocean:SetWaveSource(1) -- Baked
ocean:SetWaveAssetPath("../tests/lua/fixtures/ocean_mini/ocean.json")
ocean:SetSeed(42)
ocean:SetWindSpeed(1234.0)
ocean:SetWindDirectionDeg(210.0)
ocean:SetFetchCm(250000.0)
ocean:SetChoppiness(1.7)
ocean:SetSwellResolution(64)
ocean:SetRippleResolution(32)
ocean:SetSwellTileCm(5000.0)
ocean:SetRippleTileCm(400.0)
ocean:SetFrameCount(16)
ocean:SetLoopSeconds(8.0)
ocean:SetWaterLevel(-120.0)
ocean:SetFiniteSizeCm(4000.0)
ocean:SetFiniteResolution(96)
ocean:SetRingCellSizeCm(150.0)
ocean:SetRingCells(48)
ocean:SetRingCount(4)
ocean:SetSkirtRadiusCm(250000.0)
ocean:SetAbsorbColor(Color(0.1, 0.2, 0.3, 1.0))
ocean:SetScatterColor(Color(0.3, 0.2, 0.1, 1.0))
ocean:SetRoughness(0.33)
ocean:SetNormalStrength(1.8)
ocean:SetFoamAmount(2.2)
ocean:SetShoreFoamDepthCm(750.0)
ocean:SetSsrEnabled(false)
ocean:SetDebugView(2)
ocean_node:AddComponent(ocean)
root:AddChild(ocean_node)

-- -- buoyant prop with every field non-default ------------------------------
local mat = Material.Create("rt_mat")
scene:AddMaterial(mat)
local cube_mesh = MeshUtil.CreateCube()
scene:AddMesh(cube_mesh)

local prop = SceneNode.Create("Prop")
prop:AddComponent(MeshRender.Create(mat, cube_mesh))
local body = BodySetup.Create()
body:SetShapeType(1)
body:SetMotionType(1)
body:SetMass(7.5)
prop:AddComponent(body)
local floaty = BuoyancyComponent.Create()
floaty:AddFloatPoint(-30.0, -25.0, -30.0, 30.0)
floaty:AddFloatPoint(30.0, -25.0, 30.0, 35.0)
floaty:SetWaterDensity(2.4)
floaty:SetLinearDrag(1.9)
floaty:SetAngularDrag(1.3)
floaty:SetRightingStrength(7.0)
floaty:SetOceanNodeName("Ocean")
floaty:SetDebugDraw(true)
prop:AddComponent(floaty)
root:AddChild(prop)

-- -- save + reload -----------------------------------------------------------
local path = "/tmp/ocean_scene_roundtrip.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(path)) then
    fail("save failed")
end
if not Scene.LoadActive(FileUtil.GetAbsPath(path)) then
    fail("reload failed")
end
local scene2 = Scene.GetActive()
local root2 = scene2:GetRootNode()

local ocean2_node = root2:FindChildRecursively("Ocean")
if not ocean2_node then fail("Ocean node missing after reload") end
local o = ocean2_node:GetOceanComponent()
if not o then fail("OceanComponent missing after reload") end

if o:GetMode() ~= 0 then fail("mode lost") end
if o:GetWaveSource() ~= 1 then fail("waveSource lost") end
if o:GetWaveAssetPath() ~= "../tests/lua/fixtures/ocean_mini/ocean.json" then fail("waveAsset lost") end
if o:GetSeed() ~= 42 then fail("seed lost") end
near(o:GetWindSpeed(), 1234.0, 0.01, "windSpeed")
near(o:GetWindDirectionDeg(), 210.0, 0.01, "windDirectionDeg")
near(o:GetFetchCm(), 250000.0, 1.0, "fetchCm")
near(o:GetChoppiness(), 1.7, 0.001, "choppiness")
if o:GetSwellResolution() ~= 64 then fail("swellResolution lost") end
if o:GetRippleResolution() ~= 32 then fail("rippleResolution lost") end
near(o:GetSwellTileCm(), 5000.0, 0.1, "swellTileCm")
near(o:GetRippleTileCm(), 400.0, 0.1, "rippleTileCm")
if o:GetFrameCount() ~= 16 then fail("frameCount lost") end
near(o:GetLoopSeconds(), 8.0, 0.001, "loopSeconds")
near(o:GetWaterLevel(), -120.0, 0.001, "waterLevel")
near(o:GetFiniteSizeCm(), 4000.0, 0.1, "finiteSizeCm")
if o:GetFiniteResolution() ~= 96 then fail("finiteResolution lost") end
near(o:GetRingCellSizeCm(), 150.0, 0.01, "ringCellSizeCm")
if o:GetRingCells() ~= 48 then fail("ringCells lost") end
if o:GetRingCount() ~= 4 then fail("ringCount lost") end
near(o:GetSkirtRadiusCm(), 250000.0, 1.0, "skirtRadiusCm")
do
    local c = o:GetAbsorbColor()
    near(c.r, 0.1, 0.001, "absorb.r")
    near(c.g, 0.2, 0.001, "absorb.g")
    near(c.b, 0.3, 0.001, "absorb.b")
    local s = o:GetScatterColor()
    near(s.r, 0.3, 0.001, "scatter.r")
    near(s.g, 0.2, 0.001, "scatter.g")
    near(s.b, 0.1, 0.001, "scatter.b")
end
near(o:GetRoughness(), 0.33, 0.001, "roughness")
near(o:GetNormalStrength(), 1.8, 0.001, "normalStrength")
near(o:GetFoamAmount(), 2.2, 0.001, "foamAmount")
near(o:GetShoreFoamDepthCm(), 750.0, 0.01, "shoreFoamDepthCm")
if o:GetSsrEnabled() ~= false then fail("ssr lost") end
if o:GetDebugView() ~= 2 then fail("debugView lost") end

-- The wave asset re-resolves under the new working dir via the EntityManager
-- cache of the still-referenced first scene (or flat-fallback otherwise) -
-- either way the component must stay alive and answer queries.
local _ = o:WaveHeightAtWorld(0.0, 0.0)

local prop2 = root2:FindChildRecursively("Prop")
if not prop2 then fail("Prop node missing after reload") end
local b = prop2:GetBuoyancyComponent()
if not b then fail("BuoyancyComponent missing after reload") end
if b:GetFloatPointCount() ~= 2 then fail("float point count lost") end
do
    local p0 = b:GetFloatPointOffset(0)
    near(p0.x, -30.0, 0.001, "point0.x")
    near(p0.y, -25.0, 0.001, "point0.y")
    near(p0.z, -30.0, 0.001, "point0.z")
    near(b:GetFloatPointRadius(0), 30.0, 0.001, "point0.radius")
    near(b:GetFloatPointRadius(1), 35.0, 0.001, "point1.radius")
end
near(b:GetWaterDensity(), 2.4, 0.001, "waterDensity")
near(b:GetLinearDrag(), 1.9, 0.001, "linearDrag")
near(b:GetAngularDrag(), 1.3, 0.001, "angularDrag")
near(b:GetRightingStrength(), 7.0, 0.001, "rightingStrength")
if b:GetOceanNodeName() ~= "Ocean" then fail("oceanNode lost") end
if b:GetDebugDraw() ~= true then fail("debugDraw lost") end

print("ocean_scene_roundtrip: PASS")
