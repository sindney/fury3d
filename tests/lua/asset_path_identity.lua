-- tests/lua/asset_path_identity.lua — path-keyed EntityManager invariants.
--
-- 1. Legacy same-path duplicates collapse to one canonical entry on load
--    (the bundled ocean_island.bin carries 30 top-level texture entries at
--    14 unique paths -> the EM holds exactly 14 after load).
-- 2. ForEachTexture can never yield two entries at the same path.
-- 3. Scene.GetTexture(path) returns the canonical entry (an entry that IS
--    at that path).
-- 4. Save+reload cycles keep the unique-path count stable (14 -> 14).
--
--   ./fury exec Projects/ocean/ocean_island.bin ../tests/lua/asset_path_identity.lua

local function fail(msg)
    io.stderr:write("asset_path_identity FAIL: " .. msg .. "\n")
    os.exit(1)
end

local EXPECT = 14

local function count_textures(scene)
    local n, seen = 0, {}
    scene:ForEachTexture(function(t)
        n = n + 1
        local p = t:GetFilePath()
        if seen[p] then
            fail("ForEachTexture yielded duplicate path '" .. p .. "'")
        end
        seen[p] = true
    end)
    return n, seen
end

local scene = Scene.GetActive()
if not scene then fail("Scene.GetActive() returned nil") end

local n, seen = count_textures(scene)
if n ~= EXPECT then
    fail("expected " .. EXPECT .. " unique-path textures after legacy load, got " .. n)
end

for p in pairs(seen) do
    local t = scene:GetTexture(p)
    if not t then fail("GetTexture('" .. p .. "') returned nil") end
    if t:GetFilePath() ~= p then
        fail("GetTexture('" .. p .. "') returned entry at '" .. t:GetFilePath() .. "'")
    end
end

local tmp = "/tmp/asset_path_identity_rt.bin"
for cycle = 1, 5 do
    if not FileUtil.SaveByExtension(scene, tmp) then
        fail("save cycle " .. cycle .. " failed")
    end
    local reloaded = Importer.LoadScene(tmp)
    if not reloaded then fail("reload cycle " .. cycle .. " failed") end
    local rn, rseen = count_textures(reloaded)
    if rn ~= EXPECT then
        fail("cycle " .. cycle .. ": expected " .. EXPECT .. " textures, got " .. rn)
    end
    for p in pairs(seen) do
        if not rseen[p] then
            fail("cycle " .. cycle .. ": lost texture path '" .. p .. "'")
        end
    end
    scene = reloaded
end

print("asset_path_identity: legacy load collapsed to " .. EXPECT ..
    " unique paths, stable across 5 reload cycles")
print("asset_path_identity: PASS")
os.exit(0)
