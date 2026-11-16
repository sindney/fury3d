local function fail(msg)
    io.stderr:write("cluster_lod_pak FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene") end
local fingerprint = Scene.GetPersistentFingerprint(scene)
if not fingerprint:find('"payload_clusters"', 1, true) then
    fail("packaged mesh did not attach its cluster payload")
end
if not fingerprint:find('"payload_groups"', 1, true) then
    fail("packaged mesh did not attach cluster groups")
end
if not fingerprint:find('"cluster_lod"', 1, true) then
    fail("packaged mesh lost its cluster descriptor")
end
print("cluster_lod_pak: packaged payload attached OK")
