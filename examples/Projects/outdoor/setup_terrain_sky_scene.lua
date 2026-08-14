-- setup_terrain_sky_scene.lua - derive outdoor_terrain.bin from
-- outdoor_physics.bin (change: add-sky-atmosphere-terrain, task 10.1).
-- NEVER writes the source. Re-runnable: always starts from
-- outdoor_physics.bin.
--
--   ./fury exec Projects/outdoor/outdoor_physics.bin Projects/outdoor/setup_terrain_sky_scene.lua
--
-- Result: examples/Projects/outdoor/outdoor_terrain.bin with:
--   * Terrain node (Terrain/height.r16, 4-layer splat, chunked LOD meshes)
--     with a heightfield BodySetup; the flat Grid ground is removed
--   * SkyAtmosphere node bound to DefaultSun (TOD drives the sun light)
--   * village-area terrain re-leveled so authored props stay grounded;
--     rocks/trees/fences/crates/player re-seated via Terrain:GetHeight
--   * PlayerCamera far plane pushed out for terrain sightlines

local function fail(msg)
    io.stderr:write("setup_terrain_sky_scene FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene (exec must load outdoor_physics.bin)") end
local root = scene:GetRootNode()

-- -- 1. Terrain node -------------------------------------------------------
local terrain_node = SceneNode.Create("Terrain")
local terrain = Terrain.Create()
terrain:SetHeightmapPath("Terrain/height.r16")
terrain:SetSplatmapPath("Terrain/splat.png")
terrain:SetChunkCount(16)
terrain:SetLodCount(4)
terrain:SetLayer(0, "grass", "Terrain/grass.png", 900.0)
terrain:SetLayer(1, "rock",  "Terrain/rock.png",  1200.0)
terrain:SetLayer(2, "mud",   "Terrain/mud.png",   700.0)
terrain:SetLayer(3, "snow",  "Terrain/snow.png",  1000.0)
root:AddChild(terrain_node)
terrain_node:AddComponent(terrain) -- OnAttaching builds chunks headlessly
if not terrain:HasHeights() then fail("heightmap did not load (run tools/gen_terrain_assets.py)") end

local terrain_body = BodySetup.Create()
terrain_body:SetShapeType(3) -- heightfield, static
terrain_node:AddComponent(terrain_body)

-- Re-level: the village area was authored on a flat grid at y ~= 0, so sink
-- the terrain node until the flatten-disc center lands at y = 0.
local h0 = terrain:GetHeight(0.0, -200.0)
terrain_node:SetLocalPosition(Vector4(0.0, -h0, 0.0, 1.0))
terrain_node:Recompose(true)
print(string.format("setup_terrain_sky_scene: terrain offset y=%.1f (village h0=%.1f)",
    -h0, terrain:GetHeight(0.0, -200.0)))

-- -- 2. Remove the flat grid ground ----------------------------------------
local grid = root:FindChildRecursively("Grid")
if grid then
    grid:RemoveFromParent()
    print("setup_terrain_sky_scene: Grid removed")
else
    print("setup_terrain_sky_scene: no Grid node (already removed?)")
end

-- -- 3. Re-seat props on the terrain ---------------------------------------
-- These are direct children of the x100 RootNode: local y shifts by
-- (terrainH - worldY) / 100. Nodes inside the flatten disc barely move.
local reseat_names = {
    "Rock", "Rock1", "Rock2",
    "Tree1", "Tree2", "Tree3", "Tree4", "Tree0",
    "BarriereLong",
    "Barriere", "Barriere.001", "Barriere.002", "Barriere.003",
    "Barriere.004", "Barriere.005", "Barriere.006", "Barriere.007",
}
for _, name in ipairs(reseat_names) do
    local node = root:FindChildRecursively(name)
    if node then
        local wp = node:GetWorldPosition()
        local h = terrain:GetHeight(wp.x, wp.z)
        local dy = h - wp.y
        if math.abs(dy) > 5.0 then
            local parent = node:GetParent()
            local parentScale = parent and parent:GetLocalScale() or nil
            local s = 1.0
            if parentScale then s = parentScale.y end
            if s == 0.0 then s = 1.0 end
            local lp = node:GetLocalPosition()
            node:SetLocalPosition(Vector4(lp.x, lp.y + dy / s, lp.z, 1.0))
        end
    else
        print("setup_terrain_sky_scene: skip " .. name .. " (not found)")
    end
end
root:Recompose(true)

-- player + crates (root level, unscaled space)
local function drop_on_terrain(name, clearance)
    local node = root:FindChildRecursively(name)
    if not node then return end
    local wp = node:GetWorldPosition()
    local h = terrain:GetHeight(wp.x, wp.z)
    local lp = node:GetLocalPosition()
    node:SetLocalPosition(Vector4(lp.x, h + clearance, lp.z, 1.0))
end
drop_on_terrain("Player", 10.0)
drop_on_terrain("Crate1", 40.0)
drop_on_terrain("Crate2", 60.0)
drop_on_terrain("Crate3", 120.0)

-- -- 4. Sky -----------------------------------------------------------------
local sun_node = root:FindChildRecursively("DefaultSun")
if not sun_node then fail("DefaultSun not found") end

local sky_node = SceneNode.Create("Sky")
local sky = SkyAtmosphere.Create()
sky:SetTimeHours(10.5)
sky:SetSunFromTod(true)
sky:SetSunLightName("DefaultSun")
sky:SetCloudsEnabled(true)
sky:SetCloudCoverage(0.4)
sky:SetCloudScale(0.7)   -- 512px tile ~= 1.4km, texel ~2.8m
sky:SetCloudDensity(18.0)
-- Engine/ prefix -> Scene::ResolveAsset maps to <cwd>/Resource/...
-- so these resolve to examples/Resource/Texture/Sky/ regardless of
-- the opened scene's working_dir. See SKY-README.md.
sky:SetCloudNoisePath("Engine/Texture/Sky/cloud_noise.png")
sky:SetMoonTexturePath("Engine/Texture/Sky/moon.png")
sky:SetSunAngularRadius(0.009)   -- larger-than-life disc, demo readability
sky:SetSunDiscIntensity(10.0)    -- keeps the horizon disc orange, not blown
sky:SetMoonIntensity(0.9)
sky:SetMoonAngularRadius(0.008)
sky_node:AddComponent(sky)
root:AddChild(sky_node)

-- -- 5. Camera range for terrain sightlines ---------------------------------
local cam_node = root:FindChildRecursively("PlayerCamera")
if cam_node then
    local cam = cam_node:GetComponent(Camera)
    if cam then
        cam:PerspectiveFov(0.7854, 1.778, 1.0, 200000.0)
    end
end

-- shadow settings live on the scene render settings (single tuning spot)
local rs = scene:GetRenderSettings()
if rs then
    rs:SetShadowFar(20000.0)     -- 200m cascade range
    rs:SetCsmSplitBlend(0.7)     -- log-heavy: fine near texels + full range
    rs:SetCsmMapSize(2048)       -- 2k cascades for terrain self-shadowing
end

scene:ForEachNode(function() end) -- no-op; keeps parity with list scripts

root:Recompose(true)

local out = "Projects/outdoor/outdoor_terrain.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(out)) then
    fail("save failed: " .. out)
end
print("setup_terrain_sky_scene: wrote " .. out)
print("setup_terrain_sky_scene: DONE")
