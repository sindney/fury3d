-- tests/lua/physics_smoke.lua - headless physics smoke test for the
-- BodySetup/PhysicsWorld core (change: add-jolt-physics-play-mode).
--
-- Builds a floor + falling crate programmatically, steps the world, and
-- asserts the crate comes to rest on the floor, then round-trips the scene
-- and re-checks the serialized fields. Covers spec scenarios:
--   physics-world:  "Dynamic body falls under gravity", gravity defaults
--   body-setup:     round-trip preserves body setup, auto-fit box
--
-- Usage:
--   ./fury exec Projects/outdoor/outdoor_water.bin tests/lua/physics_smoke.lua
-- (the exec scene arg is required by the CLI but unused - the script builds
-- its own scene)

local function fail(msg)
    io.stderr:write("physics_smoke FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.Create("physics_smoke", "", OcTree.Create())
Scene.SetActive(scene)

local cube = MeshUtil.CreateCube()
scene:AddMesh(cube)

local mat = Material.Create("physics_smoke_mat")
scene:AddMaterial(mat)

-- Floor: unit cube scaled to (2000, 100, 2000) -> box half extents
-- (1000, 50, 1000) after world-scale bake; top surface at y = 50.
-- (100 cm thick: the crate lands at ~790 cm/s = 32 cm per 25 Hz step -
-- a 20 cm floor tunneled; Discrete motion quality has no CCD.)
local floor = SceneNode.Create("Floor")
floor:SetLocalScale(Vector4(2000.0, 100.0, 2000.0, 1.0))
floor:AddComponent(MeshRender.Create(mat, cube))
local floor_body = BodySetup.Create()
floor_body:SetShapeType(1) -- box
floor:AddComponent(floor_body)
floor_body:AutoFitFromMesh()
scene:GetRootNode():AddChild(floor)

-- Crate: dynamic unit box dropped from y = 300. Rest pose ~= 50.5
-- (floor top 50 + half height 0.5xscale... unit cube half 0.5).
local crate = SceneNode.Create("Crate")
crate:SetLocalPosition(Vector4(0.0, 300.0, 0.0, 1.0))
crate:AddComponent(MeshRender.Create(mat, cube))
local crate_body = BodySetup.Create()
crate_body:SetShapeType(1)     -- box
crate_body:SetMotionType(1)    -- dynamic
crate_body:SetMass(5.0)
crate_body:SetFriction(0.8)
crate:AddComponent(crate_body)
crate_body:AutoFitFromMesh()   -- after attach, so the sibling MeshRender resolves
scene:GetRootNode():AddChild(crate)

-- Gravity default (cm units).
local g = Physics.GetGravity()
if math.abs(g.y + 981.0) > 0.01 then
    fail("default gravity should be (0,-981,0), got y=" .. g.y)
end

Physics.SetEnabled(true)
if not Physics.GetEnabled() then fail("physics did not enable") end

Physics.Step(1) -- lazy creation pass + first tick
if not floor_body:HasBody() then fail("floor body not created") end
if not crate_body:HasBody() then fail("crate body not created") end

Physics.Step(100) -- 4 s at 25 Hz
local y = crate:GetWorldPosition().y
print(string.format("physics_smoke: crate rest y = %.2f", y))
if y < 48.0 or y > 54.0 then
    fail("crate should rest on the floor top (~50.5), got " .. y)
end

-- Crate must not have drifted sideways on the flat floor. (The real impact
-- velocity is ~790 cm/s since BodySetup::CreateBody fixed the meter-scale
-- cMaxPhysicsVelocity clamp; the bounce can shift it a couple of cm.)
local x, z = crate:GetWorldPosition().x, crate:GetWorldPosition().z
if math.abs(x) > 3.0 or math.abs(z) > 3.0 then
    fail(string.format("crate drifted: x=%.2f z=%.2f", x, z))
end

-- Round-trip: save, reload, BodySetup fields intact.
local path = "/tmp/physics_smoke.bin"
if not FileUtil.SaveCompressedFile(scene, path) then fail("save failed") end
if not Scene.LoadActive(path) then fail("reload failed") end

local crate2 = Scene.GetActive():GetRootNode():FindChildRecursively("Crate")
if not crate2 then fail("crate missing after reload") end
local body2 = crate2:GetBodySetup()
if not body2 then fail("BodySetup missing after reload") end
if body2:GetMotionType() ~= 1 then fail("motion_type lost (want 1=dynamic)") end
if math.abs(body2:GetMass() - 5.0) > 0.001 then fail("mass lost") end
if math.abs(body2:GetFriction() - 0.8) > 0.001 then fail("friction lost") end
if body2:GetShapeType() ~= 1 then fail("shape_type lost (want 1=box)") end

-- The reloaded scene simulates too (components re-register on load).
Physics.Step(50)
local y2 = crate2:GetWorldPosition().y
if y2 < 48.0 or y2 > 54.0 then
    fail("reloaded crate should rest on the floor, got " .. y2)
end

print("physics_smoke: PASS (rest y=" .. string.format("%.2f", y) .. ", reloaded y=" .. string.format("%.2f", y2) .. ")")
