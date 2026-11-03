-- Bake the seeded grass layer into a new island scene.
-- Run from examples/: ./fury Projects/ocean/populate_island_grass.lua <output.bin>

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
    for _, name in ipairs({ "GrassClump_D.png", "GrassClump_Billboard.png" }) do
        local texture = grass:GetTexture(name)
        if not texture then fail("grass texture not found: " .. name) end
        texture:SetFilePathAndSRGB("Engine/Trees/Grass/" .. name, true)
    end
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

    for _, name in ipairs({ "GrassClump_D.png", "GrassClump_Billboard.png" }) do
        if not scene:GetTexture("Engine/Trees/Grass/" .. name) then
            fail("canonical grass texture not found: " .. name)
        end
    end

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
