-- setup_physics_scene.lua - derive outdoor_physics.bin from outdoor_water.bin
-- (change: add-jolt-physics-play-mode, task 8.2). NEVER writes the source
-- scene. Re-runnable: always starts from outdoor_water.bin.
--
--   ./fury exec Projects/outdoor/outdoor_water.bin Projects/outdoor/setup_physics_scene.lua
--
-- Result: examples/Projects/outdoor/outdoor_physics.bin with:
--   * Fox (imported from Projects/outdoor/Fox/) as the playable character:
--     Player(CharacterController) -> foxRoot (model+joints+Animator),
--     PlayerCamera child bound to the controller (third person boom)
--   * static mesh BodySetup on Grid / rocks / trees / fences
--   * 3 dynamic crates near spawn
--   * explicit physics block (gravity (0,-981,0))

local function fail(msg)
    io.stderr:write("setup_physics_scene FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene (exec must load outdoor_water.bin)") end
local root = scene:GetRootNode()

-- -- 1. Import the fox and merge it in -------------------------------------
local fox_scene = Importer.LoadGltf("Projects/outdoor/Fox/Fox.gltf")
if not fox_scene then fail("fox import failed") end
Importer.MergeInto(scene, fox_scene)

-- The imported glTF has TWO top-level nodes: "root" (joint hierarchy) and
-- "fox" (skinned mesh, sibling of root). Both must move with the player -
-- reparent the pair under a new Player node. Internal joint math is
-- untouched (do NOT strip node scales inside the subtree - bindpose math
-- depends on them).
local fox_root = root:FindChildRecursively("root")
if not fox_root then fail("imported fox 'root' node not found") end
local fox_mesh_node = root:FindChildRecursively("fox")
if not fox_mesh_node or not fox_mesh_node:GetMeshRender() then
    fail("fox mesh node (with MeshRender) not found")
end

local player = SceneNode.Create("Player")
player:SetLocalPosition(Vector4(0.0, 0.0, -400.0, 1.0)) -- open ground south of the fire pit
root:AddChild(player)
fox_root:RemoveFromParent()
player:AddChild(fox_root)
fox_mesh_node:RemoveFromParent()
player:AddChild(fox_mesh_node)

-- The fox texture imports with a CWD-relative path ("Projects/outdoor/...")
-- which breaks against the scene's working dir at save/load (double prefix).
-- Retarget the manager-registered texture working-dir-relative, matching
-- the scene's own textures ("fire.png" convention). Also fixes srgb (the
-- import marks it false).
local fox_tex = scene:GetTexture("Fox_image0.png")
if fox_tex then
    fox_tex:SetFilePathAndSRGB("Fox/Texture.png", true)
else
    print("setup_physics_scene: fox texture not found in manager (path fix skipped)")
end

-- Animator on the fox mesh node (importer does not attach one); clips are
-- in the scene's EntityManager named Survey / Walk / Run.
fox_mesh_node:AddComponent(Animator.Create())

-- -- 2. CharacterController + third-person camera --------------------------
local controller = CharacterController.Create()
-- Fox is ~79cm tall / ~155cm long: capsule height 70, radius 30 (clamped
-- by the 2r<=h rule), tuned for its world AABB.
controller:SetHeight(70.0)
controller:SetRadius(30.0)
controller:SetWalkSpeed(450.0)   -- 3x over the initial 150 after the first
controller:SetRunSpeed(1350.0)   -- playtest felt too slow
controller:SetJumpSpeed(350.0)
controller:SetCameraDistance(350.0)
controller:SetCameraHeight(130.0)
controller:SetModelYawOffset(180.0) -- fox nose faces +Z; engine forward is -Z
player:AddComponent(controller)

local camera_node = SceneNode.Create("PlayerCamera")
camera_node:AddComponent(Transform.Create())
local camera = Camera.Create()
camera:PerspectiveFov(0.7854, 1.778, 1.0, 8000.0)
camera_node:AddComponent(camera)
camera_node:SetLocalPosition(Vector4(0.0, 130.0, 350.0, 1.0))
camera_node:Recompose(false)
player:AddChild(camera_node)
controller:SetCameraNodeName("PlayerCamera")

-- -- 3. Static collision on the level geometry -----------------------------
local static_names = {
    "Grid",
    "Rock", "Rock1", "Rock2",
    "Tree1", "Tree2", "Tree3", "Tree4",
    "BarriereLong",
    "Barriere", "Barriere.001", "Barriere.002", "Barriere.003",
    "Barriere.004", "Barriere.005", "Barriere.006", "Barriere.007",
}
-- Grass billboards and the Feu fire/water plane deliberately get none.
local colliders = 0
for _, name in ipairs(static_names) do
    local node = root:FindChildRecursively(name)
    if node and node:GetMeshRender() then
        local body = BodySetup.Create() -- mesh shape + static by default
        node:AddComponent(body)
        colliders = colliders + 1
    else
        print("setup_physics_scene: skip " .. name .. " (not found / no mesh)")
    end
end
print("setup_physics_scene: static colliders on " .. colliders .. " nodes")

-- -- 4. Dynamic crates near spawn ------------------------------------------
local cube = MeshUtil.CreateCube()
cube:SetName("physics_crate_mesh")
scene:AddMesh(cube)
local crate_mat = Material.Create("physics_crate_mat")
scene:AddMaterial(crate_mat)

local crate_spots = {
    Vector4(60.0, 40.0, -330.0, 1.0),
    Vector4(-45.0, 60.0, -350.0, 1.0),
    Vector4(10.0, 100.0, -360.0, 1.0), -- starts airborne: drops in view of spawn
}
for i, pos in ipairs(crate_spots) do
    local crate = SceneNode.Create("Crate" .. i)
    crate:SetLocalPosition(pos)
    crate:SetLocalScale(40.0) -- 40cm cubes
    crate:AddComponent(MeshRender.Create(crate_mat, cube))
    local body = BodySetup.Create()
    body:SetShapeType(1)  -- box
    body:SetMotionType(1) -- dynamic
    crate:AddComponent(body)
    body:AutoFitFromMesh() -- after attach so the sibling MeshRender resolves
    body:SetMass(8.0)
    body:SetRestitution(0.2)
    root:AddChild(crate)
end

-- -- 5. Physics block + save -----------------------------------------------
scene:SetPhysicsGravity(Vector4(0.0, -981.0, 0.0, 0.0))

-- Recompose before measuring/saving so every world matrix is fresh.
root:Recompose(true)

local out = "Projects/outdoor/outdoor_physics.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(out)) then
    fail("save failed: " .. out)
end
print("setup_physics_scene: wrote " .. out)
print("setup_physics_scene: DONE")
