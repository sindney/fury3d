-- setup_ocean_base.lua - build ocean_base.bin from scratch
-- (change: add-fft-ocean, task 9.1). Re-runnable: always starts empty.
--
--   ./fury exec Projects/outdoor/outdoor_water.bin Projects/ocean/setup_ocean_base.lua
-- (the exec scene arg is required by the CLI but unused - the script builds
-- its own scene with working dir Projects/ocean/)
--
-- Result: examples/Projects/ocean/ocean_base.bin with:
--   * DefaultSun directional light + SkyAtmosphere bound to it (TOD 13:00,
--     auto-advance on, 20-min days, clouds on)
--   * infinite OceanComponent on the committed default bake (Baked source:
--     deterministic on every platform; compute is the Auto opt-in)
--   * a far-plane camera + HDR/SSR render settings

local function fail(msg)
    io.stderr:write("setup_ocean_base FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.Create("ocean_base", "Projects/ocean/", OcTree.Create())
Scene.SetActive(scene)
local root = scene:GetRootNode()

-- -- 1. Sun ---------------------------------------------------------------
local sunNode = SceneNode.Create("DefaultSun")
local sun = Light.Create()
sun:SetType(0) -- DIRECTIONAL
sun:SetIntensity(3.0)
sun:SetCastShadows(true)
sunNode:AddComponent(sun)
root:AddChild(sunNode)

-- -- 2. Sky ---------------------------------------------------------------
local skyNode = SceneNode.Create("Sky")
local sky = SkyAtmosphere.Create()
sky:SetEnabled(true)
sky:SetSunLightName("DefaultSun")
sky:SetSunFromTod(true)
sky:SetTimeHours(13.0)
sky:SetAutoAdvance(true)
sky:SetDayLengthMinutes(20.0)
sky:SetCloudsEnabled(true)
-- Engine/ prefix -> engine resource root from any working dir; the moon
-- disc draws only with a texture bound, so night was pure dark without it
sky:SetMoonTexturePath("Engine/Texture/Sky/moon.png")
sky:SetCloudNoisePath("Engine/Texture/Sky/cloud_noise.png")
skyNode:AddComponent(sky)
root:AddChild(skyNode)

-- -- 3. Ocean -------------------------------------------------------------
local oceanNode = SceneNode.Create("Ocean")
local ocean = OceanComponent.Create()
-- GPU generation was removed (change: remove-gpu-ocean-generation);
-- the engine always resolves the baked asset now. SetWaveSource is a
-- no-op kept for back-compat with older scenes.
ocean:SetWaveAssetPath("Engine/Ocean/ocean.json") -- engine baseline bake (Resource/Ocean/)
oceanNode:AddComponent(ocean)
if ocean:GetResolvedSource() ~= 1 then
    fail("ocean did not resolve the baked asset: " .. ocean:GetResolvedReason())
end
root:AddChild(oceanNode)
print("setup_ocean_base: ocean verts " .. ocean:GetOceanVertexCount())

-- -- 4. Camera ------------------------------------------------------------
local camNode = SceneNode.Create("MainCamera")
local cam = Camera.Create()
cam:PerspectiveFov(60.0, 16.0 / 9.0, 10.0, 800000.0)
camNode:SetLocalPosition(Vector4(0.0, 300.0, 1500.0, 1.0))
camNode:AddComponent(cam)
root:AddChild(camNode)

-- -- 5. Render settings + save ---------------------------------------------
local rs = scene:GetRenderSettings()
if rs then
    rs:SetPipelinePath("Resource/Pipeline/DefferedLightingPBR.json")
    rs:SetHDR(true)
    rs:SetCascadedShadowMap(true)
    rs:SetShadowFar(20000.0)
    rs:SetCsmMapSize(2048)
    rs:SetCsmSplitBlend(0.7)
    rs:ClearChain()
    rs:AddEffect("SSAO", true)
    rs:AddEffect("SSR", true)
else
    fail("no render settings on fresh scene")
end

root:Recompose(true)

local out = "Projects/ocean/ocean_base.bin"
if not FileUtil.SaveCompressedFile(scene, FileUtil.GetAbsPath(out)) then
    fail("save failed: " .. out)
end
print("setup_ocean_base: wrote " .. out)
print("setup_ocean_base: DONE")
