-- tests/lua/cloud_sky_roundtrip.lua - SkyAtmosphere cloud block save/load
-- round-trip + legacy field mapping (change: cloud-quality-veg-prepass, task 7.4).
--
-- Usage (from examples/):
--   ./fury ../tests/lua/cloud_sky_roundtrip.lua
--
-- 1. Loads the unmodified island scene and asserts the legacy 2D-deck cloud
--    fields map onto the volumetric block (alt/thick/fade) while the new
--    fields hold their defaults.
-- 2. Sets every cloud field (legacy + new + cirrus block + TOD) to
--    non-default values, saves to /tmp, reloads, and asserts every getter
--    returns the set value exactly.
-- Exits 0 with "OK" on stderr; exits 1 naming the failing field.

local function fail(msg)
    io.stderr:write("cloud_sky_roundtrip FAIL: " .. msg .. "\n")
    os.exit(1)
end

local OUT_PATH = "/tmp/cloud_sky_roundtrip.bin"

local function find_sky(scene)
    local sky = nil
    Scene.ForEachNode(scene, function(n)
        local s = n:GetSkyAtmosphere()
        if s then sky = s end
    end)
    return sky
end

-- expected values: field name -> {get, want}
-- Lua numbers are doubles; C++ getters promote float32 to double, so compare
-- with a small epsilon (float round-trip noise is ~1e-7 at these magnitudes,
-- anything above that is a real serialization drift).
local function expect_float(sky, label, got, want)
    if math.abs(got - want) > 1e-4 then
        fail(string.format("%s: want %s, got %s", label, tostring(want), tostring(got)))
    end
end

local function on_init()
    local octree = OcTree.Create()
    Scene.SetActive(Scene.Create("cloud_rt", "Projects/ocean/", octree))
    if not Scene.LoadActive("Projects/ocean/ocean_island.bin") then
        fail("failed to load ocean_island.bin")
    end
    local scene = Scene.GetActive()
    local sky = find_sky(scene)
    if not sky then fail("no SkyAtmosphere in island scene") end

    -- -- 1. legacy load: 2D-deck fields map, new fields stay at defaults ----
    expect_float(sky, "legacy.cloud_alt_km", sky:GetCloudAltitudeKm(), 0.15)
    expect_float(sky, "legacy.cloud_thick_km", sky:GetCloudThicknessKm(), 0.12)
    expect_float(sky, "legacy.cloud_fade_km", sky:GetCloudFadeKm(), 2.5)
    expect_float(sky, "default.cloud_type_bias", sky:GetCloudTypeBias(), 0.0)
    expect_float(sky, "default.cloud_detail_scale", sky:GetCloudDetailScale(), 2.4)
    expect_float(sky, "default.cloud_erosion", sky:GetCloudErosion(), 0.5)
    expect_float(sky, "default.cloud_powder", sky:GetCloudPowder(), 1.0)
    expect_float(sky, "default.cloud_hg_g", sky:GetCloudHgG(), 0.2)
    expect_float(sky, "default.cloud_hg_g_fwd", sky:GetCloudHgGFwd(), 0.7)
    expect_float(sky, "default.cloud_hg_blend", sky:GetCloudHgBlend(), 0.5)
    expect_float(sky, "default.cloud_ambient_scale", sky:GetCloudAmbientScale(), 1.0)
    expect_float(sky, "default.cloud_quality", sky:GetCloudQuality(), 1)
    expect_float(sky, "default.cloud_weather_bias", sky:GetCloudWeatherBias(), 0.0)
    expect_float(sky, "default.cloud_weather_type_contrast", sky:GetCloudWeatherTypeContrast(), 1.6)
    if sky:GetCirrusEnabled() ~= true then fail("default.cirrus_enabled") end
    expect_float(sky, "default.cirrus_alt_km", sky:GetCirrusAltKm(), 8.0)
    expect_float(sky, "default.cirrus_density", sky:GetCirrusDensity(), 1.5)
    io.stderr:write("cloud_sky_roundtrip: legacy load + defaults OK\n")

    -- -- 2. set every field non-default, save, reload, assert ----------------
    sky:SetCloudsEnabled(true)
    sky:SetCloudCoverage(0.62)
    sky:SetCloudAltitudeKm(2.25)
    sky:SetCloudThicknessKm(3.75)
    sky:SetCloudScale(0.21)
    sky:SetCloudWindSpeed(432.0)
    sky:SetCloudDensity(27.0)
    sky:SetCloudFadeKm(55.0)
    sky:SetCloudTypeBias(0.65)
    sky:SetCloudDetailScale(5.5)
    sky:SetCloudErosion(0.85)
    sky:SetCloudPowder(0.35)
    sky:SetCloudHgG(0.45)
    sky:SetCloudHgGFwd(0.82)
    sky:SetCloudHgBlend(0.25)
    sky:SetCloudAmbientScale(1.6)
    sky:SetCloudQuality(2)
    sky:SetCloudDebugMode(1)
    sky:SetCloudWeatherBias(0.18)
    sky:SetCloudWeatherTypeContrast(3.2)
    sky:SetCirrusEnabled(false)
    sky:SetCirrusCoverage(0.7)
    sky:SetCirrusAltKm(11.5)
    sky:SetCirrusScale(0.05)
    sky:SetCirrusDensity(2.8)
    sky:SetTimeHours(17.5)
    sky:SetDayLengthMinutes(22.0)
    sky:SetAutoAdvance(true)
    sky:SetSunFromTod(false)
    sky:SetSunLightName("RoundTripSun")

    if not Scene.SaveActive(OUT_PATH) then fail("SaveActive failed: " .. OUT_PATH) end
    if not Scene.LoadActive(OUT_PATH) then fail("reload failed: " .. OUT_PATH) end

    local sky2 = find_sky(Scene.GetActive())
    if not sky2 then fail("no SkyAtmosphere after reload") end

    if sky2:GetCloudsEnabled() ~= true then fail("clouds_enabled") end
    expect_float(sky2, "cloud_coverage", sky2:GetCloudCoverage(), 0.62)
    expect_float(sky2, "cloud_alt_km", sky2:GetCloudAltitudeKm(), 2.25)
    expect_float(sky2, "cloud_thick_km", sky2:GetCloudThicknessKm(), 3.75)
    expect_float(sky2, "cloud_scale", sky2:GetCloudScale(), 0.21)
    expect_float(sky2, "cloud_wind_speed", sky2:GetCloudWindSpeed(), 432.0)
    expect_float(sky2, "cloud_density", sky2:GetCloudDensity(), 27.0)
    expect_float(sky2, "cloud_fade_km", sky2:GetCloudFadeKm(), 55.0)
    expect_float(sky2, "cloud_type_bias", sky2:GetCloudTypeBias(), 0.65)
    expect_float(sky2, "cloud_detail_scale", sky2:GetCloudDetailScale(), 5.5)
    expect_float(sky2, "cloud_erosion", sky2:GetCloudErosion(), 0.85)
    expect_float(sky2, "cloud_powder", sky2:GetCloudPowder(), 0.35)
    expect_float(sky2, "cloud_hg_g", sky2:GetCloudHgG(), 0.45)
    expect_float(sky2, "cloud_hg_g_fwd", sky2:GetCloudHgGFwd(), 0.82)
    expect_float(sky2, "cloud_hg_blend", sky2:GetCloudHgBlend(), 0.25)
    expect_float(sky2, "cloud_ambient_scale", sky2:GetCloudAmbientScale(), 1.6)
    expect_float(sky2, "cloud_quality", sky2:GetCloudQuality(), 2)
    expect_float(sky2, "cloud_debug_mode", sky2:GetCloudDebugMode(), 1)
    expect_float(sky2, "cloud_weather_bias", sky2:GetCloudWeatherBias(), 0.18)
    expect_float(sky2, "cloud_weather_type_contrast", sky2:GetCloudWeatherTypeContrast(), 3.2)
    if sky2:GetCirrusEnabled() ~= false then fail("cirrus_enabled") end
    expect_float(sky2, "cirrus_coverage", sky2:GetCirrusCoverage(), 0.7)
    expect_float(sky2, "cirrus_alt_km", sky2:GetCirrusAltKm(), 11.5)
    expect_float(sky2, "cirrus_scale", sky2:GetCirrusScale(), 0.05)
    expect_float(sky2, "cirrus_density", sky2:GetCirrusDensity(), 2.8)
    expect_float(sky2, "time_hours", sky2:GetTimeHours(), 17.5)
    expect_float(sky2, "day_length_minutes", sky2:GetDayLengthMinutes(), 22.0)
    if sky2:GetAutoAdvance() ~= true then fail("auto_advance") end
    if sky2:GetSunFromTod() ~= false then fail("sun_from_tod") end
    if sky2:GetSunLightName() ~= "RoundTripSun" then fail("sun_light_name") end

    io.stderr:write("cloud_sky_roundtrip OK\n")
    os.exit(0)
end

Engine.run({ on_init = on_init, on_update = function() end })
