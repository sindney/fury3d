-- os.exit from a windowed run with the render thread up: exit-path repro.
local octree = OcTree.Create()
Scene.SetActive(Scene.Create("uitest_osexit", "Projects/ocean/", octree))
assert(Scene.LoadActive("Projects/ocean/ocean_island.bin"))
Pipeline.SetActive(PrelightPipeline.Create("pipeline"))

assert(GameUI.LoadDocument("ui/main_menu.rml"))
assert(GameUI.Show("ui/main_menu.rml"))

local frame = 0
Engine.run({
	on_update = function(dt)
		frame = frame + 1
		if frame >= 30 then
			os.exit(0)
		end
	end,
})
