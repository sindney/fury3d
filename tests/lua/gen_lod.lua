-- tests/lua/gen_lod.lua — exercise MeshSimplifier.SimplifyMesh + Mesh.SetLodMeshes
-- bindings end-to-end. Genuinely new — no existing CLI feature generates LODs.
--
-- Selection:
--   arg[1] = mesh name to target, OR "--all" to process every mesh in the scene
--   arg[2] = optional output path (if omitted, just logs per-mesh counts)
--
-- Usage:
--   ./fury exec scene.json tests/lua/gen_lod.lua --all /tmp/out.json
--   ./fury exec scene.json tests/lua/gen_lod.lua TankMesh /tmp/out.json
--   ./fury exec scene.json tests/lua/gen_lod.lua --all   # read-only, no save

local target = arg[1]
local out_path = arg[2]

if not target or target == "" then
    io.stderr:write("gen_lod: expected <mesh_name|--all> as arg[1]\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then
    io.stderr:write("gen_lod: Scene.GetActive() returned nil\n")
    os.exit(1)
end

local processed = 0

local function should_process(mesh_name)
    if target == "--all" then return true end
    return mesh_name == target
end

scene:ForEachMesh(function(mesh)
    local name = mesh:GetName()
    if not should_process(name) then return end
    local r = MeshSimplifier.SimplifyMesh(mesh, {})
    if not r or not r.lod_meshes or #r.lod_meshes == 0 then
        io.stderr:write("gen_lod: simplifier returned no LODs for mesh '"
            .. tostring(name) .. "'\n")
        return
    end
    mesh:SetLodMeshes(r.lod_meshes, r.thresholds)
    processed = processed + 1
    if not out_path then
        print(string.format("mesh %s: %d LODs generated",
            tostring(name), #r.lod_meshes))
    end
end)

if processed == 0 then
    io.stderr:write("gen_lod: no mesh matched '" .. target .. "'\n")
    os.exit(1)
end

if out_path then
    local ok = Scene.SaveActive(out_path)
    if not ok then
        io.stderr:write("gen_lod: SaveActive('" .. out_path .. "') failed\n")
        os.exit(1)
    end
    print(string.format("wrote %s (LODs attached to %d mesh(es))",
        out_path, processed))
end