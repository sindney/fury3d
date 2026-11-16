local function fail(msg)
    io.stderr:write("cluster_lod_import FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene") end
local fingerprint = Scene.GetPersistentFingerprint(scene)
if not fingerprint:find('"payload_clusters"', 1, true) then
    fail("import did not attach cluster payloads")
end
local ddc_root = os.getenv("FURY_DDC") or "DDC"
local meshes = {}
scene:ForEachMesh(function(mesh) meshes[#meshes + 1] = mesh end)
if #meshes == 0 then fail("import produced no meshes") end
local first
for _, mesh in ipairs(meshes) do
    local prepared = ClusterLodTest.Prepare(mesh, ddc_root)
    if not prepared.ok then fail("cache reload failed: " .. tostring(prepared.error)) end
    if prepared.key then
        if not prepared.cache_hit then fail("unchanged import did not hit the payload cache") end
        first = first or mesh
    end
end
if not first then fail("import produced no eligible cluster mesh") end

local normals = first:GetNormals()
local changed = {}
for i, value in ipairs(normals) do changed[i] = value end
if #changed == 0 then
    changed = { 0, 0, 1 }
else
    changed[1] = changed[1] + 0.125
end
first:SetNormals(changed)
local rebuilt = ClusterLodTest.Prepare(first, ddc_root)
if not rebuilt.ok then fail("attribute-change rebuild failed: " .. tostring(rebuilt.error)) end
if rebuilt.cache_hit then fail("attribute-only edit did not invalidate the payload cache") end
first:SetNormals(normals)
local restored = ClusterLodTest.Prepare(first, ddc_root)
if not restored.ok or not restored.cache_hit then fail("restored source did not reuse its payload") end
print("cluster_lod_import: automatic build, cache hit, attribute invalidation OK")
