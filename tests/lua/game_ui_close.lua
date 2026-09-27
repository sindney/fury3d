-- Windowed GameUI shutdown repro: real window + GL uploads + Window.Close.
-- Run: ./fury ../tests/lua/game_ui_close.lua   (from examples/)
local octree = OcTree.Create()
Scene.SetActive(Scene.Create("uitest_close", "Projects/ocean/", octree))
assert(Scene.LoadActive("Projects/ocean/ocean_island.bin"))
Pipeline.SetActive(PrelightPipeline.Create("pipeline"))

assert(GameUI.LoadDocument("ui/main_menu.rml"))
assert(GameUI.Show("ui/main_menu.rml"))

local frame = 0
Engine.run({
	on_update = function(dt)
		frame = frame + 1
		if frame >= 30 then
			Window.Close()
		end
	end,
})
print("game_ui_close OK, frames=" .. tostring(frame))
