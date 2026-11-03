-- setup_feature_scene.lua - build the canonical feature-showcase scene.
-- Idempotent: always builds a FRESH scene and overwrites feature.bin.
--
--   ./fury exec Projects/samples/fox/Fox.gltf Projects/feature/setup_feature_scene.lua
--
-- Result: examples/Projects/feature/feature.bin with TankGroup (6 tanks +
-- turret spot + ground), Campfire (log pile + FireEmber/SmokePlume +
-- FireLight), animated Fox and JamesRig, Sun/KeySpot lights, MainCamera,
-- PBR+HDR+CSM render settings with ACES+FXAA, and all particle/model
-- textures copied into Projects/feature/ so reload has no dependency on
-- the outdoor scene files.

local FEATURE_DIR = "Projects/feature"

local function fail(msg)
    io.stderr:write("setup_feature_scene FAIL: " .. msg .. "\n")
    os.exit(1)
end

-- ---- small utilities -------------------------------------------------------

local function file_copy(src, dst)
    local fin, err = io.open(src, "rb")
    if not fin then return false, err end
    local bytes = fin:read("a")
    fin:close()
    local fout, werr = io.open(dst, "wb")
    if not fout then return false, werr end
    fout:write(bytes)
    fout:close()
    return true
end

local function copy_asset(src, dst_name)
    local dst = FEATURE_DIR .. "/" .. dst_name
    local ok, err = file_copy(src, dst)
    if not ok then fail("copy " .. src .. " -> " .. dst .. ": " .. tostring(err)) end
    print("setup_feature_scene: asset " .. dst_name .. " <- " .. src)
end

-- Union of every mesh/particle-bearing descendant's world AABB.
local function union_aabb(node)
    local has = false
    local minx, miny, minz = math.huge, math.huge, math.huge
    local maxx, maxy, maxz = -math.huge, -math.huge, -math.huge
    local function walk(n)
        if n:GetMeshRender() or n:GetParticleRenderer() then
            local b = n:GetWorldAABB()
            if b:Valid() and not b:GetInfinite() then
                local mn, mx = b:GetMin(), b:GetMax()
                minx = math.min(minx, mn.x); miny = math.min(miny, mn.y); minz = math.min(minz, mn.z)
                maxx = math.max(maxx, mx.x); maxy = math.max(maxy, mx.y); maxz = math.max(maxz, mx.z)
                has = true
            end
        end
        for i = 0, n:GetChildCount() - 1 do walk(n:GetChildAt(i)) end
    end
    walk(node)
    if not has then return nil end
    return { minx = minx, miny = miny, minz = minz, maxx = maxx, maxy = maxy, maxz = maxz }
end

-- Shift node so its content sits on the ground, centered at (target_x, target_z).
local function place_on_ground(node, target_x, target_z)
    node:GetParent():Recompose(false)
    node:Recompose(false)
    local b = union_aabb(node)
    if not b then return end
    local p = node:GetLocalPosition()
    node:SetLocalPosition(Vector4(
        p.x + (target_x - (b.minx + b.maxx) * 0.5),
        p.y - b.miny,
        p.z + (target_z - (b.minz + b.maxz) * 0.5), 1.0))
end

local function aim_at(node, target)
    local p = node:GetLocalPosition()
    local dir = Vector4(target[1] - p.x, target[2] - p.y, target[3] - p.z, 0.0):Normalized()
    local yaw = math.atan(-dir.x, -dir.z)
    local pitch = math.asin(math.max(-1.0, math.min(1.0, dir.y)))
    node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
end

-- First descendant named `name`, depth-first.
local function find_node(node, name)
    if node:GetName() == name then return node end
    for i = 0, node:GetChildCount() - 1 do
        local hit = find_node(node:GetChildAt(i), name)
        if hit then return hit end
    end
    return nil
end

-- Drop the importer's "RootNode" wrapper shells once keepers are moved out.
local function prune_shells(parent)
    local i = 0
    while i < parent:GetChildCount() do
        local child = parent:GetChildAt(i)
        if child:GetName() == "RootNode" then
            child:RemoveFromParent()
        else
            i = i + 1
        end
    end
end

-- ---- 0. durable project assets --------------------------------------------
-- Textures are referenced by bare filename, resolved against the feature
-- working dir; copy them in place before the save-time extraction runs.
local scene = Scene.Create("feature", FEATURE_DIR .. "/")
Scene.SetActive(scene)
local root = scene:GetRootNode()

-- ---- 1. tanks + ground ------------------------------------------------------
local tank_scene = Importer.LoadScene("Projects/samples/tank/tank.fbx")
if not tank_scene then fail("tank import failed") end
Importer.MergeInto(scene, tank_scene)

local ground = find_node(root, "Ground")
if not ground or not ground:GetMeshRender() then fail("tank Ground node not found") end
ground:RemoveFromParent()
root:AddChild(ground)

local tank_group = SceneNode.Create("TankGroup")
root:AddChild(tank_group)
for _, name in ipairs({ "Tank", "Tank.001", "Tank.002", "Tank.003", "Tank.004", "Tank.005" }) do
    local node = find_node(root, name)
    if not node or not node:GetMeshRender() then fail("tank mesh node missing: " .. name) end
    node:RemoveFromParent()
    tank_group:AddChild(node)
end
prune_shells(root)
place_on_ground(tank_group, -7.0, -6.0)
print("setup_feature_scene: TankGroup composed (6 meshes)")

-- ---- 2. fox -----------------------------------------------------------------
local fox_scene = Importer.LoadScene("Projects/samples/fox/Fox.gltf")
if not fox_scene then fail("fox import failed") end
Importer.MergeInto(scene, fox_scene)

local fox_root = find_node(root, "root") -- joint hierarchy
local fox_mesh = find_node(root, "fox")  -- skinned mesh
if not fox_root or not fox_mesh or not fox_mesh:GetMeshRender() then
    fail("fox nodes not found after merge")
end

local fox = SceneNode.Create("Fox")
root:AddChild(fox)
fox_root:RemoveFromParent(); fox:AddChild(fox_root)
fox_mesh:RemoveFromParent(); fox:AddChild(fox_mesh)
fox:SetLocalScale(0.05)
-- glTF textures register CWD-relative paths that double-prefix against the
-- scene working dir; retarget to the durable project copy.
copy_asset("Projects/samples/fox/Texture.png", "FoxTexture.png")
local fox_tex_fixed = false
scene:ForEachTexture(function(t)
    if t:GetFilePath():find("Texture%.png$") then
        t:SetFilePathAndSRGB("FoxTexture.png", true)
        fox_tex_fixed = true
    end
end)
if not fox_tex_fixed then fail("fox texture not found for retarget") end

local fox_anim = Animator.Create()
fox_mesh:AddComponent(fox_anim)
if not fox_anim:Play("Survey") then fail("fox clip 'Survey' did not play") end
place_on_ground(fox, 2.0, 7.0)
print("setup_feature_scene: Fox placed, Animator plays 'Survey'")

-- ---- 3. james ---------------------------------------------------------------
local james_scene = Importer.LoadScene("Projects/samples/james/james.fbx")
if not james_scene then fail("james import failed") end
Importer.MergeInto(scene, james_scene)

local james_arm = find_node(root, "James")
local james_mesh = find_node(root, "JamesNode")
if not james_arm or not james_mesh or not james_mesh:GetMeshRender() then
    fail("james nodes not found after merge")
end

local james = SceneNode.Create("JamesRig")
root:AddChild(james)
james_arm:RemoveFromParent(); james:AddChild(james_arm)
james_mesh:RemoveFromParent(); james:AddChild(james_mesh)
-- Authored 100x skinned-node scale is kept (bindpose math); present via the wrapper instead.
james:SetLocalScale(0.125) -- 14.43 native -> ~1.80 units tall

local james_anim = Animator.Create()
james_mesh:AddComponent(james_anim)
if not james_anim:Play("James|Walk") then fail("james clip 'James|Walk' did not play") end
prune_shells(root)
place_on_ground(james, 9.0, -2.0)
print("setup_feature_scene: JamesRig placed, Animator plays 'James|Walk'")

-- ---- 4. campfire + merged outdoor particles ---------------------------------
local donor = Importer.LoadScene("Projects/outdoor/outdoor_water.bin")
if not donor then fail("outdoor donor scene failed to load") end
local donor_root = donor:GetRootNode()
donor_root:Recompose(true)

local feu = find_node(donor_root, "Feu")
local ember_node = find_node(donor_root, "FireEmber")
local smoke_node = find_node(donor_root, "SmokePlume")
if not feu or not feu:GetMeshRender() then fail("donor Feu node not found") end
if not ember_node or not ember_node:GetParticleRenderer() then fail("donor FireEmber node not found") end
if not smoke_node or not smoke_node:GetParticleRenderer() then fail("donor SmokePlume node not found") end

-- Move the three nodes into the fresh scene; the donor is discarded afterwards.
local campfire = SceneNode.Create("Campfire")
root:AddChild(campfire)
campfire:SetLocalPosition(Vector4(5.0, 0.0, 4.0, 1.0))
campfire:Recompose(false)

for _, node in ipairs({ feu, ember_node, smoke_node }) do
    node:RemoveFromParent()
    campfire:AddChild(node)
end
-- Emitter transforms are outdoor-space; re-anchor them at the measured top
-- of the log pile (Campfire-local space == world offset here).
root:Recompose(true)
local pile = union_aabb(feu)
if not pile then fail("campfire log pile AABB not measurable") end
local anchor_x = (pile.minx + pile.maxx) * 0.5
local anchor_z = (pile.minz + pile.maxz) * 0.5
local base_y = pile.maxy
local cp = campfire:GetLocalPosition()
ember_node:SetLocalPosition(Vector4(anchor_x - cp.x, base_y + 0.17 - cp.y, anchor_z - cp.z, 1.0))
ember_node:SetLocalRoattion(Quaternion(0.0, 0.0, 0.0, 1.0))
smoke_node:SetLocalPosition(Vector4(anchor_x - cp.x, base_y + 0.70 - cp.y, anchor_z - cp.z, 1.0))
smoke_node:SetLocalRoattion(Quaternion(0.0, 0.0, 0.0, 1.0))

-- Transfer the particle systems + their materials + the log pile mesh into the EM.
for _, name in ipairs({ "FireEmber", "SmokePlume" }) do
    local ps = donor:GetParticleSystem(name)
    if not ps then fail("donor ParticleSystem missing: " .. name) end
    ps:SetStartSize(name == "FireEmber" and 1.0 or 1.5)
    scene:AddParticleSystem(ps)
end
local feu_mesh = feu:GetMeshRender():GetMesh()
if not feu_mesh then fail("donor Feu mesh pointer is nil") end
scene:AddMesh(feu_mesh)
local mats = 0
donor:ForEachMaterial(function(m)
    local n = m:GetName()
    if n == "Material_Fire" or n == "Material_Smoke" then
        scene:AddMaterial(m)
        mats = mats + 1
    end
end)
if mats ~= 2 then fail("donor particle materials missing (got " .. mats .. ")") end

-- Fire/smoke keep their authored material names; the log texture is renamed
-- so it does not collide with the fox texture in the project dir.
copy_asset("Projects/outdoor/fire.png", "fire.png")
copy_asset("Projects/outdoor/smoke.png", "smoke.png")
copy_asset("Projects/outdoor/Texture.png", "CampfireTexture.png")
local logs_mat = feu:GetMeshRender():GetMaterial(0)
if logs_mat then scene:AddMaterial(logs_mat) end
donor:ForEachTexture(function(t)
    local p = t:GetFilePath()
    if p == "Texture.png" or p:find("/Texture%.png$") then
        t:SetFilePathAndSRGB("CampfireTexture.png", t:IsSRGB())
    end
end)

-- Warm point light on the fire.
local fire_light_node = SceneNode.Create("FireLight")
campfire:AddChild(fire_light_node)
fire_light_node:SetLocalPosition(Vector4(anchor_x - cp.x, base_y + 1.0 - cp.y, anchor_z - cp.z, 1.0))
local fire_light = Light.Create()
fire_light:SetType(LightType.POINT)
fire_light:SetColor(Color(1.0, 0.55, 0.25, 1.0))
fire_light:SetIntensity(5.0)
fire_light:SetRadius(25.0)
fire_light_node:AddComponent(fire_light)
print("setup_feature_scene: Campfire + FireEmber/SmokePlume merged")

-- ---- 5. sun + key spot ------------------------------------------------------
local sun_node = SceneNode.Create("Sun")
root:AddChild(sun_node)
local sun = Light.Create()
sun:SetType(LightType.DIRECTIONAL)
sun:SetColor(Color(1.0, 0.96, 0.88, 1.0))
sun:SetIntensity(1.5)
sun:SetCastShadows(true)
sun_node:AddComponent(sun)
sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(math.rad(35.0), math.rad(-50.0), 0.0))

local spot_node = SceneNode.Create("KeySpot")
root:AddChild(spot_node)
spot_node:SetLocalPosition(Vector4(-1.0, 13.0, 7.0, 1.0))
local spot = Light.Create()
spot:SetType(LightType.SPOT)
spot:SetColor(Color(0.85, 0.9, 1.0, 1.0))
spot:SetIntensity(40.0)
spot:SetRadius(60.0)
spot:SetCastShadows(true)
spot_node:AddComponent(spot)
aim_at(spot_node, { -7.0, 1.0, -6.0 })
print("setup_feature_scene: Sun (CSM) + KeySpot placed")

-- ---- 6. inspection camera ---------------------------------------------------
local cam_node = SceneNode.Create("MainCamera")
root:AddChild(cam_node)
cam_node:SetLocalPosition(Vector4(15.0, 8.0, 17.0, 1.0))
local cam = Camera.Create()
cam:PerspectiveFov(0.7854, 16.0 / 9.0, 0.1, 5000.0)
cam_node:AddComponent(cam)
aim_at(cam_node, { 0.0, 1.5, 0.0 })

-- ---- 7. render settings -----------------------------------------------------
local rs = scene:GetRenderSettings()
rs:SetPipelinePath("Resource/Pipeline/DefferedLightingPBR.json")
rs:SetHDR(true)
rs:SetCascadedShadowMap(true)
rs:SetCsmMapSize(2048)
rs:SetShadowFar(6000.0)
rs:SetCsmSplitBlend(0.7)
rs:ClearChain()
rs:AddEffect("ACES", true)
rs:AddEffect("FXAA", true)

-- ---- 8. save ----------------------------------------------------------------
root:Recompose(true)

local out = FEATURE_DIR .. "/feature.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(out)) then
    fail("save failed: " .. out)
end
print("setup_feature_scene: wrote " .. out)
print("setup_feature_scene: DONE")
