-- dump_nodes.lua — print every MeshRender node's mesh + materials.
-- Usage: ./fury exec <scene.bin> ../tests/lua/dump_nodes.lua
local scene = Scene.GetActive()
if not scene then error("no scene") end
local function walk(n, d)
    local mr = n:GetMeshRender()
    if mr then
        local mats = {}
        for i = 0, mr:GetMaterialCount() - 1 do
            local m = mr:GetMaterial(i)
            mats[#mats + 1] = m and m:GetName() or "?"
        end
        local mesh = mr:GetMesh()
        print(string.rep(" ", d) .. n:GetName()
            .. " | mesh=" .. (mesh and mesh:GetName() or "?")
            .. " | mats=" .. table.concat(mats, ","))
    end
    for i = 0, n:GetChildCount() - 1 do walk(n:GetChildAt(i), d + 1) end
end
walk(scene:GetRootNode(), 0)
