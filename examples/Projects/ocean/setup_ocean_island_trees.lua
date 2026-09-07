-- setup_ocean_island_trees.lua - populate ocean_island.bin with Kraut trees
-- (change: add-kraut-vegetation, task 8.3). Re-runnable: existing grove
-- nodes are reused (instances cleared), so re-running does not duplicate.
--
--   ./fury exec Projects/ocean/ocean_island.bin Projects/ocean/setup_ocean_island_trees.lua
--
-- For each baked tree type (examples/Resource/Trees/<name>/<name>.bin, see
-- tools/gen_tree_assets.py) the script merges the fragment into the island
-- scene, swaps the merged node's MeshRender for an InstancedMeshRender (the
-- fragment node keeps the kraut importer's meters->cm x100 scale, so instance
-- positions are in meters), and scatters a seeded stand over the island
-- terrain (above the waterline, below the bare rock).

local function fail(msg)
    io.stderr:write("setup_ocean_island_trees FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene (exec must load ocean_island.bin)") end
local root = scene:GetRootNode()

local terrain_node = root:FindChildRecursively("Terrain")
if not terrain_node then fail("ocean_island.bin has no Terrain node") end
local terrain = terrain_node:GetTerrain()
if not terrain or not terrain:HasHeights() then fail("terrain has no heights") end

-- tree name -> (seed, count, minHeightM, maxHeightM). Heights in meters;
-- the kraut fragment node carries the x100 meters->cm scale.
local STANDS = {
    { name = "PalmTree2", seed = 11, count = 1500, minH = 4.5, maxH = 26.0 },
    { name = "Tree1",     seed = 23, count = 800,  minH = 5.5, maxH = 22.0 },
}

local HALF_ISLAND_M = 480.0 -- island is 1024 m across, centered at origin

for _, stand in ipairs(STANDS) do
    -- reuse an existing grove on re-run (its node was renamed at creation)
    local node = root:FindChildRecursively(stand.name .. "Grove")
    local imr = node and node:GetInstancedMeshRender() or nil

    if not imr then
        local fragPath = FileUtil.GetAbsPath("Resource/Trees/" .. stand.name .. "/" .. stand.name .. ".bin")
        if not FileUtil.FileExist(fragPath) then
            fail("tree fragment missing: " .. fragPath .. " (run tools/gen_tree_assets.py)")
        end

        local frag = Importer.LoadScene(fragPath)
        if not frag then fail("failed to load fragment: " .. fragPath) end
        if Importer.MergeInto(scene, frag) < 1 then
            fail("merge produced no nodes for " .. stand.name)
        end

        -- the fragment's LOD0 node (chain root) hosts the grove
        node = root:FindChildRecursively(stand.name .. "_LOD0")
        if not node then fail("fragment node not found after merge: " .. stand.name .. "_LOD0") end

        local mr = node:GetMeshRender()
        if not mr then fail("fragment node has no MeshRender") end
        local mesh = mr:GetMesh()
        if not mesh then fail("fragment MeshRender has no mesh") end
        if mesh:GetLodCount() < 2 then fail("fragment mesh has no LOD chain") end

        -- carry every material slot over (bark + fronds), then swap the renderer
        imr = InstancedMeshRender.Create(mesh, mr:GetMaterial(0))
        for i = 1, mr:GetMaterialCount() - 1 do
            imr:SetMaterial(mr:GetMaterial(i), i)
        end
        node:RemoveComponent(mr)
        node:AddComponent(imr)
        node:SetName(stand.name .. "Grove")
        node:SetLocalPosition(Vector4(0.0, 0.0, 0.0, 1.0))
    end

    -- seeded scatter (mt19937 is not exposed; Lua's rng seeded per stand is
    -- deterministic within this script's platform)
    math.randomseed(stand.seed)
    imr:ClearInstances()
    local placed = 0
    local attempts = 0
    while placed < stand.count and attempts < stand.count * 20 do
        attempts = attempts + 1
        local x = (math.random() * 2.0 - 1.0) * HALF_ISLAND_M
        local z = (math.random() * 2.0 - 1.0) * HALF_ISLAND_M
        local h = terrain:GetHeight(x * 100.0, z * 100.0) / 100.0 -- cm -> m
        if h >= stand.minH and h <= stand.maxH then
            local yaw = math.random() * 6.2831853
            local s = 0.85 + math.random() * 0.4
            -- node is x100 scaled: instance local units are meters
            imr:AddInstance(Vector4(x, h, z, 1.0), yaw, s)
            placed = placed + 1
        end
    end
    print(stand.name .. ": scattered " .. placed .. " instances")
    if placed < stand.count / 2 then
        fail("scatter placed too few instances for " .. stand.name .. ": " .. placed)
    end
end

root:Recompose(true)

-- The island scene's textures are bare siblings of the .bin (the engine's
-- scene-save relocation convention); the merged tree fragments reference
-- their textures bare too, so the atlas/bark/frond files must be copied
-- next to ocean_island.bin before the save resolves them.
local fragDir = FileUtil.GetAbsPath("Resource/Trees")
for _, stand in ipairs(STANDS) do
    local dir = fragDir .. "/" .. stand.name
    for _, f in ipairs(FileUtil.ListDirectory(dir)) do
        if f:match("%.tga$") or f:match("%.png$") then
            FileUtil.CopyFile(dir .. "/" .. f, FileUtil.GetAbsPath("Projects/ocean") .. "/" .. f)
        end
    end
end

local out = "Projects/ocean/ocean_island.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(out)) then
    fail("save failed: " .. out)
end
print("setup_ocean_island_trees: wrote " .. out)
print("setup_ocean_island_trees: DONE")
