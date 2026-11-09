local function fail(msg)
    io.stderr:write("scene_archive_roundtrip FAIL: " .. msg .. "\n")
    os.exit(1)
end

local source_path = arg and arg[1]
local output_root = arg and arg[2]
if not source_path or not output_root then fail("expected <source-scene> <artifact-dir> [legacy]") end
local legacy_baseline = arg and arg[3] == "legacy"

local separator = package.config:sub(1, 1)
local windows = separator == "\\"

local function shell_quote(value)
    if windows then return '"' .. value .. '"' end
    return "'" .. value:gsub("'", "'\\''") .. "'"
end

local function mkdir(path)
    local command = windows and "if not exist " .. shell_quote(path) .. " mkdir " .. shell_quote(path)
        or "mkdir -p " .. shell_quote(path)
    local ok, _, code = os.execute(command)
    if not ok or code ~= 0 then fail("cannot create directory: " .. path) end
end

local function dirname(path)
    return path:match("^(.*)/[^/]*$")
end

local function read_file(path)
    local file = io.open(path, "rb")
    if not file then fail("cannot read: " .. path) end
    local bytes = file:read("a")
    file:close()
    return bytes
end

local function write_file(path, bytes)
    local parent = dirname(path)
    if parent then mkdir(parent) end
    local file = io.open(path, "wb")
    if not file then fail("cannot write: " .. path) end
    file:write(bytes)
    file:close()
end

local function copy_file(source, target)
    write_file(target, read_file(source))
end

local function resolve_asset(scene, path)
    if path:sub(1, 7) == "Engine/" then return "Resource/" .. path:sub(8) end
    if path:sub(1, 1) == "/" or path:match("^%a:[/\\]") then return path end
    if path:sub(1, 9) == "Projects/" or path:sub(1, 9) == "Resource/" then return path end
    return scene:GetWorkingDir() .. path
end

local function stage_sidecars(scene, output_dir)
    Scene.ForEachNode(scene, function(node)
        local terrain = node:GetTerrain()
        if not terrain then return end
        local heightmap = terrain:GetHeightmapPath()
        if heightmap == "" or heightmap:sub(1, 7) == "Engine/" then return end
        if heightmap:sub(1, 1) == "/" or heightmap:match("^%a:[/\\]") then return end
        local sidecar = heightmap:gsub("%.[^./]+$", ".json")
        for _, path in ipairs({ heightmap, sidecar }) do
            copy_file(resolve_asset(scene, path), output_dir .. "/" .. path)
        end
    end)
end

local function save_and_load(scene, path)
    local output_dir = dirname(path)
    mkdir(output_dir)
    stage_sidecars(scene, output_dir)
    if not FileUtil.SaveByExtension(scene, path) then fail("save failed: " .. path) end
    local loaded = Importer.LoadScene(path)
    if not loaded then fail("fresh load failed: " .. path) end
    Scene.SetActive(loaded)
    return loaded
end

local count_keys = {
    "nodes", "meshes_total", "meshes_static", "meshes_skinned", "submeshes",
    "vertices", "triangles", "materials", "textures", "animations", "joints",
}

local function compare_counts(first, second, label)
    for _, key in ipairs(count_keys) do
        if first[key] ~= second[key] then
            fail(string.format("%s count %s: %s ~= %s", label, key,
                tostring(first[key]), tostring(second[key])))
        end
    end
end

local function report_differences(differences, label)
    if #differences == 0 then return end
    for _, entry in ipairs(differences) do
        if _ <= 12 then
            io.stderr:write(string.format("%s: %s: %s (%s) ~= %s (%s)\n", label,
                entry.path, entry.first, entry.firstRaw, entry.second, entry.secondRaw))
        end
    end
    fail(string.format("%s document diff has %d entr%s", label, #differences,
        #differences == 1 and "y" or "ies"))
end

-- Compare two saved scene documents given as paths.
local function compare_files(first, second, label)
    report_differences(FileUtil.CompareSceneFiles(first, second), label)
end

-- Compare two saved scene documents given as JSON text.
local function compare_documents(first, second, label)
    report_differences(Scene.DiffPersistentState(first, second), label)
end

local function counts(scene)
    return Scene.GetStateCounts(scene)
end

mkdir(output_root)
local final_documents = {}
for _, extension in ipairs({ "json", "bin" }) do
    local source = Importer.LoadScene(source_path)
    if not source then fail("cannot load source: " .. source_path) end
    Scene.SetActive(source)
    local source_counts = counts(source)
    local base = output_root .. "/" .. extension
    local first = save_and_load(source, base .. "/gen1/scene." .. extension)
    compare_counts(source_counts, counts(first), extension .. " source/gen1")
    local second = save_and_load(first, base .. "/gen2/scene." .. extension)
    compare_counts(counts(first), counts(second), extension .. " gen1/gen2")
    compare_files(base .. "/gen1/scene." .. extension, base .. "/gen2/scene." .. extension,
        extension .. " gen1/gen2")
    final_documents[extension] = base .. "/gen2/scene." .. extension
    print("scene_archive_roundtrip: " .. extension .. " semantic round trip OK")
end
compare_files(final_documents.json, final_documents.bin, "JSON/BIN")

local string_source = Importer.LoadScene(source_path)
if not string_source then fail("cannot reload source for string serialization") end
Scene.SetActive(string_source)
local function texture_paths(scene)
    local paths = {}
    scene:ForEachTexture(function(texture) paths[#paths + 1] = texture:GetFilePath() end)
    table.sort(paths)
    return table.concat(paths, "\n")
end
local source_paths = texture_paths(string_source)
local json = FileUtil.SerializeSceneToString(string_source)
if json == "" then fail("string serialization failed") end
if texture_paths(string_source) ~= source_paths then fail("string serialization relocated textures") end
local string_scene = Scene.Create("string_roundtrip", string_source:GetWorkingDir(), OcTree.Create())
if not FileUtil.DeserializeSceneFromString(string_scene, json) then fail("string deserialization failed") end
Scene.SetActive(string_scene)
compare_counts(counts(string_source), counts(string_scene), "string")
compare_documents(json, FileUtil.SerializeSceneToString(string_scene), "string")
if not string_source:GetRootNode():CloneTree("StringClone") then fail("root CloneTree failed") end
if texture_paths(string_source) ~= source_paths then fail("CloneTree relocated textures") end
local before_cook = FileUtil.SerializeSceneToString(string_source)
local cook_references = Scene.CollectCookReferences(string_source)
compare_documents(before_cook, FileUtil.SerializeSceneToString(string_source), "cook traversal")
print("scene_archive_roundtrip: string, clone and cook texture identity OK (" .. #cook_references .. " typed refs)")

local compat_dir = output_root .. "/compat"
mkdir(compat_dir)

local function document(options)
    options = options or {}
    local name = options.name or '"compat"'
    local version = options.version == nil and '"version": 3,' or options.version
    local textures = options.textures == nil and '"textures": [],' or options.textures
    local materials = options.materials == nil and '"materials": [],' or options.materials
    local pos = options.pos or "[0.0, 0.0, 0.0, 1.0]"
    local components = options.components or "[]"
    local render = options.render or ""
    local physics = options.physics or ""
    return [[{
      "name": ]] .. name .. [[,
      ]] .. version .. [[
      ]] .. textures .. [[
      ]] .. materials .. [[
      "meshes": [], "animations": [], "particleSystems": [], "heightmaps": [],
      "nodes": {
        "name": "RootNode",
        "pos": ]] .. pos .. [[,
        "rot": [0.0, 0.0, 0.0, 1.0],
        "scl": [1.0, 1.0, 1.0, 1.0],
        "components": ]] .. components .. [[,
        "childs": []
      }
      ]] .. render .. [[
      ]] .. physics .. [[
    }]]
end

local function load_case(name, contents, expected)
    local path = compat_dir .. "/" .. name
    write_file(path, contents)
    local previous = Scene.GetActive()
    local loaded = Importer.LoadScene(path)
    if expected and not loaded then fail(name .. ": expected load success") end
    if not expected and loaded then fail(name .. ": expected load failure") end
    if not expected and Scene.GetActive() ~= previous then fail(name .. ": failed load became active") end
    if loaded then Scene.SetActive(loaded) end
    return loaded
end

local legacy = load_case("legacy_optional.json", document({ version = "", textures = "" }), true)
if legacy:GetRenderSettings():IsHDR() then fail("legacy optional render settings defaulted to HDR") end
if legacy:GetRenderSettings():IsClusteredLighting() then fail("missing clustered_lighting did not default to false") end
if #legacy:GetRenderSettings():GetChain() ~= 0 then fail("legacy optional chain is not empty") end

local clustered_enabled = load_case("clustered_enabled.json", document({
    render = ',"renderSettings": {"clustered_lighting": true}',
}), true)
if not clustered_enabled:GetRenderSettings():IsClusteredLighting() then
    fail("clustered_lighting=true was not loaded")
end
local clustered_enabled_json = save_and_load(clustered_enabled, compat_dir .. "/clustered_enabled_roundtrip.json")
if not clustered_enabled_json:GetRenderSettings():IsClusteredLighting() then
    fail("headless JSON save rewrote the clustered lighting request")
end
local clustered_enabled_bin = save_and_load(clustered_enabled_json, compat_dir .. "/clustered_enabled_roundtrip.bin")
if not clustered_enabled_bin:GetRenderSettings():IsClusteredLighting() then
    fail("headless BIN save rewrote the clustered lighting request")
end
local clustered_disabled = load_case("clustered_disabled.json", document({
    render = ',"renderSettings": {"clustered_lighting": false}',
}), true)
if clustered_disabled:GetRenderSettings():IsClusteredLighting() then
    fail("clustered_lighting=false was not loaded")
end
compare_files(compat_dir .. "/clustered_enabled_roundtrip.bin",
    compat_dir .. "/clustered_enabled_roundtrip.json", "clustered JSON/BIN")

load_case("invalid_required.json", document({ name = "42" }), false)
load_case("future_version.json", document({ version = '"version": 999,' }), false)
local vector3 = load_case("vector3.json", document({ pos = "[1.0, 2.0, 3.0]" }), true)
local vector3_pos = vector3:GetRootNode():GetLocalPosition()
if math.abs(vector3_pos.w - 1.0) > 1e-4 then fail("three-component Vector4 w mismatch") end
local expected_x = legacy_baseline and 0.0 or 1.0
if math.abs(vector3_pos.x - expected_x) > 1e-4 then fail("three-component Vector4 x mismatch") end
load_case("vector2.json", document({ pos = "[1.0, 2.0]" }), legacy_baseline)
if not legacy_baseline then
    load_case("uniform_short.json", document({ materials = [[
      "materials": [{"name":"mat","shaders":[],"textures":[],"uniforms":[
        {"key":"diffuse_color","type":"Uniform4f","data":[1.0,0.0,0.0]}
      ]}],
    ]] }), false)
end
load_case("unknown_component.json", document({
    components = '[{"type":"NotARegisteredComponent","value":1}]',
}), true)

local duplicate = load_case("duplicate_paths.json", document({ textures = [[
  "textures": [
    {"name":"first","path":"same.png","srgb":false},
    {"name":"second","path":"same.png","srgb":false}
  ],
]] }), true)
local texture_count = 0
duplicate:ForEachTexture(function(texture)
    texture_count = texture_count + 1
    if texture:GetFilePath() ~= "same.png" then fail("duplicate path identity changed") end
end)
if texture_count ~= 1 then fail("duplicate texture paths did not collapse") end

local physics = load_case("physics.json", document({
    physics = ',"physics": {"gravity": [0.0, -981.0, 0.0, 0.0]}',
}), true)
if not physics:HasPhysicsSettings() then fail("physics block was not retained") end
local gravity = physics:GetPhysicsGravity()
if math.abs(gravity.y + 981.0) > 1e-4 then fail("physics gravity changed") end
load_case("physics_no_world.json", read_file(compat_dir .. "/physics.json"), true)

local truncated = compat_dir .. "/truncated.bin"
write_file(truncated, string.char(0, 0, 0, 20))
if Importer.LoadScene(truncated) then fail("truncated LZ4 loaded successfully") end

save_and_load(legacy, compat_dir .. "/minimal.json")
save_and_load(legacy, compat_dir .. "/minimal.bin")
compare_files(compat_dir .. "/minimal.json", compat_dir .. "/minimal.bin", "minimal JSON/BIN")

print("scene_archive_roundtrip: compatibility cases OK")
print("scene_archive_roundtrip: OK")
