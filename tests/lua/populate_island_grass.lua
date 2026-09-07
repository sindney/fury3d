-- tests/lua/populate_island_grass.lua - bake the grass layer into
-- ocean_island.bin (change: add-kraut-vegetation, task 10.6).
--
-- Placement comes from examples/Projects/ocean/grass_points.lua (written by
-- tools/gen_island_grass.py: splat-dominant-grass + slope + height gated).
-- The grass mesh/materials enter the island scene by merging the
-- GrassClump.bin fragment and instantiating an InstancedMeshRender; the
-- fragment's template node is removed.
--
-- Usage (from examples/):
--   ./fury ../tests/lua/populate_island_grass.lua <output.bin>
-- Always writes a NEW file; review the diff, then replace
-- ocean_island.bin manually (cp).

local function fail(msg)
    io.stderr:write("populate_island_grass FAIL: " .. msg .. "\n")
    os.exit(1)
end

local out_path = arg and arg[1]
local function on_init()
    if not out_path then fail("expected output .bin path as arg[1]") end

    local octree = OcTree.Create()
    Scene.SetActive(Scene.Create("ocean_island", "Projects/ocean/", octree))
    if not Scene.LoadActive("Projects/ocean/ocean_island.bin") then
        fail("failed to load ocean_island.bin")
    end
    local scene = Scene.GetActive()

    -- grass fragment: separate scene, then merge assets in
    local grass = Importer.LoadScene(FileUtil.GetAbsPath("Resource/Trees/Grass/GrassClump.bin"))
    if not grass then fail("failed to load GrassClump.bin") end
    if Importer.MergeInto(scene, grass) < 1 then fail("merge produced no nodes") end

    -- find the merged template node; harvest mesh + materials, then remove it
    local mesh, materials = nil, {}
    Scene.ForEachNode(scene, function(n)
        local mr = n:GetMeshRender()
        if mr and mr:GetMesh() and mr:GetMesh():GetName() == "GrassClump_LOD0" then
            mesh = mr:GetMesh()
            for i = 0, mr:GetMaterialCount() - 1 do
                materials[#materials + 1] = mr:GetMaterial(i)
            end
            n:RemoveFromParent()
        end
    end)
    if not mesh then fail("merged grass mesh not found") end

    -- idempotent: drop any existing GrassField before adding
    local existing = {}
    Scene.ForEachNode(scene, function(n)
        if n:GetName() == "GrassField" then existing[#existing + 1] = n end
    end)
    for _, n in ipairs(existing) do
        n:RemoveFromParent()
        print("removed existing GrassField")
    end

    -- Texture paths: the material blocks embed bare filenames
    -- ("GrassClump_D.png") that resolve relative to the island's working
    -- dir -- the sibling convention the tree textures already follow.
    -- tools/gen_island_grass.py copies the two PNGs into Projects/ocean/.

    local points = dofile("Projects/ocean/grass_points.lua")
    if not points or #points < 5 then fail("grass_points.lua empty") end

    local inst_node = SceneNode.Create("GrassField")
    inst_node:AddComponent(Transform.Create())
    local imr = InstancedMeshRender.Create(mesh, materials[1])
    for i = 2, #materials do imr:SetMaterial(materials[i], i - 1) end
    imr:SetCastShadows(false)
    imr:SetCullDistance(8000.0) -- grass is invisible past ~80 m; don't pay for it
    for i = 1, #points, 5 do
        imr:AddInstance(Vector4(points[i], points[i + 1], points[i + 2], 1.0),
            points[i + 3], points[i + 4])
    end
    inst_node:AddComponent(imr)
    scene:GetRootNode():AddChild(inst_node)

    print(string.format("POPULATE instances=%d (%.0f points) mesh=%s materials=%d",
        imr:GetInstanceCount(), #points / 5, mesh:GetName(), #materials))

    if not Scene.SaveActive(out_path) then fail("save failed: " .. out_path) end
    print("populate_island_grass OK -> " .. out_path)
    os.exit(0)
end

Engine.run({ on_init = on_init, on_update = function() end })
