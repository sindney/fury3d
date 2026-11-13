-- tests/lua/skinned_bounds.lua -- guard for skinned-mesh world bounds.
--
-- A skinned mesh's world AABB must follow the animated pose and must stay
-- around the character. It must never stretch back to the world origin,
-- and the mesh asset's stored (model space) bounds must survive posing.

local function fail(msg)
    io.stderr:write("skinned_bounds FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene") end

local ORIGIN_CLEARANCE = 1.0
local TOLERANCE = 0.05
local POSE_STEPS = 30
local POSE_DT = 1.0 / 60.0

local function fmt(v)
    return string.format("(%.3f, %.3f, %.3f)", v.x, v.y, v.z)
end

local function contains_origin(aabb)
    local min = aabb:GetMin()
    local max = aabb:GetMax()
    return min.x <= 0 and max.x >= 0 and
           min.y <= 0 and max.y >= 0 and
           min.z <= 0 and max.z >= 0
end

local function contains_point(aabb, p, tol)
    local min = aabb:GetMin()
    local max = aabb:GetMax()
    return p.x >= min.x - tol and p.x <= max.x + tol and
           p.y >= min.y - tol and p.y <= max.y + tol and
           p.z >= min.z - tol and p.z <= max.z + tol
end

local function joint_world_positions(mesh)
    local positions = {}
    for i = 0, mesh:GetJointCount() - 1 do
        local joint = mesh:GetJointAt(i)
        local node = joint and joint:GetSceneNode()
        if node then positions[#positions + 1] = node:GetWorldPosition() end
    end
    return positions
end

local function positions_changed(a, b)
    if #a ~= #b then return true end
    for i = 1, #a do
        if a[i].x ~= b[i].x or a[i].y ~= b[i].y or a[i].z ~= b[i].z then
            return true
        end
    end
    return false
end

-- The joint extent is only meaningful if the skeleton is one connected
-- hierarchy rooted inside this mesh.
local function check_joint_hierarchy(mesh, label)
    local names = {}
    for i = 0, mesh:GetJointCount() - 1 do
        local joint = mesh:GetJointAt(i)
        if not joint then fail("joint " .. i .. " missing: " .. label) end
        names[joint:GetName()] = true
    end
    local roots = 0
    for i = 0, mesh:GetJointCount() - 1 do
        local parent = mesh:GetJointAt(i):GetParent()
        if not parent then
            roots = roots + 1
        elseif not names[parent:GetName()] then
            fail("joint " .. mesh:GetJointAt(i):GetName() ..
                " has a parent outside the mesh: " .. label)
        end
    end
    if roots ~= 1 then
        fail("expected exactly one root joint, got " .. roots .. ": " .. label)
    end
end

local skinned_count = 0

scene:ForEachNode(function(node)
    local render = node:GetMeshRender()
    if not render then return end
    local mesh = render:GetMesh()
    if not mesh or not mesh:IsSkinnedMesh() then return end

    skinned_count = skinned_count + 1
    local label = node:GetName() .. " (mesh " .. mesh:GetName() .. ")"

    check_joint_hierarchy(mesh, label)

    local asset_before = mesh:GetAABB()
    local before_min = asset_before:GetMin()
    local before_max = asset_before:GetMax()

    local animator = node:GetAnimator()
    if not animator or animator:GetStateCount() < 1 then
        fail("skinned node has no animator state: " .. label)
    end
    local state = animator:GetStateAt(0)
    if not state then fail("animator state missing: " .. label) end
    if not animator:Play(state:GetName()) then
        fail("could not play animator state " .. state:GetName() .. ": " .. label)
    end

    local joints_before = joint_world_positions(mesh)
    local time_before = state:GetTime()
    for _ = 1, POSE_STEPS do
        animator:AdvanceTime(POSE_DT)
        animator:Display(1.0)
    end
    local joints_after = joint_world_positions(mesh)

    if #joints_after < 1 then fail("skinned mesh has no joint scene nodes: " .. label) end
    if state:GetTime() <= time_before then
        fail("animator state did not advance: " .. label .. " state " .. state:GetName())
    end
    if not positions_changed(joints_before, joints_after) then
        fail("pose did not move the skeleton: " .. label .. " state " .. state:GetName())
    end

    local joint_min = Vector4(joints_after[1].x, joints_after[1].y, joints_after[1].z)
    local joint_max = Vector4(joints_after[1].x, joints_after[1].y, joints_after[1].z)
    local min_dist, max_dist = math.huge, -math.huge
    for i = 1, #joints_after do
        local p = joints_after[i]
        joint_min.x = math.min(joint_min.x, p.x)
        joint_min.y = math.min(joint_min.y, p.y)
        joint_min.z = math.min(joint_min.z, p.z)
        joint_max.x = math.max(joint_max.x, p.x)
        joint_max.y = math.max(joint_max.y, p.y)
        joint_max.z = math.max(joint_max.z, p.z)
        local dist = math.sqrt(p.x * p.x + p.y * p.y + p.z * p.z)
        min_dist = math.min(min_dist, dist)
        max_dist = math.max(max_dist, dist)
    end

    local world_aabb = node:GetWorldAABB()
    local wmin = world_aabb:GetMin()
    local wmax = world_aabb:GetMax()
    local wsize = world_aabb:GetSize()

    print(string.format("skinned_bounds: %s state=%s", label, state:GetName()))
    print(string.format("  world aabb: min=%s max=%s size=%s valid=%s",
        fmt(wmin), fmt(wmax), fmt(wsize), tostring(world_aabb:Valid())))
    print(string.format("  joint extent: min=%s max=%s", fmt(joint_min), fmt(joint_max)))
    print(string.format("  joint distance to origin: min=%.3f max=%.3f", min_dist, max_dist))

    if not world_aabb:Valid() then
        fail("world aabb is not valid: " .. label)
    end
    if wsize.x <= 0 or wsize.y <= 0 or wsize.z <= 0 then
        fail("world aabb collapsed: " .. label .. " size=" .. fmt(wsize))
    end

    if min_dist >= ORIGIN_CLEARANCE and contains_origin(world_aabb) then
        fail("world aabb reaches the world origin: " .. label ..
            " aabb min=" .. fmt(wmin) .. " max=" .. fmt(wmax) ..
            " joints=" .. fmt(joint_min) .. ".." .. fmt(joint_max))
    end

    for i = 1, #joints_after do
        if not contains_point(world_aabb, joints_after[i], TOLERANCE) then
            fail("world aabb does not cover joint: " .. label ..
                " joint " .. i .. " at " .. fmt(joints_after[i]) ..
                " aabb min=" .. fmt(wmin) .. " max=" .. fmt(wmax))
        end
    end

    local asset_after = mesh:GetAABB()
    local after_min = asset_after:GetMin()
    local after_max = asset_after:GetMax()
    if before_min.x ~= after_min.x or before_min.y ~= after_min.y or before_min.z ~= after_min.z or
       before_max.x ~= after_max.x or before_max.y ~= after_max.y or before_max.z ~= after_max.z then
        fail("mesh asset bounds were clobbered by the animation: " .. label ..
            " before min=" .. fmt(before_min) .. " max=" .. fmt(before_max) ..
            " after min=" .. fmt(after_min) .. " max=" .. fmt(after_max))
    end

    -- A skinned mesh renders with an identity model matrix, so moving the
    -- mesh node does not move its geometry and must not move its bounds.
    local home = node:GetLocalPosition()
    node:SetLocalPosition(home.x + 3.0, home.y, home.z)
    node:Recompose(true)
    local moved = node:GetWorldAABB()
    node:SetLocalPosition(home.x, home.y, home.z)
    node:Recompose(true)

    local moved_min = moved:GetMin()
    local moved_max = moved:GetMax()
    local drift = 0.0
    for _, v in ipairs({moved_min.x - wmin.x, moved_min.y - wmin.y, moved_min.z - wmin.z,
                        moved_max.x - wmax.x, moved_max.y - wmax.y, moved_max.z - wmax.z}) do
        drift = math.max(drift, math.abs(v))
    end
    print(string.format("  bounds drift after moving the mesh node: %.6f", drift))
    if drift > 0.001 then
        fail("world aabb followed the mesh node's transform: " .. label ..
            " drift=" .. fmt({x = drift, y = 0, z = 0}) ..
            " before min=" .. fmt(wmin) .. " max=" .. fmt(wmax) ..
            " after min=" .. fmt(moved_min) .. " max=" .. fmt(moved_max))
    end
end)

if skinned_count == 0 then fail("scene contains no skinned mesh") end

print(string.format("skinned_bounds: OK (%d skinned meshes)", skinned_count))
