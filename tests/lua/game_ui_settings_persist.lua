-- Settings persistence restart verify (task 9.6): seed a cfg with
-- non-defaults, boot the demo headless (ISLANDGAME_NO_RUN seam), open
-- options, assert the panel reflects the persisted values.
local CFG = "Projects/ocean/game_settings.cfg"

-- back up a pre-existing cfg
local backup = nil
do
	local f = io.open(CFG, "rb")
	if f then backup = f:read("a") f:close() end
end

local f = assert(io.open(CFG, "wb"))
f:write("shadow_quality=3\nfps_cap=60\nvsync=true\nhide_cursor=false\nsens=2.5\n")
f:close()

local function restore()
	if backup then
		local w = assert(io.open(CFG, "wb"))
		w:write(backup) w:close()
	else
		os.remove(CFG)
	end
end

local function check(label, fn)
	local ok, res = pcall(fn)
	if not ok or not res then
		restore()
		print("PERSIST FAIL: " .. label)
		os.exit(1)
	end
end

_G.ISLANDGAME_NO_RUN = true
dofile("IslandGame.lua")
_G.ISLANDGAME.on_init()

-- open options (syncs the panel from S), then read the doc
Menu_OpenOptions("main")
local doc = rmlui.contexts["main"].documents["ui/options.rml"]
check("options doc", function() return doc ~= nil end)

check("shadow-3 active", function()
	return doc:GetElementById("shadow-3"):IsClassSet("active")
end)
check("shadow-2 inactive", function()
	return not doc:GetElementById("shadow-2"):IsClassSet("active")
end)
check("shadow-3 label=HIGH", function()
	return doc:GetElementById("shadow-3").inner_rml == "HIGH"
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
