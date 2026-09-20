-- tests/lua/pak_scene_load.lua - a pak-loaded scene must match its loose
-- counterpart. Runs identically under both mounts:
--
--   cd examples
--   ./fury exec Projects/ocean/ocean_island.bin ../tests/lua/pak_scene_load.lua [expect_tex] [expect_nodes] [known_tex]
--   ./fury exec Projects/ocean/ocean_island.pak ../tests/lua/pak_scene_load.lua [expect_tex] [expect_nodes] [known_tex]
--
-- and against the tests/pak_pipeline.sh synthetic project:
--
--   ./fury exec scene.json|scene.pak ../tests/lua/pak_scene_load.lua 3 4 sky_moon.png
--
-- Args:
--   arg[1] expected texture count (default 14, ocean_island EM unique paths)
--   arg[2] expected node count    (default 267)
--   arg[3] known texture name    (default "Palm1.tga"; path-created textures
--                                 carry GetName == GetFilePath)
--
-- Asserts (same enumeration APIs as asset_path_identity.lua):
--   1. Scene.ForEachTexture yields exactly arg[1] entries, unique non-empty
--      GetFilePath values.
--   2. Scene.GetTexture(arg[3]) resolves the canonical entry at that path.
--   3. Scene.ForEachNode yields exactly arg[2] nodes (must be > 0).
--
-- Texture Width/Height are not reachable from Lua (the Texture usertype
-- binds only GetName/GetFilePath/SetFilePathAndSRGB); dims are verified by
-- tests/pak_pipeline.sh from the cook manifest and the cooked KTX2 headers.

local function fail(msg)
    io.stderr:write("pak_scene_load FAIL: " .. msg .. "\n")
    os.exit(1)
end

local EXPECT_TEX = tonumber(arg and arg[1]) or 14
local EXPECT_NODES = tonumber(arg and arg[2]) or 267
local KNOWN = (arg and arg[3]) or "Palm1.tga"

if EXPECT_NODES <= 0 then fail("expected node count must be > 0") end

local scene = Scene.GetActive()
if not scene then fail("Scene.GetActive() returned nil") end

local n, seen = 0, {}
scene:ForEachTexture(function(t)
    n = n + 1
    local p = t:GetFilePath()
    if p == "" then fail("texture '" .. t:GetName() .. "' has an empty path") end
    if seen[p] then fail("ForEachTexture yielded duplicate path '" .. p .. "'") end
    seen[p] = true
end)
if n ~= EXPECT_TEX then
    fail("expected " .. EXPECT_TEX .. " textures, got " .. n)
end

local known = scene:GetTexture(KNOWN)
if not known then fail("Scene.GetTexture('" .. KNOWN .. "') returned nil") end
if known:GetFilePath() ~= KNOWN then
    fail("GetTexture('" .. KNOWN .. "') returned entry at '" .. known:GetFilePath() .. "'")
end

local nodes = 0
scene:ForEachNode(function(nd) nodes = nodes + 1 end)
if nodes ~= EXPECT_NODES then
    fail("expected " .. EXPECT_NODES .. " nodes, got " .. nodes)
end

print("pak_scene_load: " .. n .. " textures, " .. nodes ..
    " nodes, known '" .. KNOWN .. "' resolved")
print("pak_scene_load: PASS")
os.exit(0)
