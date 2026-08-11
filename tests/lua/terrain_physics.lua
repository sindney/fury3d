-- tests/lua/terrain_physics.lua - headless test for the Terrain component
-- and the BodySetup heightfield shape (change: add-sky-atmosphere-terrain).
--
-- Builds a terrain from the mini fixture (tests/lua/fixtures/terrain_mini),
-- drops a dynamic box on it, and asserts the box rests at GetHeight.
-- Also covers: chunk meshes skipped at serialization, Terrain round-trip,
-- GetHeight edge clamp, heightfield-without-Terrain failure path.
--
-- Covers spec scenarios:
--   heightmap-terrain:   "Spawn placement sits on the surface",
--                        "Out-of-bounds query clamps", "Saved scene contains
--                        no chunk meshes"
--   body-setup-component:"Character walks on the terrain" (box rest),
--                        "Heightfield without terrain fails cleanly"
--
-- Usage:
--   ./fury exec Projects/outdoor/outdoor_water.bin tests/lua/terrain_physics.lua
-- (the exec scene arg is required by the CLI but unused - the script builds
-- its own scene; CWD must be examples/ so the fixture path resolves)

local function fail(msg)
    io.stderr:write("terrain_physics FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.Create("terrain_physics", "", OcTree.Create())
Scene.SetActive(scene)

local terrain_node = SceneNode.Create("Ground")
local terrain = Terrain.Create()
terrain:SetHeightmapPath("../tests/lua/fixtures/terrain_mini/height.r16")
terrain:SetSplatmapPath("../tests/lua/fixtures/terrain_mini/splat.png")
terrain:SetChunkCount(4)
terrain:SetLodCount(2)
terrain_node:AddComponent(terrain)

local body = BodySetup.Create()
body:SetShapeType(3) -- heightfield (static by default)
terrain_node:AddComponent(body)
scene:GetRootNode():AddChild(terrain_node)

if not terrain:HasHeights() then fail("heights not loaded") end

-- heights at probe points
local h_center = terrain:GetHeight(0.0, -200.0)
local h_probe = terrain:GetHeight(1000.0, 800.0)
print(string.format("terrain_physics: h(center)=%.1f h(probe)=%.1f", h_center, h_probe))
if h_center < 1.0 then fail("center height looks unloaded") end

-- out-of-bounds clamps to the edge sample
local h_edge = terrain:GetHeight(1.0e6, 0.0)
local h_edge_ref = terrain:GetHeight(3200.0, 0.0) -- world edge = size/2
if math.abs(h_edge - h_edge_ref) > 1.0 then
    fail(string.format("edge clamp: got %.1f want ~%.1f", h_edge, h_edge_ref))
end

-- heightfield without a Terrain sibling must fail cleanly
local orphan = SceneNode.Create("Orphan")
local orphan_body = BodySetup.Create()
orphan_body:SetShapeType(3)
orphan:AddComponent(orphan_body)
scene:GetRootNode():AddChild(orphan)

-- dynamic box dropped above the (flattened) center
local cube = MeshUtil.CreateCube()
scene:AddMesh(cube)
local mat = Material.Create("terrain_physics_mat")
scene:AddMaterial(mat)

local crate = SceneNode.Create("Crate")
crate:SetLocalPosition(Vector4(0.0, h_center + 300.0, -200.0, 1.0))
crate:SetLocalScale(50.0)
crate:AddComponent(MeshRender.Create(mat, cube))
local crate_body = BodySetup.Create()
crate_body:SetShapeType(1)  -- box
crate_body:SetMotionType(1) -- dynamic
crate_body:SetMass(5.0)
crate:AddComponent(crate_body)
crate_body:AutoFitFromMesh()
scene:GetRootNode():AddChild(crate)

Physics.SetEnabled(true)
Physics.Step(1) -- lazy creation pass
if not body:HasBody() then fail("heightfield body not created") end
if orphan_body:HasBody() then fail("orphan heightfield should NOT have a body") end

Physics.Step(150) -- 6 s at 25 Hz
local pos = crate:GetWorldPosition()
local ground = terrain:GetHeight(pos.x, pos.z)
print(string.format("terrain_physics: crate rest (%.1f, %.1f, %.1f), ground %.1f",
    pos.x, pos.y, pos.z, ground))
-- rest = terrain surface + half box (25); tolerance covers one texel of slope
if pos.y < ground + 15.0 or pos.y > ground + 45.0 then
    fail(string.format("crate should rest on the terrain (ground %.1f), got %.1f", ground, pos.y))
end

-- round-trip: chunk nodes must NOT serialize; component params must survive
local path = "/tmp/terrain_physics.json"
if not FileUtil.SaveFile(scene, path) then fail("save failed") end
local f = io.open(path, "r")
if not f then fail("saved scene unreadable") end
local text = f:read("*a")
f:close()
if text:find("terrain_chunk", 1, true) then
    fail("chunk meshes leaked into the serialized scene")
end
if not text:find('"Terrain"', 1, true) then
    fail("Terrain component missing from the serialized scene")
end

if not Scene.LoadActive(path) then fail("reload failed") end
local ground2 = Scene.GetActive():GetRootNode():FindChildRecursively("Ground")
if not ground2 then fail("terrain node missing after reload") end
local terrain2 = ground2:GetTerrain()
if not terrain2 then fail("Terrain component missing after reload") end
if terrain2:GetChunkCount() ~= 4 then fail("chunk_count lost") end
if not terrain2:HasHeights() then fail("heights not rebuilt after reload") end
-- chunks rebuilt on load (play-mode temp saves strip editorOnly nodes)
local container = ground2:FindChildRecursively("__terrain_chunks")
if not container then fail("chunk container not rebuilt after reload") end
if container:GetChildCount() ~= 16 then
    fail("expected 16 chunk nodes after reload, got " .. container:GetChildCount())
end

print("terrain_physics: PASS")
