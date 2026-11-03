-- tests/lua/scene_component_registry.lua - component registry coverage:
-- proves every SceneNode::ComponentRegistry entry is exercised by the
-- union of the canonical fixtures plus the gap components this script
-- attaches (FreeFlyController, CharacterController, Transform, and
-- BodySetup on the island fixture), and that migrated components
-- round-trip through Serialize(Archive&) with identical fingerprints
-- while unmigrated ones keep riding the Load/Save adapter.
--
-- Usage:
--   ./fury exec <fixture-scene> tests/lua/scene_component_registry.lua <feature|island> <artifact-dir>

local function fail(msg)
    io.stderr:write("scene_component_registry FAIL: " .. msg .. "\n")
    os.exit(1)
end

local which = arg and arg[1]
local artifact_dir = arg and arg[2]
if not which or not artifact_dir then fail("expected <feature|island> <artifact-dir>") end

-- Every ComponentRegistry key. The per-fixture tables below must union
-- to this exact set; the startup self-check enforces it.
local registry = {
    "MeshRender", "InstancedMeshRender", "Light", "Camera", "Transform",
    "Animator", "ParticleRenderer", "BodySetup", "FreeFlyController",
    "CharacterController", "SkyAtmosphere", "Terrain", "OceanComponent",
    "BuoyancyComponent",
}

local expected = {
    feature = {
        "MeshRender", "Light", "Camera", "Animator", "ParticleRenderer",
        "FreeFlyController", "CharacterController", "Transform",
    },
    island = {
        "MeshRender", "InstancedMeshRender", "Light", "Camera",
        "SkyAtmosphere", "Terrain", "OceanComponent", "BuoyancyComponent",
        "BodySetup", "FreeFlyController", "CharacterController", "Transform",
    },
}

if not expected[which] then fail("unknown fixture: " .. which) end

do
    local union = {}
    for _, list in pairs(expected) do
        for _, name in ipairs(list) do union[name] = true end
    end
    for _, name in ipairs(registry) do
        if not union[name] then
            fail("registry entry '" .. name .. "' is not exercised by any fixture table")
        end
    end
    local count = 0
    for _ in pairs(union) do count = count + 1 end
    if count ~= #registry then fail("fixture tables reference unknown registry entries") end
end

local scene = Scene.GetActive()
if not scene then fail("no active scene") end
local root = scene:GetRootNode()

-- Gap components: not present in any canonical fixture, attached here so
-- their save/load path is exercised too.
local fly_node = SceneNode.Create("RegistryFreeFly")
fly_node:AddComponent(FreeFlyController.Create())
local fly = fly_node:GetFreeFlyController()
fly:SetEnabled(true)
fly:SetCameraNodeName("RegistryCamera")
fly:SetMoveSpeed(1234.5)
fly:SetMouseSensitivity(0.25)
root:AddChild(fly_node)

local char_node = SceneNode.Create("RegistryCharacter")
char_node:AddComponent(CharacterController.Create())
local char = char_node:GetCharacterController()
char:SetEnabled(false)
char:SetCameraNodeName("RegistryCamera")
char:SetHeight(175.0)
char:SetRadius(30.0)
char:SetWalkSpeed(250.0)
char:SetRunSpeed(650.0)
char:SetJumpSpeed(550.0)
root:AddChild(char_node)

if which == "island" then
    local body_node = SceneNode.Create("RegistryBody")
    body_node:AddComponent(BodySetup.Create())
    local body = body_node:GetBodySetup()
    body:SetShapeType(1) -- box
    body:SetMotionType(1) -- dynamic
    body:SetHalfExtents(Vector4(25.0, 40.0, 25.0))
    body:SetMass(12.5)
    body:SetFriction(0.7)
    body:SetRestitution(0.1)
    root:AddChild(body_node)
end

-- Transform: newly persisted as a valid typed object with no fields.
-- Load must leave the node transform untouched and re-sync from it.
local tf_node = SceneNode.Create("RegistryTransformNode")
tf_node:SetLocalPosition(11.0, 22.0, 33.0)
tf_node:SetLocalRoattion(0.0, 45.0, 0.0)
tf_node:SetLocalScale(2.5)
local tf_pos = tf_node:GetLocalPosition()
local tf_rot = tf_node:GetLocalRoattion()
local tf_scl = tf_node:GetLocalScale()
tf_node:AddComponent(Transform.Create(Vector4(11.0, 22.0, 33.0), tf_rot, Vector4(2.5)))
root:AddChild(tf_node)

local before = Scene.GetPersistentFingerprint(scene)

local function mkdir(path)
    os.execute("mkdir -p '" .. path:gsub("'", "'\\''") .. "'")
end
mkdir(artifact_dir)

local function check_roundtrip(extension)
    local path = artifact_dir .. "/registry_scene." .. extension
    if not FileUtil.SaveByExtension(scene, path) then fail("save failed: " .. path) end
    local loaded = Importer.LoadScene(path)
    if not loaded then fail("load failed: " .. path) end
    Scene.SetActive(loaded)
    local differences = Scene.DiffPersistentFingerprints(before,
        Scene.GetPersistentFingerprint(loaded))
    if #differences ~= 0 then
        for i, entry in ipairs(differences) do
            if i <= 8 then
                io.stderr:write(string.format("%s: %s: %s ~= %s\n", extension,
                    entry.path, entry.first, entry.second))
            end
        end
        fail(extension .. " fingerprint diff has " .. #differences .. " entries")
    end
    return path, loaded
end

local json_path, json_scene = check_roundtrip("json")
local _, bin_scene = check_roundtrip("bin")

-- Every expected component type must appear as a persisted "type" field
-- in the saved document.
local function read_file(path)
    local file = io.open(path, "rb")
    if not file then fail("cannot read: " .. path) end
    local bytes = file:read("a")
    file:close()
    return bytes
end

local discovered = {}
for name in read_file(json_path):gmatch('"type"%s*:%s*"([^"]+)"') do
    discovered[name] = true
end
for _, name in ipairs(expected[which]) do
    if not discovered[name] then
        fail(which .. " saved document lacks component type " .. name)
    end
end
print("scene_component_registry: " .. which .. " document covers "
    .. #expected[which] .. " component types")

-- Transform load behavior: node transform preserved, component attached.
local tf_loaded = json_scene:GetRootNode():FindChildRecursively("RegistryTransformNode")
if not tf_loaded then fail("RegistryTransformNode missing after reload") end
local function near(a, b) return math.abs(a - b) <= 1e-3 end
local p = tf_loaded:GetLocalPosition()
if not (near(p.x, tf_pos.x) and near(p.y, tf_pos.y) and near(p.z, tf_pos.z)) then
    fail("Transform load changed node position")
end
local r = tf_loaded:GetLocalRoattion()
if not (near(r.x, tf_rot.x) and near(r.y, tf_rot.y) and near(r.z, tf_rot.z) and near(r.w, tf_rot.w)) then
    fail("Transform load changed node rotation")
end
local s = tf_loaded:GetLocalScale()
if not (near(s.x, tf_scl.x) and near(s.y, tf_scl.y) and near(s.z, tf_scl.z)) then
    fail("Transform load changed node scale")
end
if not tf_loaded:GetTransform() then fail("Transform component missing after reload") end

-- Migrated FreeFlyController round-trips field values (adapter path for
-- now; the values still must survive save/load).
local fly_loaded = bin_scene:GetRootNode():FindChildRecursively("RegistryFreeFly")
if not fly_loaded then fail("RegistryFreeFly missing after bin reload") end
local fly2 = fly_loaded:GetFreeFlyController()
if not fly2 then fail("FreeFlyController missing after bin reload") end
if not near(fly2:GetMoveSpeed(), 1234.5) then fail("FreeFlyController move_speed changed") end
if not near(fly2:GetMouseSensitivity(), 0.25) then fail("FreeFlyController sensitivity changed") end
if fly2:GetCameraNodeName() ~= "RegistryCamera" then fail("FreeFlyController camera node changed") end

-- Malformed / strictness cases for the migrated component path. These
-- documents go through the same Scene::Load envelope as the main
-- round-trip harness.
local compat_dir = artifact_dir .. "/compat"
mkdir(compat_dir)

local function document(options)
    options = options or {}
    local version = options.version or '"version": 3,'
    local textures = options.textures or '"textures": [],'
    local materials = options.materials or '"materials": [],'
    local meshes = options.meshes or '"meshes": [],'
    local components = options.components or "[]"
    return [[{
      "name": "compat",
      ]] .. version .. [[
      ]] .. textures .. [[
      ]] .. materials .. [[
      "animations": [], "particleSystems": [], "heightmaps": [],
      ]] .. meshes .. [[
      "nodes": {
        "name": "RootNode",
        "pos": [0.0, 0.0, 0.0, 1.0],
        "rot": [0.0, 0.0, 0.0, 1.0],
        "scl": [1.0, 1.0, 1.0, 1.0],
        "components": ]] .. components .. [[,
        "childs": []
      }
    }]]
end

local function load_case(name, contents, expect_ok)
    local path = compat_dir .. "/" .. name
    local file = io.open(path, "wb")
    if not file then fail("cannot write " .. path) end
    file:write(contents)
    file:close()
    local previous = Scene.GetActive()
    local loaded = Importer.LoadScene(path)
    if expect_ok and not loaded then fail(name .. ": expected load success") end
    if not expect_ok and loaded then fail(name .. ": expected load failure") end
    if not expect_ok and Scene.GetActive() ~= previous then
        fail(name .. ": failed load became active")
    end
    return loaded
end

local mesh_and_material = {
    meshes = '"meshes": [{"name":"m","positions":[],"indices":[],"submeshes":[],"cast_shadows":true}],',
    materials = '"materials": [{"name":"mat","shaders":[],"textures":[],"uniforms":[]}],',
}

load_case("mesh_missing.json", document({
    components = '[{"type":"MeshRender","materials":[]}]' }), false)
load_case("mesh_wrong_type.json", document({
    components = '[{"type":"MeshRender","mesh":42,"materials":[]}]' }), false)
load_case("material_unknown.json", document({
    components = '[{"type":"MeshRender","mesh":"m","materials":["nope"]}]',
    meshes = mesh_and_material.meshes }), false)
load_case("light_missing_type.json", document({
    components = '[{"type":"Light"}]' }), false)
load_case("light_bad_color.json", document({
    components = '[{"type":"Light","light_type":"spot","color":[1.0,2.0]}]' }), false)
load_case("imr_bad_instance.json", document({
    components = '[{"type":"InstancedMeshRender","mesh":"m","materials":["mat"],"instances":[42]}]',
    meshes = mesh_and_material.meshes,
    materials = mesh_and_material.materials }), false)

local light_defaults = load_case("light_optional_defaults.json", document({
    components = '[{"type":"Light","light_type":"point"}]' }), true)
do
    local light = light_defaults:GetRootNode():GetLight()
    if not light then fail("light_optional_defaults: Light component missing") end
    if not near(light:GetIntensity(), 1.0) then fail("absent optional intensity default changed") end
    if light:GetCastShadows() then fail("absent optional cast_shadows default changed") end
    local c = light:GetColor()
    if not (near(c.r, 1.0) and near(c.g, 1.0) and near(c.b, 1.0)) then
        fail("absent optional color default changed")
    end
end

local mesh_seed = load_case("mesh_cast_shadows_seed.json", document({
    components = '[{"type":"MeshRender","mesh":"m","materials":["mat"]}]',
    meshes = mesh_and_material.meshes,
    materials = mesh_and_material.materials }), true)
do
    local render = mesh_seed:GetRootNode():GetMeshRender()
    if not render then fail("mesh_cast_shadows_seed: MeshRender missing") end
    if not render:GetCastShadows() then
        fail("absent cast_shadows did not seed from the mesh asset flag")
    end
end

local animator_lenient = load_case("animator_lenient_clips.json", document({
    components = '[{"type":"Animator","clips":[42,"x"]}]' }), true)
do
    local animator = animator_lenient:GetRootNode():GetAnimator()
    if not animator then fail("animator_lenient_clips: Animator missing") end
    if animator:GetStateCount() ~= 0 then
        fail("non-string clip entries must be skipped, not loaded")
    end
end

local transform_doc = load_case("transform_typed.json", document({
    components = '[{"type":"Transform"}]' }), true)
do
    local node = transform_doc:GetRootNode()
    if not node:GetTransform() then fail("transform_typed: Transform missing") end
    local p = node:GetLocalPosition()
    if not (near(p.x, 0.0) and near(p.y, 0.0) and near(p.z, 0.0)) then
        fail("transform_typed: node position changed")
    end
end

load_case("unknown_component.json", document({
    components = '[{"type":"NotARegisteredComponent","value":1}]' }), true)

print("scene_component_registry: " .. which .. " OK")
