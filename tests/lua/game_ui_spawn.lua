-- tests/lua/game_ui_spawn.lua - PlayerSpawn marker end-to-end check for
-- IslandGame.lua.
--
--   ./fury exec Projects/ocean/ocean_island.bin ../tests/lua/game_ui_spawn.lua
--
-- The scene bin carries a "PlayerSpawn" node (placed via the engine's own
-- save path). After ISLANDGAME_NO_RUN on_init, the demo's player must be at
-- the marker's world position plus the demo's 100cm clearance, instead of
-- the terrain-probe / island-center fallbacks.

local function fail(msg)
	io.stderr:write("game_ui_spawn FAIL: " .. msg .. "\n")
	os.exit(1)
end

local function near(a, b, eps, what)
	if math.abs(a - b) > (eps or 0.01) then
		fail(string.format("%s: want %s, got %s", what, tostring(b), tostring(a)))
	end
end

Physics.SetEnabled(true)

_G.ISLANDGAME_NO_RUN = true
dofile("IslandGame.lua")

local IG = _G.ISLANDGAME
if type(IG) ~= "table" or type(IG.on_init) ~= "function" then
	fail("ISLANDGAME hooks missing (flag must be set before dofile)")
end

IG.on_init()

local root = Scene.GetActive():GetRootNode()
local spawn = root:FindChildRecursively("PlayerSpawn")
if not spawn then fail("PlayerSpawn node missing from loaded scene") end
local player = root:FindChildRecursively("Player")
if not player then fail("Player node missing after on_init") end

-- the marker must not carry a camera (demo camera selection stays singular)
if spawn:GetCamera() then fail("PlayerSpawn unexpectedly has a Camera") end

local sp = spawn:GetWorldPosition()
local pp = player:GetLocalPosition()
near(pp.x, sp.x, 0.01, "player.x vs spawn.x")
near(pp.y, sp.y + 100.0, 0.01, "player.y vs spawn.y + clearance")
near(pp.z, sp.z, 0.01, "player.z vs spawn.z")

print("game_ui_spawn OK")
os.exit(0)
