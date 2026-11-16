local function fail(msg)
    io.stderr:write("scene_sample_scenes FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene") end
local kind = arg and arg[1] or ""
if kind ~= "feature" and kind ~= "island" then fail("expected feature or island argument") end
local root = scene:GetRootNode()
root:Recompose(true)

local function require_node(name)
    local node = root:FindChildRecursively(name)
    if not node then fail(kind .. " node missing: " .. name) end
    return node
end

local function resolve(path)
    if path:sub(1, 7) == "Engine/" then return "Resource/" .. path:sub(8) end
    if path:sub(1, 1) == "/" or path:match("^%a:[/\\]") then return path end
    if path:sub(1, 9) == "Projects/" or path:sub(1, 9) == "Resource/" then return path end
    return scene:GetWorkingDir() .. path
end

local texture_count = 0
scene:ForEachTexture(function(texture)
    local path = texture:GetFilePath()
    if path == "" then fail("texture has no persistent path: " .. texture:GetName()) end
    if not FileUtil.FileExist(resolve(path)) then fail("texture path does not resolve: " .. path) end
    texture_count = texture_count + 1
end)

local rs = scene:GetRenderSettings()
if not rs then fail(kind .. " render settings missing") end
if not rs:IsHDR() then fail(kind .. " HDR setting was not retained") end
if not rs:IsCascadedShadowMap() then fail(kind .. " CSM setting was not retained") end

if kind == "feature" then
    for _, name in ipairs({
        "TankGroup", "Ground", "Campfire", "Feu", "FireEmber", "SmokePlume",
        "Fox", "root", "fox", "JamesRig", "James", "JamesNode",
        "Sun", "KeySpot", "FireLight", "MainCamera",
    }) do require_node(name) end

    local tanks = require_node("TankGroup")
    local tank_meshes = 0
    local tank_mesh_refs = {}
    for i = 0, tanks:GetChildCount() - 1 do
        local render = tanks:GetChildAt(i):GetMeshRender()
        if render then
            tank_meshes = tank_meshes + 1
            local mesh = render:GetMesh()
            if not mesh or mesh:GetName() ~= "Tank.005" then fail("tank shared mesh identity changed") end
            tank_mesh_refs[#tank_mesh_refs + 1] = mesh
        end
    end
    if tank_meshes ~= 6 then fail("expected six tank meshes, got " .. tank_meshes) end
    local cluster = ClusterLodTest.BuildShared(tank_mesh_refs)
    if not cluster.ok then fail("tank cluster build failed: " .. tostring(cluster.error)) end
    if cluster.reference_count ~= 6 or cluster.unique_mesh_count ~= 1 or cluster.build_count ~= 1 then
        fail("tank cluster builder did not deduplicate the shared mesh")
    end
    if cluster.source_submesh_count ~= 3 or cluster.source_triangle_count ~= 22163 then
        fail("tank source topology changed")
    end
    if cluster.lod0_triangle_count ~= 22163 then fail("tank LOD-zero coverage is incomplete") end
    if cluster.cut_cluster_counts[1] ~= cluster.leaf_cluster_count or cluster.cut_triangles[1] ~= 22163 then
        fail(string.format("tank zero-threshold cut is incomplete: %d/%d clusters, %d triangles",
            cluster.cut_cluster_counts[1], cluster.leaf_cluster_count, cluster.cut_triangles[1]))
    end
    local expected_materials = { 368, 21729, 66 }
    for i, expected in ipairs(expected_materials) do
        if cluster.material_triangles[i] ~= expected then
            fail("tank material triangle coverage changed at slot " .. i)
        end
    end
    if not cluster.source_unchanged or not cluster.cuts_valid then fail("tank cluster validation failed") end
    if cluster.group_count == 0 or cluster.max_level == 0 then fail("tank has no coarse cluster roots") end
    local previous = cluster.lod0_triangle_count
    for i, triangles in ipairs(cluster.cut_triangles) do
        if triangles > previous then
            fail(string.format("tank cut triangle count increased at threshold %s",
                tostring(cluster.cut_thresholds[i])))
        end
        previous = triangles
    end
    if previous >= cluster.lod0_triangle_count then fail("tank coarse cuts do not reduce triangles") end
    print(string.format("cluster_lod_tank: %d clusters, %d groups, %d -> %d triangles OK",
        cluster.cluster_count, cluster.group_count, cluster.lod0_triangle_count, previous))

    local lights = {
        Sun = LightType.DIRECTIONAL,
        KeySpot = LightType.SPOT,
        FireLight = LightType.POINT,
    }
    for name, light_type in pairs(lights) do
        local light = require_node(name):GetLight()
        if not light or light:GetType() ~= light_type then fail("light mismatch: " .. name) end
    end
    if not require_node("Sun"):GetLight():GetCastShadows() then fail("Sun does not cast shadows") end
    if not require_node("MainCamera"):GetCamera() then fail("MainCamera has no Camera") end

    local renderer_count = 0
    Scene.ForEachNode(scene, function(node)
        local renderer = node:GetParticleRenderer()
        if renderer then
            if not renderer:GetSystem() then fail("unresolved particle renderer: " .. node:GetName()) end
            renderer_count = renderer_count + 1
        end
    end)
    if renderer_count ~= 2 then fail("expected two particle renderers") end
    local ember = scene:GetParticleSystem("FireEmber")
    local plume = scene:GetParticleSystem("SmokePlume")
    if not ember or not plume then fail("particle definitions missing") end
    if math.abs(ember:GetLifetime() - 0.8) > 1e-4 or ember:GetMaxParticles() ~= 512 then
        fail("FireEmber definition changed")
    end
    if math.abs(plume:GetLifetime() - 3.5) > 1e-4 or plume:GetMaxParticles() ~= 256 then
        fail("SmokePlume definition changed")
    end

    local clips = {}
    scene:ForEachAnimationClip(function(clip) clips[clip:GetName()] = true end)
    for _, name in ipairs({ "Survey", "Walk", "Run", "James|Walk" }) do
        if not clips[name] then fail("animation clip missing: " .. name) end
    end
    for _, name in ipairs({ "fox", "JamesNode" }) do
        local animator = require_node(name):GetAnimator()
        if not animator or animator:GetStateCount() < 1 then fail("Animator missing: " .. name) end
    end
    if rs:GetPipelinePath() ~= "Resource/Pipeline/DeferredLightingPBR.json" then
        fail("feature pipeline path changed")
    end
    if texture_count == 0 then fail("feature textures missing") end
else
    for _, name in ipairs({ "DefaultSun", "Sky", "Ocean", "Terrain", "MainCamera", "PlayerSpawn" }) do
        require_node(name)
    end
    if root:FindChildRecursively("Tree1") then fail("orphaned Tree1 content remains") end

    local terrain_node = require_node("Terrain")
    local terrain = terrain_node:GetTerrain()
    if not terrain or not terrain:HasHeights() then fail("island terrain heights missing") end
    local body = terrain_node:GetBodySetup()
    if not body or body:GetShapeType() ~= 3 then fail("island terrain collision missing") end
    if not FileUtil.FileExist(resolve(terrain:GetHeightmapPath())) then fail("heightmap path does not resolve") end
    if not FileUtil.FileExist(resolve(terrain:GetSplatmapPath())) then fail("splatmap path does not resolve") end

    local ocean = require_node("Ocean"):GetOceanComponent()
    if not ocean or ocean:GetMode() ~= 1 then fail("infinite ocean missing") end
    if ocean:GetResolvedSource() ~= 1 then fail("ocean waves did not resolve: " .. ocean:GetResolvedReason()) end
    if not require_node("DefaultSun"):GetLight() then fail("DefaultSun light missing") end
    if not require_node("Sky"):GetSkyAtmosphere() then fail("Sky atmosphere missing") end
    if not require_node("MainCamera"):GetCamera() then fail("MainCamera camera missing") end
    local spawn = require_node("PlayerSpawn"):GetWorldPosition()
    if spawn.y <= ocean:GetWaterLevel() then fail("PlayerSpawn is below the waterline") end

    local canonical_textures = {
        ["Palm1.tga"] = "Engine/Trees/PalmTree2/Palm1.tga",
        ["PalmTree.tga"] = "Engine/Trees/PalmTree2/PalmTree.tga",
        ["PalmTree2_BillboardAtlas.png"] = "Engine/Trees/PalmTree2/PalmTree2_BillboardAtlas.png",
        ["GrassClump_D.png"] = "Engine/Trees/Grass/GrassClump_D.png",
        ["GrassClump_Billboard.png"] = "Engine/Trees/Grass/GrassClump_Billboard.png",
    }
    local canonical_count = 0
    scene:ForEachTexture(function(texture)
        local path = texture:GetFilePath()
        local name = path:match("[^/\\]+$") or ""
        if canonical_textures[name] then
            if path ~= canonical_textures[name] then fail("vegetation texture is not shared: " .. path) end
            canonical_count = canonical_count + 1
        end
    end)
    if canonical_count < 5 then fail("shared vegetation textures missing") end

    local hism_count = 0
    local lod_count = 0
    Scene.ForEachNode(scene, function(node)
        local renderer = node:GetInstancedMeshRender()
        if not renderer then return end
        if renderer:GetInstanceCount() == 0 then fail("empty kraut HISM: " .. node:GetName()) end
        local mesh = renderer:GetMesh()
        if not mesh or mesh:GetLodCount() < 2 then fail("kraut LOD chain missing: " .. node:GetName()) end
        hism_count = hism_count + 1
        lod_count = lod_count + mesh:GetLodCount()
    end)
    if hism_count < 1 or lod_count < 2 then fail("nonempty kraut vegetation missing") end
end

print("scene_sample_scenes: " .. kind .. " OK")
