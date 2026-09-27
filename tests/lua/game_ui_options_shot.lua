-- Options menu visual verify: styled shot, then a mid-run resolution
-- change to prove backdrop/modal track the new window size.
local octree = OcTree.Create()
Scene.SetActive(Scene.Create("uitest_options", "Projects/ocean/", octree))
assert(Scene.LoadActive("Projects/ocean/ocean_island.bin"))
Pipeline.SetActive(PrelightPipeline.Create("pipeline"))

rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Regular.ttf")
rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Bold.ttf")

assert(GameUI.LoadDocument("ui/options.rml"))
assert(GameUI.Show("ui/options.rml"))

local resized = false
Engine.run({
	on_update = function(dt)
		if not resized then
			resized = true
			Window.SetResolution(1600, 900)
		end
	end,
})
