-- tests/lua/ocean_asset_flow.lua - OceanWaves must survive the editor's
-- open-scene path (Importer.LoadScene + MergeInto). Before the fix,
-- MergeInto transferred textures/materials/meshes/clips/particles/heightmaps
-- but NOT OceanWaves: the asset was stranded in the discarded source scene,
-- so the content browser tile and the inspector wave picker came up empty.
--
--   ./fury exec Projects/outdoor/outdoor_water.bin ../tests/lua/ocean_asset_flow.lua
-- (the exec scene arg is required by the CLI but unused - the script builds
-- its own empty scene so the only source of waves is the transfer)

local function fail(msg)
    io.stderr:write("ocean_asset_flow FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.Create("ocean_asset_flow", "", OcTree.Create())
Scene.SetActive(scene)

-- Editor's open-scene path, minus the Lua-side replace helper: load into a
-- temp scene (Scene::Active swaps inside the binding), then merge.
local imported = Importer.LoadScene("Projects/ocean/ocean_lake.bin")
if not imported then fail("failed to load ocean_lake.bin") end
if scene:GetOceanWaves("Engine/Ocean/ocean.json") ~= nil then
    fail("waves leaked into the active scene before the merge")
end
local merged = Importer.MergeInto(scene, imported)
if merged <= 0 then fail("MergeInto merged nothing") end

local waves = scene:GetOceanWaves("Engine/Ocean/ocean.json")
if not waves then
    fail("OceanWaves stranded by MergeInto (picker/browser would be empty)")
end
if not waves:IsValid() then fail("transferred OceanWaves invalid") end
print("ocean_asset_flow: waves transferred, " .. waves:GetFrameCount() .. " frames")
print("ocean_asset_flow: PASS")
