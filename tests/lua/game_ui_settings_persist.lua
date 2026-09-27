-- Settings persistence restart verify (task 9.6): seed a cfg with
-- non-defaults, boot the demo headless (ISLANDGAME_NO_RUN seam), open
-- options, assert the panel reflects the persisted values.
local CFG = "Projects/ocean/game_settings.cfg"

-- back up a pre-existing cfg
local backup = nil
do
	local f = io.open(CFG, "r")
	if f then backup = f:read("a") f:close() end
end

local f = assert(io.open(CFG, "w"))
f:write("fov=95\nfps_cap=60\nvsync=true\nhide_cursor=false\nsens=2.5\n")
f:close()

local function restore()
	if backup then
		local w = assert(io.open(CFG, "w"))
		w:write(backup) w:close()
	else
		os.remove(CFG)
	end
end

_G.ISLANDGAME_NO_RUN = true
dofile("IslandGame.lua")
_G.ISLANDGAME.on_init()

-- open options (syncs the panel from S), then read the doc
Menu_OpenOptions("main")
local doc = rmlui.contexts["main"].documents["ui/options.rml"]
assert(doc, "options doc")

local function check(label, fn)
	if not fn() then
		restore()
		print("PERSIST FAIL: " .. label)
		os.exit(1)
	end
end

check("fov slider=95", function()
	return tonumber(doc:GetElementById("fov"):GetAttribute("value")) == 95
end)
check("fov label=95", function()
	return doc:GetElementById("fov-value").inner_rml == "95"
end)
check("fps-60 active", function()
	return doc:GetElementById("fps-60"):IsClassSet("active")
end)
check("fps-144 inactive", function()
	return not doc:GetElementById("fps-144"):IsClassSet("active")
end)
check("vsync ON", function()
	return doc:GetElementById("vsync").inner_rml == "ON"
end)
check("hide_cursor OFF", function()
	return doc:GetElementById("hide_cursor").inner_rml == "OFF"
end)
check("sens label=2.5", function()
	return doc:GetElementById("sens-value").inner_rml == "2.5"
end)

restore()
print("game_ui_settings_persist OK")
os.exit(0)
