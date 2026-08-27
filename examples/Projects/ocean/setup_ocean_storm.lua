-- setup_ocean_storm.lua - derive ocean_storm.bin from ocean_infinite.bin
-- (change: ocean-round2-polish, task 9.3). NEVER writes the source.
-- Re-runnable: always starts from ocean_infinite.bin.
--
--   ./fury exec Projects/ocean/ocean_infinite.bin Projects/ocean/setup_ocean_storm.lua
--
-- The calm demo ocean becomes a storm demo: the project-local OceanStorm
-- bake (22 m/s wind, 2.5 m swell, choppiness 1.8 - crest foam everywhere)
-- plus matching spectrum fields (so a GPU regen on GL 4.3+ lands in the
-- same sea state) and the shader's wind-driven crest coverage. The 8
-- buoyant props from the infinite scene ride the heavy swell.

local function fail(msg)
    io.stderr:write("setup_ocean_storm FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene (exec must load ocean_infinite.bin)") end
local root = scene:GetRootNode()

local ocean_node = root:FindChildRecursively("Ocean")
if not ocean_node then fail("ocean_infinite.bin has no Ocean node") end
local ocean = ocean_node:GetOceanComponent()
if not ocean then fail("Ocean node has no OceanComponent") end

ocean:SetWaveAssetPath("OceanStorm/ocean.json") -- scene working dir is Projects/ocean/
-- spectrum fields mirror the storm bake so a GPU regen (Auto) matches, and
-- windSpeed drives the shader's crest-foam coverage modulation
ocean:SetWindSpeed(2200.0)
ocean:SetFetchCm(200000.0)
ocean:SetChoppiness(1.8)
ocean:SetSwellTileCm(15000.0)
ocean:SetFoamAmount(0.8)

-- a storm sea is no mirror: SSR's screen-space sky mirror fights the rough
-- water read (the analytic sky reflection carries it instead)
local rs = scene:GetRenderSettings()
for i, entry in ipairs(rs:GetChain()) do
    if entry.effectName == "SSR" then rs:SetEffectEnabled(i - 1, false) end
end

-- force the lazy re-resolve (GetWaves triggers it) before asserting
local waves = ocean:GetWaves()
if ocean:GetResolvedSource() ~= 1 then
    fail("storm ocean did not resolve baked: " .. ocean:GetResolvedReason())
end
if not waves or not waves:IsValid() then fail("storm waves invalid") end
print("setup_ocean_storm: storm bake loaded, " .. waves:GetFrameCount() .. " frames")

root:Recompose(true)

local out = "Projects/ocean/ocean_storm.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(out)) then
    fail("save failed: " .. out)
end
print("setup_ocean_storm: wrote " .. out)
print("setup_ocean_storm: DONE")
