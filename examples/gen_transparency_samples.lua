-- Generates the transparency sample scenes:
--   Resource/Scene/GlassVaseFlowers.json  (BLEND glass + MASK foliage)
--   Resource/Scene/AlphaBlendModeTest.json (OPAQUE/MASK/BLEND rows)
-- Run from examples/: ./fury gen_transparency_samples.lua

local samples = {
    {
        src = "glTF-Sample-Assets/Models/GlassVaseFlowers/glTF-Binary/GlassVaseFlowers.glb",
        dst = "Projects/glass/GlassVaseFlowers.json",
    },
    {
        src = "glTF-Sample-Assets/Models/AlphaBlendModeTest/glTF-Binary/AlphaBlendModeTest.glb",
        dst = "Projects/glass/AlphaBlendModeTest.json",
    },
}

for _, s in ipairs(samples) do
    print("[gen] importing " .. s.src)
    local scene = Importer.LoadScene(s.src)
    if not scene then error("Importer.LoadScene returned nil for " .. s.src) end
    print("[gen] saving " .. s.dst)
    if not FileUtil.SaveFile(scene, s.dst) then error("SaveFile failed for " .. s.dst) end
end

print("[gen] done")
