-- tests/lua/ocean_waves_spec.lua - headless spec test for the OceanWaves
-- asset + WaveSampler (change: add-fft-ocean, tasks 3.1-3.4).
--
-- Loads the mini fixture (tests/lua/fixtures/ocean_mini, 32^2 x 8 frames)
-- and asserts the wave-data spec scenarios: loop/tile continuity,
-- determinism across loads, choppy-corrected divergence, and CPU-vs-baked
-- payload agreement at exact texel centers.
--
-- Usage:
--   ./fury exec Projects/outdoor/outdoor_water.bin tests/lua/ocean_waves_spec.lua
-- (the exec scene arg is required by the CLI but unused - the script builds
-- its own scene)

local function fail(msg)
    io.stderr:write("ocean_waves_spec FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.Create("ocean_waves_spec", "", OcTree.Create())
Scene.SetActive(scene)

local waves = OceanWaves.Create("mini")
waves:SetFilePath("../tests/lua/fixtures/ocean_mini/ocean.json")
if not waves:LoadWaves() then fail("fixture did not load") end
if not waves:IsValid() then fail("fixture invalid after load") end

local frames = waves:GetFrameCount()
local loop = waves:GetLoopSeconds()
if frames ~= 8 then fail("frames " .. frames .. " != 8") end
if math.abs(loop - 4.0) > 1e-6 then fail("loop " .. loop .. " != 4") end
if waves:GetBandCount() ~= 2 then fail("expected 2 bands") end

-- loop continuity: t=0 and t=loop must sample identically (frame wrap)
for _, x in ipairs({0.0, 137.5, 999.25}) do
    for _, z in ipairs({0.0, -55.0, 640.5}) do
        local a = WaveSampler.Height(waves, x, z, 0.0)
        local b = WaveSampler.Height(waves, x, z, loop)
        if math.abs(a - b) > 1e-3 then
            fail(string.format("loop wrap mismatch at (%.1f,%.1f): %.4f vs %.4f", x, z, a, b))
        end
    end
end

-- tile continuity: shifting by exactly one swell tile wraps identically
local tile = 2000.0
for _, t in ipairs({0.0, 1.7, 3.9}) do
    local a = WaveSampler.Height(waves, 100.0, 200.0, t)
    local b = WaveSampler.Height(waves, 100.0 + tile, 200.0, t)
    local c = WaveSampler.Height(waves, 100.0, 200.0 - tile, t)
    if math.abs(a - b) > 1e-3 or math.abs(a - c) > 1e-3 then
        fail(string.format("tile wrap mismatch at t=%.2f: %.4f %.4f %.4f", t, a, b, c))
    end
end

-- determinism: a second load of the same file samples identically
local waves2 = OceanWaves.Create("mini2")
waves2:SetFilePath("../tests/lua/fixtures/ocean_mini/ocean.json")
if not waves2:LoadWaves() then fail("second load failed") end
for _, t in ipairs({0.0, 0.4, 2.3}) do
    local a = WaveSampler.Height(waves, 33.0, -71.0, t)
    local b = WaveSampler.Height(waves2, 33.0, -71.0, t)
    if a ~= b then fail(string.format("nondeterministic reload at t=%.2f", t)) end
end

-- shared cache: Resolve returns one instance per path
local r1 = OceanWaves.Resolve("../tests/lua/fixtures/ocean_mini/ocean.json")
local r2 = OceanWaves.Resolve("../tests/lua/fixtures/ocean_mini/ocean.json")
if not r1 or not r2 then fail("Resolve returned nil") end
if r1 ~= r2 then fail("Resolve did not share the cached instance") end

-- choppy-corrected query: terminates and diverges from the plain query
-- somewhere on the tile (choppiness > 0 in the fixture)
local maxDiff = 0.0
for i = 0, 15 do
    for j = 0, 15 do
        local x = 20.0 + i * 120.0
        local z = -35.0 + j * 120.0
        local plain = WaveSampler.Height(waves, x, z, 1.3)
        local corr = WaveSampler.HeightChoppyCorrected(waves, x, z, 1.3)
        maxDiff = math.max(maxDiff, math.abs(plain - corr))
    end
end
if maxDiff < 1e-4 then fail("choppy-corrected query never diverges") end
print(string.format("ocean_waves_spec: max choppy correction %.3f cm", maxDiff))

-- CPU-vs-payload agreement: at exact texel centers + integer frames the
-- sampler must return the baked texels. The sampler sums both bands, so
-- pick swell texel centers that are also ripple texel centers: ripple tile
-- is 400cm/32 vs swell 2000cm/32, so j_ripple = (5*j_swell + 2) % 32.
local function texel_x(j) return (j + 0.5) / 32 * 2000.0 end
local function texel_z(i) return (i + 0.5) / 32 * 2000.0 end
for _, ij in ipairs({{3, 5}, {17, 28}, {31, 0}}) do
    local i, j = ij[1], ij[2]
    local ir, jr = (5 * i + 2) % 32, (5 * j + 2) % 32
    for _, f in ipairs({0, 3, 7}) do
        local t = f / 8 * 4.0
        local want = waves:GetDispValue(1, f, i, j, 1)
            + waves:GetDispValue(0, f, ir, jr, 1)
        -- texel-center sampling still lerps frames; t at exact frame -> tl=0
        local got = WaveSampler.Height(waves, texel_x(j), texel_z(i), t)
        if math.abs(got - want) > 0.05 then
            fail(string.format("payload mismatch f%d (%d,%d): %.3f vs %.3f",
                f, i, j, got, want))
        end
    end
end

-- flat fallback: an invalid asset samples as zero, no crash
local bad = OceanWaves.Create("bad")
bad:SetFilePath("../tests/lua/fixtures/ocean_mini/nope.json")
if bad:LoadWaves() then fail("missing file should fail to load") end
if WaveSampler.Height(bad, 1.0, 2.0, 3.0) ~= 0.0 then fail("invalid asset not flat") end

print("ocean_waves_spec: OK")
