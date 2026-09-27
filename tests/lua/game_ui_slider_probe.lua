-- Slider interaction probe: options open, late screenshot; an external
-- click lands on the FOV slider mid-run, the shot shows whether it moved.
local octree = OcTree.Create()
Scene.SetActive(Scene.Create("uitest_slider", "Projects/ocean/", octree))
assert(Scene.LoadActive("Projects/ocean/ocean_island.bin"))
Pipeline.SetActive(PrelightPipeline.Create("pipeline"))

rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Regular.ttf")
rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Bold.ttf")

assert(GameUI.LoadDocument("ui/options.rml"))
assert(GameUI.Show("ui/options.rml"))

Engine.run({ on_update = function(dt) end })
