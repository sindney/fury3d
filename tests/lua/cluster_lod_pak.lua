local function fail(msg)
    io.stderr:write("cluster_lod_pak FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene") end
local attached = false
scene:ForEachMesh(function(mesh)
    local state = ClusterLodTest.Inspect(mesh)
    if state.has_descriptor and state.has_payload and state.cluster_count > 0 then
        attached = true
    end
end)
if not attached then fail("packaged mesh did not attach its cluster descriptor and payload") end
print("cluster_lod_pak: packaged payload attached OK")
