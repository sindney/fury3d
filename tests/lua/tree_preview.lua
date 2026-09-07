-- tests/lua/tree_preview.lua - render a kraut-imported tree scene fragment.
-- (change: add-kraut-vegetation; used for task 4.3 visual check + section 9
-- validation: LOD transitions, billboard facing, alpha-cut leaves, wind sway)
--
-- Usage:
--   ./fury tests/lua/tree_preview.lua <tree.bin> [distCm] --screenshot out.png --screenshot-frame 5
-- Env: FURY_WIND_TIME=<seconds> pins Engine time (wind sway at t).
--      FURY_WIND_STRENGTH=<cm> overrides renderSettings wind strength (0 = rigid).
--      FURY_SUN_PITCH=<rad> FURY_SUN_YAW=<rad> override the sun rotation.
--      FURY_FORCE_LOD=<k> forces LOD tier k: k=0 clears the chain (renders
--      tier 0 at any distance); k>=1 crafts thresholds so PickLodForCoverage
--      always lands on tier k (2.0 up to and including tier k, 0.0 deeper --
--      under the corrected deep->shallow walk the pick lands one tier above
--      the shallowest matched threshold).
--      FURY_BB_BIAS=<0..1> overrides the billboard shading-normal up bias
--      (u_billboard_up_bias) on the billboard material.
--
-- The harness loads the fragment, adds a sun + a camera at distCm looking at
-- the tree, and renders the PBR pipeline.

local function fail(msg)
    io.stderr:write("tree_preview FAIL: " .. msg .. "\n")
    os.exit(1)
end

local tree_path = arg and arg[1]
local dist = tonumber(arg and arg[2] or "1200") or 1200

local octree = nil
local pl = nil

local function on_init()
    if not tree_path then fail("expected tree .bin path as arg[1]") end

    -- the scene's working dir is where the fragment's relative texture
    -- paths resolve -- must be the .bin's own directory (exec/CLI
    -- convention; LoadActive does not rewrite it)
    local bin_dir = tree_path:match("^(.*[/\\])") or ""

    octree = OcTree.Create()
    local scene = Scene.Create("tree_preview", bin_dir, octree)
    Scene.SetActive(scene)

    if not Scene.LoadActive(tree_path) then
        fail("failed to load " .. tree_path)
    end
    scene = Scene.GetActive()
    local root = scene:GetRootNode()

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    pl = Pipeline.GetActive()
    local full_pipe = FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingPBR.json")
    FileUtil.LoadPipelineFromFile(pl, full_pipe)
    PostProcess.LoadFromDirectory("Resource/PostProcess")

    -- sun (leaf-shaped shadows via the alpha-tested depth variants)
    local sun_pitch = tonumber(os.getenv("FURY_SUN_PITCH") or "-1.0")
    local sun_yaw = tonumber(os.getenv("FURY_SUN_YAW") or "0.5")
    local sun_node = SceneNode.Create("Sun")
    local sun = Light.Create()
    sun:SetType(0)
    sun:SetIntensity(3.0)
    sun:SetCastShadows(true)
    sun_node:AddComponent(sun)
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(sun_pitch, sun_yaw, 0.0))
    root:AddChild(sun_node)

    -- camera at dist (cm), framed on the tree center
    local mn, mx = scene:ComputeWorldAABB()
    local center = Vector4(0.0, 250.0, 0.0, 1.0)
    if mn then center = (mn + mx) * 0.5 end
    local cam_node = SceneNode.Create("Camera")
    cam_node:AddComponent(Transform.Create())
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1.0, 500000.0)
    cam_node:SetLocalPosition(Vector4(center.x, center.y + dist * 0.25, center.z + dist, 1.0))
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, -math.atan(dist * 0.25, dist), 0.0))
    cam_node:Recompose(false)
    cam_node:AddComponent(camera)
    root:AddChild(cam_node)
    pl:SetCurrentCamera(cam_node)

    -- nodes added post-load are not octree-registered (AddChild doesn't
    -- register); the sun must be in the query for the light pass
    octree:AddSceneNodeRecursively(sun_node)
    octree:AddSceneNodeRecursively(cam_node)

    -- wind time pin for sway verification
    local wt = os.getenv("FURY_WIND_TIME")
    if wt then Engine.SetTime(tonumber(wt)) end

    -- wind strength override (0 = rigid trees for shading A/B)
    local ws = os.getenv("FURY_WIND_STRENGTH")
    if ws then
        scene:GetRenderSettings():SetWindParams(Vector4(1.0, 0.0, tonumber(ws), 1.0))
    end

    -- force a LOD tier for shading A/B at a fixed camera
    local force_lod = os.getenv("FURY_FORCE_LOD")
    if force_lod then
        local k = tonumber(force_lod)
        Scene.ForEachNode(scene, function(n)
            local mr = n:GetMeshRender()
            if not (mr and mr:GetMesh()) then return end
            local mesh = mr:GetMesh()
            local count = mesh:GetLodCount()
            if count <= 1 then return end
            if k <= 0 then
                mesh:ClearLodChain()
                return
            end
            local meshes = {}
            local thresholds = {}
            local flags = {}
            for i = 1, count - 1 do
                meshes[i] = mesh:GetLodMesh(i)
                thresholds[i] = (i <= k) and 2.0 or 0.0
                flags[i] = mesh:IsLodBillboard(i)
            end
            mesh:SetLodMeshes(meshes, thresholds, flags)
        end)
    end

    -- billboard shading-normal up-bias override (10.3 tuning sweep)
    local bb_bias = os.getenv("FURY_BB_BIAS")
    if bb_bias then
        Scene.ForEachNode(scene, function(n)
            local mr = n:GetMeshRender()
            if not (mr and mr:GetMesh()) then return end
            local bbmat = mr:GetMesh():GetBillboardMaterial()
            if bbmat then bbmat:SetUniform("u_billboard_up_bias", tonumber(bb_bias)) end
        end)
    end

    -- shrink test: FURY_SHRINK=1 scales positions x0.01 (tree vanishes)
    if os.getenv("FURY_SHRINK") == "1" then
        Scene.ForEachNode(scene, function(n)
            local mr = n:GetMeshRender()
            if not (mr and mr:GetMesh()) then return end
            local mesh = mr:GetMesh()
            local pos = mesh:GetPositions()
            for i = 1, #pos do pos[i] = pos[i] * 0.01 end
            mesh:SetPositions(pos)
        end)
        print("SHRINK applied")
    end

    -- flat-up test: FURY_FLAT_UP=1 forces every normal to +Y
    if os.getenv("FURY_FLAT_UP") == "1" then
        Scene.ForEachNode(scene, function(n)
            local mr = n:GetMeshRender()
            if not (mr and mr:GetMesh()) then return end
            local mesh0 = mr:GetMesh()
            local count = mesh0:GetLodCount()
            for li = 0, count - 1 do
                local mesh = mesh0:GetLodMesh(li)
                local nrm = mesh:GetNormals()
                for i = 1, #nrm, 3 do
                    nrm[i], nrm[i+1], nrm[i+2] = 0.0, 1.0, 0.0
                end
                mesh:SetNormals(nrm)
            end
        end)
        print("FLAT_UP applied")
    end

    -- canopy-normal bend prototype (10.11): overwrite vertex normals with a
    -- flattened crown-dome proxy: proxy = normalize(dir.x, dir.y*a + R*b, dir.z)
    -- where dir = vertex - crownTop, R = crown radius. a retains vertical
    -- detail, b lifts toward up. (Whole mesh incl. trunk -- prototype only;
    -- production would limit to foliage submeshes.)
    local bend_a = os.getenv("FURY_BEND_A")
    if bend_a then
        local a = tonumber(bend_a)
        local b = tonumber(os.getenv("FURY_BEND_B") or "0.6")
        Scene.ForEachNode(scene, function(n)
            local mr = n:GetMeshRender()
            if not (mr and mr:GetMesh()) then return end
            local mesh0 = mr:GetMesh()
            local count = mesh0:GetLodCount()
            for li = 0, count - 1 do
                local mesh = mesh0:GetLodMesh(li)
                local pos = mesh:GetPositions()
                local nrm = mesh:GetNormals()
                local aabb = mesh:GetAABB()
                local mn = aabb:GetMin(); local mx = aabb:GetMax()
                local crownTop = mn.y + (mx.y - mn.y) * 0.85
                local R = math.max(mx.x - mn.x, mx.z - mn.z) * 0.5
                for i = 1, #pos, 3 do
                    local dx, dy, dz = pos[i], pos[i+1] - crownTop, pos[i+2]
                    local px, py, pz = dx, dy * a + R * b, dz
                    local l = math.sqrt(px*px + py*py + pz*pz)
                    if l > 1e-5 then
                        nrm[i], nrm[i+1], nrm[i+2] = px / l, py / l, pz / l
                    end
                end
                mesh:SetNormals(nrm)
            end
        end)
        print("BEND applied a=" .. a .. " b=" .. b)
    end
end

local frame = 0
local function on_update(dt)
    pl:Execute(octree)
    frame = frame + 1
    if frame == 4 then
        -- print the active LOD tier per tree node (billboard = last tier)
        Scene.ForEachNode(Scene.GetActive(), function(n)
            local mr = n:GetMeshRender()
            if mr and mr:GetMesh() then
                local mesh = mr:GetMesh()
                print(string.format("LOD %s: tier %d/%d%s", n:GetName(), mr:GetActiveLod(), mesh:GetLodCount() - 1,
                    mesh:IsLodBillboard(mr:GetActiveLod()) and " (billboard)" or ""))
            end
        end)
    end
end

Engine.run({ on_init = on_init, on_update = on_update })
