-- setup_ocean_infinite.lua - derive ocean_infinite.bin from ocean_base.bin
-- (change: add-fft-ocean, task 9.2). NEVER writes the source. Re-runnable:
-- always starts from ocean_base.bin.
--
--   ./fury exec Projects/ocean/ocean_base.bin Projects/ocean/setup_ocean_infinite.lua
--
-- Adds 8 pure-color plastic props with dynamic BodySetup + BuoyancyComponent
-- floating on the infinite ocean (4 spheres, 3 cubes, 1 plank):
--   * half rest at the waterline, half drop from 1 m up
--   * spheres: 1 center float point; cubes: 4 bottom-corner points;
--     plank: 2 end points (asymmetric tilt demo)
-- Meshes are baked to real cm size (TransformMesh) so node scales stay 1:
-- float-point offsets are node-local and the Jolt body frame matches.

local function fail(msg)
    io.stderr:write("setup_ocean_infinite FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene (exec must load ocean_base.bin)") end
local root = scene:GetRootNode()

local ocean_node = root:FindChildRecursively("Ocean")
if not ocean_node then fail("ocean_base.bin has no Ocean node") end

-- -- meshes (baked real size, node scale stays 1) ------------------------------
local function scaleMatrix(sx, sy, sz)
    return { sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1 }
end

local sphere_mesh = MeshUtil.CreateSphere(16)
sphere_mesh:SetName("ocean_sphere_25")
MeshUtil.TransformMesh(sphere_mesh, scaleMatrix(50.0, 50.0, 50.0)) -- radius 25
scene:AddMesh(sphere_mesh)

local cube_mesh = MeshUtil.CreateCube()
cube_mesh:SetName("ocean_cube_50")
MeshUtil.TransformMesh(cube_mesh, scaleMatrix(50.0, 50.0, 50.0)) -- 50 cm box
scene:AddMesh(cube_mesh)

local plank_mesh = MeshUtil.CreateCube()
plank_mesh:SetName("ocean_plank_200")
MeshUtil.TransformMesh(plank_mesh, scaleMatrix(200.0, 20.0, 50.0)) -- 2 m plank
scene:AddMesh(plank_mesh)

-- -- materials (pure-color plastic: no textures -> notexture gbuffer variant) --
local function makeMat(name, r, g, b)
    local m = Material.Create(name)
    m:SetUniform("diffuse_color", { r, g, b })
    m:SetUniform("roughness_factor", 0.3)
    m:SetUniform("metallic_factor", 0.0)
    scene:AddMaterial(m)
    return m
end

local mat_red    = makeMat("plastic_red",    0.85, 0.15, 0.12)
local mat_blue   = makeMat("plastic_blue",   0.12, 0.25, 0.85)
local mat_yellow = makeMat("plastic_yellow", 0.90, 0.75, 0.10)
local mat_green  = makeMat("plastic_green",  0.15, 0.70, 0.25)
local mat_white  = makeMat("plastic_white",  0.85, 0.85, 0.82)
local mat_orange = makeMat("plastic_orange", 0.90, 0.45, 0.10)

-- -- prop builders --------------------------------------------------------------
-- dropY: 0 rests at the waterline, 100 drops from 1 m up.
local function addSphere(name, mat, x, z, dropY)
    local node = SceneNode.Create(name)
    node:SetLocalPosition(Vector4(x, dropY, z, 1.0))
    node:AddComponent(MeshRender.Create(mat, sphere_mesh))
    local body = BodySetup.Create()
    body:SetShapeType(2) -- sphere
    body:SetMotionType(1) -- dynamic
    body:SetMass(5.0)
    body:SetRadius(25.0)
    node:AddComponent(body)
    local floaty = BuoyancyComponent.Create()
    floaty:AddFloatPoint(0.0, 0.0, 0.0, 25.0)
    floaty:SetWaterDensity(2.0)
    floaty:SetLinearDrag(2.5)
    floaty:SetAngularDrag(0.8)
    floaty:SetOceanNodeName("Ocean")
    floaty:SetDebugDraw(true)
    node:AddComponent(floaty)
    root:AddChild(node)
end

local function addCube(name, mat, x, z, dropY)
    local node = SceneNode.Create(name)
    node:SetLocalPosition(Vector4(x, dropY, z, 1.0))
    node:AddComponent(MeshRender.Create(mat, cube_mesh))
    local body = BodySetup.Create()
    body:SetShapeType(1) -- box
    body:SetMotionType(1)
    body:SetMass(8.0)
    body:SetHalfExtents(Vector4(25.0, 25.0, 25.0, 0.0))
    node:AddComponent(body)
    local floaty = BuoyancyComponent.Create()
    for _, sx in ipairs({ -25.0, 25.0 }) do
        for _, sz in ipairs({ -25.0, 25.0 }) do
            floaty:AddFloatPoint(sx, -25.0, sz, 25.0)
        end
    end
    floaty:SetWaterDensity(2.5)
    floaty:SetLinearDrag(2.0)
    floaty:SetAngularDrag(1.5)
    -- strong righting: the point-model cube can stably capsize on splash
    -- (see docs/OCEAN.md); 20 levels a 1 m drop
    floaty:SetRightingStrength(20.0)
    floaty:SetOceanNodeName("Ocean")
    floaty:SetDebugDraw(true)
    node:AddComponent(floaty)
    root:AddChild(node)
end

-- long plank: 2 end float points (asymmetric - tilts, then levels)
local function addPlank(name, mat, x, z, dropY)
    local node = SceneNode.Create(name)
    node:SetLocalPosition(Vector4(x, dropY, z, 1.0))
    node:AddComponent(MeshRender.Create(mat, plank_mesh))
    local body = BodySetup.Create()
    body:SetShapeType(1)
    body:SetMotionType(1)
    body:SetMass(12.0)
    body:SetHalfExtents(Vector4(100.0, 10.0, 25.0, 0.0))
    node:AddComponent(body)
    local floaty = BuoyancyComponent.Create()
    floaty:AddFloatPoint(-90.0, 0.0, 0.0, 30.0)
    floaty:AddFloatPoint(90.0, 0.0, 0.0, 30.0)
    floaty:SetWaterDensity(2.2)
    floaty:SetLinearDrag(2.0)
    floaty:SetAngularDrag(2.0)
    floaty:SetRightingStrength(4.0)
    floaty:SetOceanNodeName("Ocean")
    floaty:SetDebugDraw(true)
    node:AddComponent(floaty)
    root:AddChild(node)
end

-- ring of props around the default camera view (camera at 0,300,1500)
addSphere("SphereRed",    mat_red,    -300.0,  800.0, 0.0)
addSphere("SphereBlue",   mat_blue,    300.0,  800.0, 100.0)
addSphere("SphereYellow", mat_yellow, -600.0, 1200.0, 100.0)
addSphere("SphereGreen",  mat_green,   600.0, 1200.0, 0.0)
addCube("CubeWhite",  mat_white,  -150.0, 1000.0, 0.0)
addCube("CubeOrange", mat_orange,  150.0, 1000.0, 100.0)
addCube("CubeBlue",   mat_blue,    450.0,  900.0, 100.0)
addPlank("PlankRed",  mat_red,     0.0,  600.0, 100.0)

root:Recompose(true)

local out = "Projects/ocean/ocean_infinite.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(out)) then
    fail("save failed: " .. out)
end
print("setup_ocean_infinite: wrote " .. out)
print("setup_ocean_infinite: DONE")
