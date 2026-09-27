-- Swim mode verify (ISLANDGAME_NO_RUN seam): teleport into deep ocean,
-- drive frames, assert swim engages and the body bobs to the float line.
_G.ISLANDGAME_NO_RUN = true
dofile("IslandGame.lua")
_G.ISLANDGAME.on_init()
Physics.SetEnabled(true)
Menu_Start()

local IG = _G.ISLANDGAME
local root = Scene.GetActive():GetRootNode()
local ocean = root:FindChildRecursively("Ocean"):GetOceanComponent()

-- deep water: far offshore where terrain is below the waterline
IG.debug_teleport(0.0, 200.0, 30000.0)
for i = 1, 5 do IG.on_update(1.0 / 60.0) end
assert(IG.is_swimming(), "swim should engage in deep water below the surface")

Physics.Step(75)  -- ~3s: servo glides the feet to the float line
local p = root:FindChildRecursively("Player"):GetWorldPosition()
local wave = ocean:WaveHeightAtWorld(p.x, p.z)
local feet_target = wave - 150.0
print(string.format("swim: player y=%.1f wave=%.1f float=%.1f", p.y, wave, feet_target))
assert(math.abs(p.y - feet_target) < 40.0, "player should bob at the float line")

-- back on land: swim disengages
IG.debug_teleport(0.0, 2000.0, 0.0)
for i = 1, 5 do IG.on_update(1.0 / 60.0) end
assert(not IG.is_swimming(), "swim should disengage on land")

print("game_ui_swim OK")
os.exit(0)
