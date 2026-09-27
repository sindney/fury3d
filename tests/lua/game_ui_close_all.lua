-- Windowed GameUI shutdown repro with all demo docs open (pause-state shape).
local octree = OcTree.Create()
Scene.SetActive(Scene.Create("uitest_close2", "Projects/ocean/", octree))
assert(Scene.LoadActive("Projects/ocean/ocean_island.bin"))
Pipeline.SetActive(PrelightPipeline.Create("pipeline"))

for _, doc in ipairs({ "ui/main_menu.rml", "ui/pause_menu.rml", "ui/options.rml", "ui/hud.rml" }) do
	assert(GameUI.LoadDocument(doc))
	assert(GameUI.Show(doc))
end

local frame = 0
Engine.run({
	on_update = function(dt)
		frame = frame + 1
		if frame >= 30 then
			Window.Close()
		end
	end,
})
print("game_ui_close_all OK, frames=" .. tostring(frame))
