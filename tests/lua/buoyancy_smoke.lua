-- tests/lua/buoyancy_smoke.lua - headless buoyancy spec (change:
-- add-fft-ocean, task 7.5; mirrors physics_smoke.lua conventions).
--
-- Usage:
--   ./fury exec Projects/outdoor/outdoor_water.bin tests/lua/buoyancy_smoke.lua
-- (the exec scene arg is required by the CLI but unused - the script builds
-- its own scene; the mini ocean fixture resolves cwd-relative)
--
-- Covers (spec ocean-buoyancy):
--   * flat-water fallback: invalid wave asset -> body floats at the flat
--     water level (no FFT dependency)
--   * fixture waves: sphere dropped from 3 m bobs and settles at the CPU
--     sampler's height; advancing wave time re-tracks the waterline
--   * 4-point cube dropped tilted levels out (up-vector assert)
--   * missing ocean node -> one warning, body falls with normal gravity
--   * determinism: save/reload re-drop agrees within 1 cm
--
-- Note: Physics.Step ticks the fixed loop only, so OceanComponent's wave
-- clock (Engine::OnUpdate) stays frozen headless; the test advances it by
-- hand via SetWaveTime to exercise waterline tracking.

local function fail(msg)
    io.stderr:write("buoyancy_smoke FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.Create("buoyancy_smoke", "", OcTree.Create())
Scene.SetActive(scene)
local root = scene:GetRootNode()

local mat = Material.Create("buoy_mat")
scene:AddMaterial(mat)
local sphere_mesh = MeshUtil.CreateSphere(16) -- unit sphere; BodySetup sets the real radius
scene:AddMesh(sphere_mesh)
local cube_mesh = MeshUtil.CreateCube()
scene:AddMesh(cube_mesh)

-- -- Oceans ---------------------------------------------------------------
-- Real fixture ocean (swell amp 40 cm over a 20 m tile).
local ocean_node = SceneNode.Create("Ocean")
local ocean = OceanComponent.Create()
ocean:SetMode(0) -- finite; geometry is irrelevant headless
ocean:SetWaveAssetPath("../tests/lua/fixtures/ocean_mini/ocean.json")
ocean_node:AddComponent(ocean)
if ocean:GetResolvedSource() ~= 1 then
    fail("fixture ocean did not resolve baked: " .. ocean:GetResolvedReason())
end
root:AddChild(ocean_node)

-- Flat fallback: missing asset -> flat water level (node y + level = 0).
local flat_node = SceneNode.Create("OceanFlat")
local flat_ocean = OceanComponent.Create()
flat_ocean:SetMode(0)
flat_ocean:SetWaveAssetPath("does/not/exist/ocean.json")
flat_node:AddComponent(flat_ocean)
if flat_ocean:GetResolvedSource() ~= 0 then
    fail("flat ocean should be unresolved, got " .. flat_ocean:GetResolvedSource())
end
root:AddChild(flat_node)

-- -- Floaters ---------------------------------------------------------------
local function makeSphere(name, x, z, oceanName)
    local node = SceneNode.Create(name)
    node:SetLocalPosition(Vector4(x, 300.0, z, 1.0))
    node:AddComponent(MeshRender.Create(mat, sphere_mesh))
    local body = BodySetup.Create()
    body:SetShapeType(2) -- sphere
    body:SetMotionType(1) -- dynamic
    body:SetMass(5.0)
    body:SetRadius(25.0)
    node:AddComponent(body)
    local floaty = BuoyancyComponent.Create()
    floaty:AddFloatPoint(0.0, 0.0, 0.0, 25.0)
    floaty:SetWaterDensity(2.0) -- rests half-submerged: center at water level
    floaty:SetLinearDrag(2.5)   -- settles a 3 m drop in ~10 s (0.8 bounces for 16+ s)
    floaty:SetAngularDrag(0.8)
    floaty:SetOceanNodeName(oceanName)
    node:AddComponent(floaty)
    root:AddChild(node)
    return node, body
end

local ball, ball_body = makeSphere("Ball", 0.0, 0.0, "Ocean")
local flat_ball, flat_body = makeSphere("FlatBall", 1000.0, 0.0, "OceanFlat")
local rock, rock_body = makeSphere("Rock", 0.0, 1000.0, "NoSuchOcean")
-- Determinism twin: identical drop 30 m away; settles at its own sampler
-- height. (Reload-and-resim is NOT deterministic in-script: Lua references
-- keep the old scene alive, the old body keeps its Jolt body, and the new
-- drop lands ON it - physics_smoke's window only hides that by luck.)
local ball2, ball2_body = makeSphere("Ball2", 3000.0, 0.0, "Ocean")

-- Tilted cube: 4 corner float points on the bottom face. A point-model
-- floater CAN stably capsize under impact (differential submersion of the
-- dry-side points forms a real inverted equilibrium); the righting term is
-- the authored escape and must dominate the buoyancy pendulum: on this 3 m
-- drop righting 8 still trapped at 137 deg, 20 levels it (documented in
-- docs/OCEAN.md).
local cube = SceneNode.Create("FloatCube")
cube:SetLocalPosition(Vector4(500.0, 300.0, 500.0, 1.0))
cube:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, 0.0, 0.4)) -- 23 deg tilt
cube:AddComponent(MeshRender.Create(mat, cube_mesh))
local cube_body = BodySetup.Create()
cube_body:SetShapeType(1) -- box
cube_body:SetMotionType(1)
cube_body:SetMass(8.0)
cube_body:SetHalfExtents(Vector4(25.0, 25.0, 25.0, 0.0))
cube:AddComponent(cube_body)
local cube_float = BuoyancyComponent.Create()
for _, sx in ipairs({ -25.0, 25.0 }) do
    for _, sz in ipairs({ -25.0, 25.0 }) do
        cube_float:AddFloatPoint(sx, -25.0, sz, 25.0)
    end
end
cube_float:SetWaterDensity(2.5)
cube_float:SetLinearDrag(2.0)
cube_float:SetAngularDrag(1.5)
cube_float:SetRightingStrength(20.0)
cube:AddComponent(cube_float)
root:AddChild(cube)

-- Long plank (2 m x 0.2 m x 0.5 m): the regression case for the angular
-- drag stability clamp. Its roll-axis inertia is ~44x below the crude
-- mass*size^2 estimate, so at the 25 Hz fixed tick the unclamped explicit
-- drag diverged (per-tick factor > 2) and the plank spun up on its own.
local plank = SceneNode.Create("Plank")
plank:SetLocalPosition(Vector4(-500.0, 200.0, -500.0, 1.0)) -- 1 m drop, flat
-- slight roll about the long axis (local x = the euler pitch arg): the
-- seed the unstable roll drag amplifies (on frozen headless water a
-- perfectly flat drop never perturbs the roll, so the divergence has
-- nothing to grow from)
plank:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, 0.15, 0.0))
plank:AddComponent(MeshRender.Create(mat, cube_mesh)) -- visual only; the shape is the half extents
local plank_body = BodySetup.Create()
plank_body:SetShapeType(1)
plank_body:SetMotionType(1)
plank_body:SetMass(12.0)
plank_body:SetHalfExtents(Vector4(100.0, 10.0, 25.0, 0.0))
plank:AddComponent(plank_body)
local plank_float = BuoyancyComponent.Create()
plank_float:AddFloatPoint(-90.0, 0.0, 0.0, 30.0)
plank_float:AddFloatPoint(90.0, 0.0, 0.0, 30.0)
plank_float:SetWaterDensity(2.2)
plank_float:SetLinearDrag(2.0)
plank_float:SetAngularDrag(2.0)
plank_float:SetRightingStrength(4.0)
plank_float:SetOceanNodeName("Ocean")
plank:AddComponent(plank_float)
root:AddChild(plank)

-- Save the pre-physics state for the reload determinism pass.
local save_path = "/tmp/buoyancy_smoke.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(save_path)) then
    fail("save failed")
end

-- -- Run --------------------------------------------------------------------
Physics.SetEnabled(true)
Physics.Step(1) -- lazy body creation + first tick
if not ball_body:HasBody() then fail("ball body not created") end
if not cube_body:HasBody() then fail("cube body not created") end

Physics.Step(400) -- 16 s at 25 Hz: splash, bob, settle

-- Wave time is frozen headless (Engine::OnUpdate never runs), so the
-- waterline per (x,z) is a constant the sampler reports exactly.
local water_ball = ocean:WaveHeightAtWorld(0.0, 0.0)
local y_ball = ball:GetWorldPosition().y
print(string.format("buoyancy_smoke: ball rest y=%.2f sampler=%.2f", y_ball, water_ball))
if math.abs(y_ball - water_ball) > 15.0 then
    fail(string.format("ball should settle at the sampler height %.2f, got %.2f", water_ball, y_ball))
end

-- Flat fallback: settles at the flat level (0) with no wave asset at all.
local y_flat = flat_ball:GetWorldPosition().y
print(string.format("buoyancy_smoke: flat ball rest y=%.2f (want ~0)", y_flat))
if math.abs(y_flat) > 5.0 then
    fail("flat-fallback ball should rest at the flat water level 0, got " .. y_flat)
end

-- Rock: missing ocean -> normal gravity, no floor -> free fall.
local y_rock = rock:GetWorldPosition().y
if y_rock > -1000.0 then
    fail("missing-ocean body should free-fall (y < -1000 after 16 s), got " .. y_rock)
end

-- Cube: up-vector levels out. local == world rotation (parent is root,
-- scale 1; GetWorldRoattion is safe here - no scaled ancestors).
local q = cube:GetWorldRoattion()
local e = MathUtil.QuatToEulerRad(q)
local up_y = math.cos(e.x) * math.cos(e.z)
print(string.format("buoyancy_smoke: cube up.y=%.3f (pitch %.2f roll %.2f)", up_y, e.x, e.z))
if up_y < 0.98 then
    fail("cube should level out, up.y=" .. up_y)
end

-- Plank spin regression (angular drag stability clamp): on FROZEN water a
-- flat-ish drop never excites the roll mode, so drive live water instead -
-- the wave clock advances in lockstep with the fixed tick. The unclamped
-- per-tick factor (3.5 on the plank's roll axis) flipped omega's sign per
-- substep and pumped a rolling limit cycle (reads as +-0.85 rad euler
-- aliasing below); the clamped drag keeps it near level.
for i = 1, 250 do
    ocean:SetWaveTime(i * 0.04)
    Physics.Step(1)
end
do
    local pe = MathUtil.QuatToEulerRad(plank:GetWorldRoattion())
    local roll = math.abs(pe.y)
    print(string.format("buoyancy_smoke: plank roll after 10 s live water = %.3f rad", roll))
    if roll > 0.5 then
        fail(string.format("plank should stay near level on live water, roll=%.2f rad", roll))
    end
end
ocean:SetWaveTime(0.0)

-- Waterline tracking: advance the wave clock half a loop; the ball must
-- follow the new sampler height.
ocean:SetWaveTime(2.0)
Physics.Step(100)
local water_ball2 = ocean:WaveHeightAtWorld(0.0, 0.0)
local y_ball2 = ball:GetWorldPosition().y
print(string.format("buoyancy_smoke: after time seek ball y=%.2f sampler=%.2f", y_ball2, water_ball2))
if math.abs(y_ball2 - water_ball2) > 15.0 then
    fail(string.format("ball should track the animated waterline %.2f, got %.2f", water_ball2, y_ball2))
end

-- -- Determinism: the wave loop is a pure function of t; returning the
-- clock to t=0 re-settles the ball to the same state (choppy-correction
-- branch choice varies with local wave shape, so residuals across
-- positions/times are NOT comparable - same x, same t is the contract).
local water_twin = ocean:WaveHeightAtWorld(3000.0, 0.0)
local y_twin = ball2:GetWorldPosition().y
print(string.format("buoyancy_smoke: twin rest y=%.2f sampler=%.2f", y_twin, water_twin))
if math.abs(y_twin - water_twin) > 15.0 then
    fail(string.format("twin ball should settle at its sampler height %.2f, got %.2f", water_twin, y_twin))
end

ocean:SetWaveTime(0.0)
Physics.Step(100)
local y_return = ball:GetWorldPosition().y
print(string.format("buoyancy_smoke: clock-return ball y=%.2f (first settle %.2f)", y_return, y_ball))
if math.abs(y_return - y_ball) > 1.0 then
    fail(string.format("determinism: t=0 return %.2f vs first settle %.2f", y_return, y_ball))
end

-- -- Round-trip: component fields survive save/load (no re-sim; see the
-- note at Ball2 for why resimulation after an in-process reload is unsafe)
if not Scene.LoadActive(FileUtil.GetAbsPath(save_path)) then
    fail("reload failed")
end
local scene2 = Scene.GetActive()
local cube2 = scene2:GetRootNode():FindChildRecursively("FloatCube")
if not cube2 then fail("FloatCube missing after reload") end
local float2 = cube2:GetBuoyancyComponent()
if not float2 then fail("BuoyancyComponent missing after reload") end
if float2:GetFloatPointCount() ~= 4 then fail("float points lost in round-trip") end
if math.abs(float2:GetWaterDensity() - 2.5) > 0.001 then fail("waterDensity lost") end
if math.abs(float2:GetLinearDrag() - 2.0) > 0.001 then fail("linearDrag lost") end
if math.abs(float2:GetAngularDrag() - 1.5) > 0.001 then fail("angularDrag lost") end
if math.abs(float2:GetRightingStrength() - 20.0) > 0.001 then fail("rightingStrength lost") end
if float2:GetOceanNodeName() ~= "Ocean" then fail("oceanNode lost") end
local p1r = float2:GetFloatPointRadius(1)
if math.abs(p1r - 25.0) > 0.001 then fail("float point radius lost") end
local p1o = float2:GetFloatPointOffset(1) -- insertion order: (-25,-25,-25), (-25,-25,25), ...
if math.abs(p1o.x + 25.0) > 0.001 or math.abs(p1o.y + 25.0) > 0.001 or math.abs(p1o.z - 25.0) > 0.001 then
    fail("float point offset lost")
end

print("buoyancy_smoke: PASS")
