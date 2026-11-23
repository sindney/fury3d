-- Deterministic foliage captures for tests/foliage_lod_render.py.
local octree, pipeline, camera_node, source_render, source_mesh
local frame = 0
local mode = os.getenv("FURY_FOLIAGE_MODE") or "plain"
local tree = os.getenv("FURY_FOLIAGE_TREE") or "PalmTree2"
local tier = tonumber(os.getenv("FURY_FOLIAGE_TIER"))
local distance = tonumber(os.getenv("FURY_FOLIAGE_DISTANCE") or "2400")

local function force_tier(mesh, selected)
    local meshes, thresholds, flags = {}, {}, {}
    for i = 1, mesh:GetLodCount() - 1 do
        meshes[i] = mesh:GetLodMesh(i)
        thresholds[i] = i <= selected and 1e30 or 0.0
        flags[i] = mesh:IsLodBillboard(i)
    end
    mesh:SetLodMeshes(meshes, thresholds, flags)
end

local function on_init()
    local dir = "Resource/Trees/" .. tree .. "/"
    octree = OcTree.Create()
    Scene.SetActive(Scene.Create("foliage_lod_check", dir, octree))
    assert(Scene.LoadActive(dir .. tree .. ".bin"), "cannot load tree")
    local scene = Scene.GetActive()
    local root = scene:GetRootNode()
    local source_node, materials
    Scene.ForEachNode(scene, function(node)
        local render = node:GetMeshRender()
        if render and not source_render then
            source_node, source_render, source_mesh = node, render, render:GetMesh()
            materials = {}
            for i = 0, render:GetMaterialCount() - 1 do materials[i + 1] = render:GetMaterial(i) end
        end
    end)
    assert(source_mesh and source_mesh:GetLodCount() > 1, "missing LOD chain")
    if tier then force_tier(source_mesh, tier) end
    source_render:SetCastShadows(false)

    local yaw = tonumber(os.getenv("FURY_FOLIAGE_YAW") or "0")
    source_node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, 0.0, 0.0))
    root:Recompose(true)
    local bounds = source_node:GetWorldAABB()
    local center = (bounds:GetMin() + bounds:GetMax()) * 0.5

    if mode == "instanced" or mode == "mixed" then
        source_node:RemoveComponent(source_render)
        local node = SceneNode.Create("FoliageInstances")
        local render = InstancedMeshRender.Create(source_mesh, materials[1])
        for i = 2, #materials do render:SetMaterial(materials[i], i - 1) end
        render:SetCastShadows(false)
        render:SetHierarchical(true)
        render:AddInstance(Vector4(0.0, 0.0, 0.0, 1.0), yaw, 100.0)
        if mode == "mixed" then
            render:AddInstance(Vector4(5000.0, 0.0, -14000.0, 1.0), yaw, 100.0)
        end
        node:AddComponent(render)
        root:AddChild(node)
        node:Recompose(true)
        octree:AddSceneNodeRecursively(node)
    end

    pipeline = DeferredPipeline.Create("pipeline")
    Pipeline.SetActive(pipeline)
    pipeline = Pipeline.GetActive()
    FileUtil.LoadPipelineFromFile(pipeline, os.getenv("FURY_FOLIAGE_PIPELINE")
        or FileUtil.GetAbsPath("Resource/Pipeline/DeferredLightingPBR.json"))
    PostProcess.LoadFromDirectory("Resource/PostProcess")
    local settings = scene:GetRenderSettings()
    settings:SetHDR(os.getenv("FURY_FOLIAGE_HDR") ~= "0")
    settings:SetCascadedShadowMap(false)
    settings:ClearChain()
    settings:SetWindParams(Vector4(1.0, 0.0, 0.0, 1.0))
    Pipeline.ApplyRenderSettings(pipeline, settings)

    local sun_node = SceneNode.Create("Sun")
    local sun = Light.Create()
    sun:SetType(0)
    sun:SetIntensity(3.0)
    sun:SetCastShadows(false)
    sun_node:AddComponent(sun)
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(
        tonumber(os.getenv("FURY_FOLIAGE_SUN_YAW") or "-1.0"),
        tonumber(os.getenv("FURY_FOLIAGE_SUN_PITCH") or "0.5"), 0.0))
    root:AddChild(sun_node)
    sun_node:Recompose(true)
    octree:AddSceneNodeRecursively(sun_node)

    camera_node = SceneNode.Create("Camera")
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1.0, 500000.0)
    camera_node:AddComponent(camera)
    root:AddChild(camera_node)
    camera_node:SetLocalPosition(Vector4(center.x, center.y, center.z + distance, 1.0))
    camera_node:Recompose(true)
    pipeline:SetCurrentCamera(camera_node)
    octree:AddSceneNodeRecursively(camera_node)
    Engine.SetTime(0.0)
end

local function on_update(dt)
    pipeline:Execute(octree)
    frame = frame + 1
    if frame == 5 and mode == "plain" then
        print(string.format("FOLIAGE tier=%d expected=%s", source_render:GetActiveLod(), tostring(tier)))
        if tier then assert(source_render:GetActiveLod() == tier, "forced tier was not selected") end
    end
end

local function checked(callback)
    return function(...)
        local ok, message = pcall(callback, ...)
        if not ok then io.stderr:write(tostring(message) .. "\n"); os.exit(1) end
    end
end

Engine.run({ on_init = checked(on_init), on_update = checked(on_update) })
