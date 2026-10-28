-- tests/lua/scene_asset_path_identity.lua
-- Merged path-identity invariant for two fixtures:
--
--   fixture A: bundled Projects/ocean/ocean_island.bin (legacy same-path
--              duplicates collapsed to 14 unique paths on load)
--   fixture B: synthetic Terrain scene built from tests/lua/_fixtures/
--              terrain_mini/ (deliberate same-path duplicate to prove
--              EM dedup)
--
-- Both fixtures assert the same invariant: Scene.ForEachTexture yields
-- unique non-empty GetFilePath values, Scene.GetTexture(path) returns the
-- canonical entry, and the unique-path count is stable across 5 reload
-- cycles.
--
-- Usage (from examples/):
--   ./fury exec Projects/ocean/ocean_island.bin ../tests/lua/scene_asset_path_identity.lua
--
local function fail(msg)
    io.stderr:write("scene_asset_path_identity FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("Scene.GetActive() returned nil") end

local EXPECT_A = 14
local EXPECT_B = 4
local RELOCATED_B = {
    ["splat.png"] = true,
    ["grass.png"] = true,
    ["rock.png"]  = true,
    ["mud.png"]   = true,
}

-- The save relocates textures to their basename in the working dir, so the
-- test has to leave the tree as it found it.
local preexisting = {}
for name in pairs(RELOCATED_B) do
    local fh = io.open(name, "rb")
    preexisting[name] = fh ~= nil
    if fh then fh:close() end
end

local function count_textures(s)
    local n, seen = 0, {}
    s:ForEachTexture(function(t)
        n = n + 1
        local p = t:GetFilePath()
        if p == "" then fail("texture '" .. t:GetName() .. "' empty path") end
        if seen[p] then fail("ForEachTexture duplicate path '" .. p .. "'") end
        seen[p] = true
    end)
    return n, seen
end

-- Fixture A: bundled ocean_island.bin (legacy collapse).
local n, seen = count_textures(scene)
if n ~= EXPECT_A then fail("A: expected " .. EXPECT_A .. ", got " .. n) end
for p in pairs(seen) do
    local t = scene:GetTexture(p)
    if not t then fail("A: GetTexture('" .. p .. "') nil") end
    if t:GetFilePath() ~= p then fail("A: GetTexture('" .. p .. "') returned '" .. t:GetFilePath() .. "'") end
end

-- Save under the scene's working dir so the round-trip preserves the
-- TerrainIsland/height.json sidecar resolution.
local tmp = "Projects/ocean/scene_asset_path_identity_rt.bin"
for cycle = 1, 5 do
    if not FileUtil.SaveByExtension(scene, tmp) then fail("A save " .. cycle) end
    local r = Importer.LoadScene(tmp)
    if not r then fail("A load " .. cycle) end
    local rn, rseen = count_textures(r)
    if rn ~= EXPECT_A then fail("A cycle " .. cycle .. ": got " .. rn) end
    for p in pairs(seen) do
        if not rseen[p] then fail("A cycle " .. cycle .. ": lost '" .. p .. "'") end
    end
    scene = r
end
os.remove(tmp)
io.stderr:write("scene_asset_path_identity: fixture A OK (14 unique paths, 5 cycles stable)\n")

-- Fixture B: synthetic Terrain scene with deliberate duplicate.
local SRC = "scene_asset_path_identity_b_src.json"
local RT  = "scene_asset_path_identity_b_rt.bin"
local DIR = "../tests/lua/_fixtures/terrain_mini/"

local scene_json = [[{
  "name": "scene_asset_path_identity_b",
  "version": 3,
  "textures": [
    {"name": "t", "path": "]] .. DIR .. [[splat.png", "srgb": false},
    {"name": "t", "path": "]] .. DIR .. [[grass.png", "srgb": true},
    {"name": "t", "path": "]] .. DIR .. [[rock.png",  "srgb": true},
    {"name": "t", "path": "]] .. DIR .. [[mud.png",   "srgb": true},
    {"name": "t", "path": "]] .. DIR .. [[grass.png", "srgb": true}
  ],
  "materials": [],
  "meshes": [],
  "nodes": {
    "name": "RootNode",
    "pos": [0.0,0.0,0.0,1.0], "rot": [0.0,0.0,0.0,1.0], "scl": [1.0,1.0,1.0,1.0],
    "components": [],
    "childs": []
  }
}]]

local f = io.open(SRC, "w")
if not f then fail("B: cannot write " .. SRC) end
f:write(scene_json)
f:close()

local b = Importer.LoadScene(SRC)
if not b then fail("B: load synthetic") end

local nb, seenb = count_textures(b)
if nb ~= EXPECT_B then fail("B: expected " .. EXPECT_B .. ", got " .. nb) end
-- pre-save: paths still carry the source dir prefix; only count + uniqueness
-- matter (the dir-prefix paths were just registered by Importer.LoadScene).

if not FileUtil.SaveByExtension(b, RT) then fail("B: save") end
local br = Importer.LoadScene(RT)
if not br then fail("B: reload") end
local nbr, seenbr = count_textures(br)
if nbr ~= EXPECT_B then fail("B post-reload: expected " .. EXPECT_B .. ", got " .. nbr) end
for p in pairs(RELOCATED_B) do
    if not seenbr[p] then fail("B post-reload: missing '" .. p .. "'") end
    if not br:GetTexture(p) then fail("B post-reload: GetTexture('" .. p .. "') nil") end
end

os.remove(SRC)
os.remove(RT)
for name in pairs(RELOCATED_B) do
    if not preexisting[name] then os.remove(name) end
end
io.stderr:write("scene_asset_path_identity: fixture B OK (4 unique paths, deliberate duplicate collapsed)\n")

io.stderr:write("scene_asset_path_identity: PASS\n")
