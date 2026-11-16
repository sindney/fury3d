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

load_case("cluster_lod_missing.json", document({
    meshes = '"meshes": [{"name":"missing_cluster","positions":[0,0,0,1,0,0,0,1,0],"indices":[0,1,2],"submeshes":[[0,1,2]],"cast_shadows":true}],',
}), false)

local cluster_source = Scene.Create("cluster_lod_roundtrip", compat_dir .. "/")
Scene.SetActive(cluster_source)
local cluster_mesh = MeshUtil.CreateCube()
local canonical = SubMesh.Create()
canonical:SetIndices(cluster_mesh:GetIndices())
cluster_mesh:AddSubMesh(canonical)
cluster_source:AddMesh(cluster_mesh)
local ddc_root = os.getenv("FURY_DDC") or "DDC"
local stored = ClusterLodTest.BuildAndStore(cluster_mesh, ddc_root)
if not stored.ok then fail("cluster DDC store failed: " .. tostring(stored.error)) end
if not stored.key:match("^CLOD_") then fail("cluster DDC key is not namespaced") end
local cluster_fingerprint = Scene.GetPersistentFingerprint(cluster_source)
ClusterLodTest.ClearDescriptor(cluster_mesh)
if #Scene.DiffPersistentFingerprints(cluster_fingerprint,
    Scene.GetPersistentFingerprint(cluster_source)) == 0 then
    fail("cluster descriptor loss was not fingerprinted")
end
stored = ClusterLodTest.BuildAndStore(cluster_mesh, ddc_root)
if not stored.ok then fail("cluster descriptor restore failed") end
for _, extension in ipairs({ "json", "bin" }) do
    Scene.SetActive(cluster_source)
    local path = compat_dir .. "/cluster_lod_roundtrip." .. extension
    if not FileUtil.SaveByExtension(cluster_source, path) then fail("cluster descriptor save failed") end
    local loaded = Importer.LoadScene(path)
    if not loaded then fail("cluster descriptor load failed: " .. extension) end
    Scene.SetActive(loaded)
    if #Scene.DiffPersistentFingerprints(cluster_fingerprint,
        Scene.GetPersistentFingerprint(loaded)) ~= 0 then
        fail("cluster descriptor fingerprint changed: " .. extension)
    end
end
local cluster_json = read_file(compat_dir .. "/cluster_lod_roundtrip.json")
if not cluster_json:find('"cluster_lod"', 1, true) then fail("cluster descriptor missing from JSON") end
if cluster_json:find('"gpu_clusters"', 1, true) or cluster_json:find('"merged_indices"', 1, true) then
    fail("cluster payload arrays were embedded in scene JSON")
end

Scene.SetActive(cluster_source)
local payload_fingerprint = Scene.GetPersistentFingerprint(cluster_source)
ClusterLodTest.DetachPayload(cluster_mesh)
if #Scene.DiffPersistentFingerprints(payload_fingerprint,
    Scene.GetPersistentFingerprint(cluster_source)) == 0 then
    fail("cluster payload loss was not fingerprinted")
end
local payload_path = ddc_root .. "/" .. stored.key:sub(1, 2) .. "/"
    .. stored.key:sub(3, 4) .. "/" .. stored.key .. ".clod"
local source_root = os.getenv("FURY_CLOD_ROOT")
    or (os.getenv("FURY_DDC") and (ddc_root .. "/source") or "Resource/ClusterLOD")
local source_payload_path = source_root .. "/" .. stored.key .. ".clod"
local payload_bytes = read_file(payload_path)
if read_file(source_payload_path) ~= payload_bytes then fail("durable and DDC payloads differ") end
local loaded_payload = ClusterLodTest.Load(cluster_mesh, ddc_root)
if not loaded_payload.ok then fail("cluster DDC load failed: " .. tostring(loaded_payload.error)) end
if #Scene.DiffPersistentFingerprints(payload_fingerprint,
    Scene.GetPersistentFingerprint(cluster_source)) ~= 0 then
    fail("cluster payload did not reattach")
end
if not os.remove(payload_path) then fail("cannot remove cluster DDC payload") end
if not os.remove(source_payload_path) then fail("cannot remove durable cluster payload") end
local missing = ClusterLodTest.Load(cluster_mesh, ddc_root)
if missing.ok or not tostring(missing.error):find(stored.key, 1, true) then
    fail("missing cluster payload did not report its key")
end
local function write_payload(bytes)
    local file = io.open(payload_path, "wb")
    if not file then fail("cannot restore cluster DDC payload") end
    file:write(bytes)
    file:close()
    file = io.open(source_payload_path, "wb")
    if not file then fail("cannot restore durable cluster payload") end
    file:write(bytes)
    file:close()
end
write_payload(payload_bytes)
local last_payload_byte = payload_bytes:byte(-1)
local corrupt_bytes = payload_bytes:sub(1, -2)
    .. string.char(last_payload_byte == 255 and 0 or last_payload_byte + 1)
write_payload(corrupt_bytes)
local corrupt = ClusterLodTest.Load(cluster_mesh, ddc_root)
if corrupt.ok then fail("corrupt cluster payload loaded successfully") end
write_payload(payload_bytes)
local original_normals = cluster_mesh:GetNormals()
cluster_mesh:SetNormals({ 0, 0, 1 })
local stale = ClusterLodTest.Load(cluster_mesh, ddc_root)
if stale.ok or not tostring(stale.error):find("stale", 1, true) then
    fail("attribute-only mesh edit did not invalidate the cluster payload")
end
cluster_mesh:SetNormals(original_normals)
local restored = ClusterLodTest.Load(cluster_mesh, ddc_root)
if not restored.ok then fail("restored cluster payload did not load") end

print("scene_component_registry: " .. which .. " OK")
