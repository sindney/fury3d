-- Independent persistence check for the editor UI save test output.
-- Usage: ./fury exec <saved-scene> ../tests/lua/ui_saved_scene_verify.lua <saved-scene> <artifact-dir>
-- Verifies the editor-saved scene carries the expected modified state and
-- round-trips semantically, without any editor state.

local function fail(msg)
    io.stderr:write("ui_saved_scene_verify FAIL: " .. msg .. "\n")
    os.exit(1)
end

local saved_path = arg and arg[1]
local output_root = arg and arg[2]
if not saved_path or not output_root then fail("expected <saved-scene> <artifact-dir>") end

local function dirname(path)
    return path:match("^(.*)/[^/]*$")
end

local function mkdir(path)
    local ok, _, code = os.execute("mkdir -p '" .. path:gsub("'", "'\\''") .. "'")
    if not ok or code ~= 0 then fail("cannot create directory: " .. path) end
end

local scene = Importer.LoadScene(saved_path)
if not scene then fail("cannot load saved scene: " .. saved_path) end
Scene.SetActive(scene)

local subject = scene:GetRootNode():FindChildRecursively("UiTestSubject")
if not subject then fail("UiTestSubject missing from saved scene") end

local pos = subject:GetLocalPosition()
if math.abs(pos.x - 1.0) > 1e-4 or math.abs(pos.y - 2.0) > 1e-4 or math.abs(pos.z - 3.0) > 1e-4 then
    fail(string.format("UiTestSubject position (%f, %f, %f) != fixture (1, 2, 3)",
        pos.x, pos.y, pos.z))
end

local light = subject:GetLight()
if not light then fail("UiTestSubject has no Light") end
if light:GetType() ~= LightType.POINT then fail("UiTestSubject light is not POINT") end
if math.abs(light:GetIntensity() - 9.5) > 1e-4 then
    fail(string.format("UiTestSubject intensity %f != 9.5 (editor edit not persisted)",
        light:GetIntensity()))
end

-- Editor-only fixture nodes must never reach the saved file.
if scene:GetRootNode():FindChildRecursively("SmokeTarget") then
    fail("editor-only SmokeTarget leaked into the saved scene")
end
if scene:GetRootNode():FindChildRecursively("EditorCamera") then
    fail("editor-only EditorCamera leaked into the saved scene")
end

-- Semantic round trip: save into the artifact dir, reload, compare
-- persistent fingerprints.
local fp_source = Scene.GetPersistentFingerprint(scene)
local out = output_root .. "/roundtrip/scene.json"
mkdir(dirname(out))
if not FileUtil.SaveByExtension(scene, out) then fail("round-trip save failed") end
local reloaded = Importer.LoadScene(out)
if not reloaded then fail("round-trip reload failed") end
Scene.SetActive(reloaded)
local diffs = Scene.DiffPersistentFingerprints(fp_source, Scene.GetPersistentFingerprint(reloaded))
if #diffs ~= 0 then
    for i, entry in ipairs(diffs) do
        if i <= 8 then
            io.stderr:write(string.format("round-trip diff: %s: %s ~= %s\n",
                entry.path, entry.first, entry.second))
        end
    end
    fail("round-trip semantic diff has " .. #diffs .. " entries")
end

print("ui_saved_scene_verify OK: " .. saved_path)
