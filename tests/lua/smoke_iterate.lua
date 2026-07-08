-- tests/lua/smoke_iterate.lua — exercise Scene.ForEach* iteration bindings.
--
-- Read-only: does not write any file. NOT a replacement for `fury info`
-- (which already does headless scene summarization); only proves the
-- Lua iteration bindings work end-to-end from the `fury exec` path.
--
-- Usage:
--   ./fury exec examples/Resource/Scene/scene.json tests/lua/smoke_iterate.lua

local scene = Scene.GetActive()
if not scene then
    io.stderr:write("smoke_iterate: Scene.GetActive() returned nil\n")
    os.exit(1)
end

local mesh_count = 0
local material_count = 0
local node_count = 0

scene:ForEachMesh(function(m)
    mesh_count = mesh_count + 1
end)

scene:ForEachMaterial(function(mat)
    material_count = material_count + 1
end)

scene:ForEachNode(function(n)
    node_count = node_count + 1
end)

print(string.format("ForEachMesh callbacks: %d", mesh_count))
print(string.format("ForEachMaterial callbacks: %d", material_count))
print(string.format("ForEachNode callbacks: %d", node_count))