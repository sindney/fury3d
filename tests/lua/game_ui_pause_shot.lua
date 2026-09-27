-- Pause-state visual verify: hud + pause menu over the island scene.
local octree = OcTree.Create()
Scene.SetActive(Scene.Create("uitest_pause", "Projects/ocean/", octree))
assert(Scene.LoadActive("Projects/ocean/ocean_island.bin"))
Pipeline.SetActive(PrelightPipeline.Create("pipeline"))

rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Regular.ttf")
rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Bold.ttf")

assert(GameUI.LoadDocument("ui/hud.rml"))
assert(GameUI.Show("ui/hud.rml"))
assert(GameUI.LoadDocument("ui/pause_menu.rml"))
assert(GameUI.Show("ui/pause_menu.rml"))

Engine.run({ on_update = function(dt) end })
