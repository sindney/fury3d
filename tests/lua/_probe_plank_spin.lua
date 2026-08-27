-- _probe_plank_spin.lua - reproduce the live-scene plank spin headlessly:
-- Physics.Step alone keeps the wave clock frozen (no forcing); this probe
-- advances the wave clock in lockstep with the fixed tick so the float
-- points see a moving waterline like in play mode.
--
--   ./fury exec Projects/outdoor/outdoor_water.bin ../tests/lua/_probe_plank_spin.lua

local scene = Scene.Create("plank_probe", "", OcTree.Create())
Scene.SetActive(scene)
local root = scene:GetRootNode()

local mat = Material.Create("p")
scene:AddMaterial(mat)
local cube_mesh = MeshUtil.CreateCube()
scene:AddMesh(cube_mesh)

local ocean_node = SceneNode.Create("Ocean")
local ocean = OceanComponent.Create()
ocean:SetMode(0)
ocean:SetWaveAssetPath("../tests/lua/fixtures/ocean_mini/ocean.json")
ocean_node:AddComponent(ocean)
root:AddChild(ocean_node)

local plank = SceneNode.Create("Plank")
plank:SetLocalPosition(Vector4(0.0, 200.0, 0.0, 1.0))
plank:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, 0.15, 0.0)) -- roll seed
plank:AddComponent(MeshRender.Create(mat, cube_mesh))
local body = BodySetup.Create()
body:SetShapeType(1)
body:SetMotionType(1)
body:SetMass(12.0)
body:SetHalfExtents(Vector4(100.0, 10.0, 25.0, 0.0))
plank:AddComponent(body)
local floaty = BuoyancyComponent.Create()
floaty:AddFloatPoint(-90.0, 0.0, 0.0, 30.0)
floaty:AddFloatPoint(90.0, 0.0, 0.0, 30.0)
floaty:SetWaterDensity(2.2)
floaty:SetLinearDrag(2.0)
floaty:SetAngularDrag(2.0)
floaty:SetRightingStrength(4.0)
floaty:SetOceanNodeName("Ocean")
plank:AddComponent(floaty)
root:AddChild(plank)

Physics.SetEnabled(true)
Physics.Step(1)

-- 20 s of animated water: wave clock and physics tick advance together
for i = 1, 500 do
    ocean:SetWaveTime(i * 0.04)
    Physics.Step(1)
    if i % 25 == 0 then
        local e = MathUtil.QuatToEulerRad(plank:GetWorldRoattion())
        print(string.format("t=%4.1fs  euler=(%.2f, %.2f, %.2f)  y=%.1f  sub=(%.2f, %.2f)",
            i * 0.04, e.x, e.y, e.z, plank:GetWorldPosition().y,
            floaty:GetLastSubmersion(0), floaty:GetLastSubmersion(1)))
    end
end
print("probe done")
