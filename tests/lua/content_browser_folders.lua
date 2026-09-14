-- tests/lua/content_browser_folders.lua — folder-scoped asset identity.
--
-- Writes a synthetic scene whose Terrain textures live under one known
-- folder (tests/lua/fixtures/terrain_mini/), with the top-level textures
-- array carrying a deliberate same-path duplicate. Loading must collapse
-- the duplicate (path is the asset identity), Scene.ForEachTexture must
-- iterate exactly the unique paths, and Scene.GetTexture(path) must
-- return the canonical entry for each. The invariants must hold again
-- after a save+reload cycle, and the Terrain component must survive the
-- round-trip with its foldered texture paths intact.
--
--   ./fury exec Projects/tank/scene.bin ../tests/lua/content_browser_folders.lua
-- (the exec scene arg is required by the CLI but unused - the script
-- builds its own scene file)

local function fail(msg)
    io.stderr:write("content_browser_folders FAIL: " .. msg .. "\n")
    os.exit(1)
end

local DIR = "../tests/lua/fixtures/terrain_mini/"
local EXPECT = {
    [DIR .. "splat.png"] = true,
    [DIR .. "grass.png"] = true,
    [DIR .. "rock.png"] = true,
    [DIR .. "mud.png"] = true,
}
local EXPECT_COUNT = 4

-- written next to the launch cwd so the fixture-relative paths resolve
local SRC = "content_browser_folders_src.json"
local RT = "content_browser_folders_rt.bin"

local function tex(path, srgb)
    return string.format('{"name": "t", "path": "%s", "srgb": %s}', path, tostring(srgb))
end

local function layer(i, name, file)
    return string.format(
        '"layer%d_name": "%s", "layer%d_texture": "%s", "layer%d_tiling": 100.0',
        i, name, i, DIR .. file, i)
end

local scene_json = [[{
  "name": "content_browser_folders",
  "version": 3,
  "textures": [
    ]] .. table.concat({
        tex(DIR .. "splat.png", false),
        tex(DIR .. "grass.png", true),
        tex(DIR .. "rock.png", true),
        tex(DIR .. "mud.png", true),
        tex(DIR .. "grass.png", true), -- deliberate same-path duplicate
    }, ",\n    ") .. [[
  ],
  "materials": [],
  "meshes": [],
  "nodes": {
    "name": "RootNode", "pos": [0.0,0.0,0.0,1.0], "rot": [0.0,0.0,0.0,1.0], "scl": [1.0,1.0,1.0,1.0],
    "components": [],
    "childs": [
      {
        "name": "Ground", "pos": [0.0,0.0,0.0,1.0], "rot": [0.0,0.0,0.0,1.0], "scl": [1.0,1.0,1.0,1.0],
        "components": [
          {
            "type": "Terrain",
            "heightmap": "]] .. DIR .. [[height.r16",
            "splatmap": "]] .. DIR .. [[splat.png",
            "chunk_count": 4, "lod_count": 2,
            ]] .. layer(0, "grass", "grass.png") .. [[,
            ]] .. layer(1, "rock", "rock.png") .. [[,
            ]] .. layer(2, "mud", "mud.png") .. [[,
            ]] .. layer(3, "snow", "grass.png") .. [[

          }
        ],
        "childs": []
      }
    ]
  }
}]]

local f = io.open(SRC, "w")
if not f then fail("cannot write " .. SRC) end
f:write(scene_json)
f:close()

local scene = Importer.LoadScene(SRC)
if not scene then fail("failed to load synthetic scene") end
Scene.SetActive(scene)

local function check(scene_obj, phase)
    local n, seen = 0, {}
    scene_obj:ForEachTexture(function(t)
        n = n + 1
        local p = t:GetFilePath()
        if seen[p] then
            fail(phase .. ": ForEachTexture yielded duplicate path '" .. p .. "'")
        end
        seen[p] = true
    end)
    if n ~= EXPECT_COUNT then
        fail(phase .. ": expected " .. EXPECT_COUNT .. " textures, got " .. n)
    end
    for p in pairs(EXPECT) do
        if not seen[p] then
            fail(phase .. ": missing texture '" .. p .. "'")
        end
        local t = scene_obj:GetTexture(p)
        if not t then
            fail(phase .. ": GetTexture('" .. p .. "') returned nil")
        end
        if t:GetFilePath() ~= p then
            fail(phase .. ": GetTexture('" .. p .. "') returned entry at '"
                .. t:GetFilePath() .. "'")
        end
    end
end

check(scene, "post-load")

-- SaveByExtension relocates file-backed textures: each source image is
-- copied next to the output and the stored path rewritten to a bare
-- filename (keeps saved scenes portable). The reloaded EM therefore keys
-- the same four assets under their basenames.
local RELOCATED = {
    ["splat.png"] = true,
    ["grass.png"] = true,
    ["rock.png"] = true,
    ["mud.png"] = true,
}

if not FileUtil.SaveByExtension(scene, RT) then fail("save failed") end
local reloaded = Importer.LoadScene(RT)
if not reloaded then fail("reload failed") end
Scene.SetActive(reloaded)

do
    local n, seen = 0, {}
    reloaded:ForEachTexture(function(t)
        n = n + 1
        local p = t:GetFilePath()
        if seen[p] then
            fail("post-reload: ForEachTexture yielded duplicate path '" .. p .. "'")
        end
        seen[p] = true
    end)
    if n ~= EXPECT_COUNT then
        fail("post-reload: expected " .. EXPECT_COUNT .. " textures, got " .. n)
    end
    for p in pairs(RELOCATED) do
        if not seen[p] then
            fail("post-reload: missing relocated texture '" .. p .. "'")
        end
        if not reloaded:GetTexture(p) then
            fail("post-reload: GetTexture('" .. p .. "') returned nil")
        end
    end
end

local terrain
reloaded:ForEachNode(function(n)
    local t = n:GetTerrain()
    if t then terrain = t return true end
end)
if not terrain then fail("reloaded scene lost its Terrain") end
if terrain:GetSplatmapPath() ~= DIR .. "splat.png" then
    fail("terrain splatmap path changed across round-trip: " .. terrain:GetSplatmapPath())
end

os.remove(SRC)
os.remove(RT)
for p in pairs(RELOCATED) do os.remove(p) end
print("content_browser_folders: " .. EXPECT_COUNT ..
    " canonical textures under " .. DIR .. " (same-path duplicate collapsed)")
print("content_browser_folders: PASS")
os.exit(0)
