-- tests/lua/kraut_import_roundtrip.lua - verifies a Kraut-imported tree
-- scene fragment (change: add-kraut-vegetation, task 4.3).
--
-- Usage:
--   ./fury exec /tmp/kraut_gen/PalmTree2.bin ../tests/lua/kraut_import_roundtrip.lua
--
-- Asserts on the imported scene (kraut postprocess applied):
--   * LOD chain: 4 mesh tiers + billboard terminal tier
--   * thresholds come from asset.extras.kraut (non-increasing, < 1 after tier 1)
--   * billboard flag on the terminal tier only
--   * COLOR_0 vertex colors populated (wind weights)
--   * materials: foliage two-sided + wind-enabled
--   * save -> reload keeps the chain intact

local function fail(msg)
    io.stderr:write("kraut_import_roundtrip FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene") end

-- Find the LOD0 mesh (the chain root) by name suffix.
local lod0 = nil
Scene.ForEachMesh(scene, function(m)
    if m:GetName():match("_LOD0$") then lod0 = m end
end)

if not lod0 then fail("no *_LOD0 mesh found") end
-- the billboard lives inline in the chain (LOD tiers are not registered
-- as separate entities); the terminal tier IS the billboard mesh

local tiers = lod0:GetLodCount()
print("tiers=" .. tiers)
if tiers ~= 5 then fail("expected 5 tiers (4 mesh + billboard), got " .. tiers) end

-- Billboard flag: terminal tier only.
for i = 0, tiers - 1 do
    local isBb = lod0:IsLodBillboard(i)
    if i < tiers - 1 and isBb then fail("tier " .. i .. " wrongly flagged billboard") end
    if i == tiers - 1 and not isBb then fail("terminal tier not flagged billboard") end
end

-- Thresholds from extras.kraut (PalmTree2: non-increasing, last < 0.5).
local t1 = lod0:GetLodThreshold(1)
local tLast = lod0:GetLodThreshold(tiers - 1)
print("thresholds: t1=" .. t1 .. " tLast=" .. tLast)
if t1 < 0.99 then fail("t1 should be ~1.0 (8m clamp), got " .. t1) end
if tLast >= 0.5 then fail("billboard threshold too high: " .. tLast) end
for i = 2, tiers - 1 do
    if lod0:GetLodThreshold(i) > lod0:GetLodThreshold(i - 1) then
        fail("thresholds not non-increasing at tier " .. i)
    end
end

-- Wind weights: LOD0 has vertex colors, one vec4 per vertex.
local colors = lod0:GetColorCount()
local positions = #lod0:GetPositions() / 3
print("LOD0 colors=" .. colors .. " verts=" .. positions)
if colors ~= positions then fail("COLOR_0 count mismatch: " .. colors .. " vs " .. positions) end
if colors == 0 then fail("no vertex colors imported") end

-- Materials: at least one MASK material with TwoSided+Wind; all MESH-tier
-- materials wind-enabled (the billboard material deliberately stays rigid --
-- swaying a camera-facing quad reads as sliding, not wind).
local maskSeen = false
local windAll = true
local matCount = 0
Scene.ForEachMaterial(scene, function(mat)
    matCount = matCount + 1
    local isBillboard = mat:GetName():match("billboard") ~= nil
    if not isBillboard and not mat:GetWindEnabled() then windAll = false end
    if isBillboard and mat:GetWindEnabled() then fail("billboard material must not be wind-enabled") end
    if mat:GetAlphaMode() == 1 then -- MASK
        maskSeen = true
        if not mat:GetTwoSided() then fail("MASK material '" .. mat:GetName() .. "' not two-sided") end
    end
end)
print("materials=" .. matCount)
if not maskSeen then fail("no MASK (foliage) material found") end
if not windAll then fail("not all mesh-tier materials are wind-enabled") end

-- Round-trip: save and reload, chain intact.
if not Scene.SaveActive("/tmp/kraut_gen/PalmTree2_rt.bin") then
    fail("re-save failed")
end
if not Scene.LoadActive("/tmp/kraut_gen/PalmTree2_rt.bin") then
    fail("reload failed")
end
scene = Scene.GetActive()
lod0 = nil
Scene.ForEachMesh(scene, function(m)
    if m:GetName():match("_LOD0$") then lod0 = m end
end)
if not lod0 then fail("LOD0 lost on reload") end
if lod0:GetLodCount() ~= 5 then fail("chain lost on reload: " .. lod0:GetLodCount()) end
if not lod0:IsLodBillboard(4) then fail("billboard flag lost on reload") end
if not lod0:GetLodMesh(4) then fail("billboard mesh lost on reload") end

print("kraut_import_roundtrip OK")
