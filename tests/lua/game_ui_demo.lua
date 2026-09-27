-- tests/lua/game_ui_demo.lua -- headless menu-flow test for IslandGame.lua.
--
-- Sets ISLANDGAME_NO_RUN so the demo skips Engine.run and exports on_init /
-- on_update / to_pause / state on _G.ISLANDGAME, then drives the real state
-- machine: menu -> game -> pause -> options -> menu, plus settings
-- persistence asserted at the cfg file level.
--
-- ESC cannot be simulated headless (InputUtil has no key-injection seam), so
-- the pause transition is asserted through the exported to_pause() -- the
-- same function on_update's ESC edge calls -- plus the real user path back
-- to the pause screen via Menu_OpenOptions('pause') / Menu_CloseOptions().
--
-- A pre-existing Projects/ocean/game_settings.cfg is backed up and restored
-- (the save-path asserts rewrite it); with no pre-existing file the test
-- deletes the one it wrote.
--
--   ./fury exec Projects/ocean/ocean_island.bin ../tests/lua/game_ui_demo.lua

local CFG = "Projects/ocean/game_settings.cfg"

local cfg_backup = nil
do
    local f = io.open(CFG, "rb")
    if f then cfg_backup = f:read("*a") f:close() end
end

local function cfg_restore()
    if cfg_backup ~= nil then
        local f = io.open(CFG, "wb")
        if f then f:write(cfg_backup) f:close() end
    else
        os.remove(CFG)
    end
end

local function cfg_value(key)
    local f = io.open(CFG, "rb")
    if not f then return nil end
    local s = f:read("*a") f:close()
    return s:match(key .. "=(%S-)\n")
end

local function fail(msg)
    cfg_restore()
    io.stderr:write("game_ui_demo FAIL: " .. msg .. "\n")
    os.exit(1)
end

Physics.SetEnabled(true)

_G.ISLANDGAME_NO_RUN = true
dofile("IslandGame.lua")

local IG = _G.ISLANDGAME
if type(IG) ~= "table" or type(IG.on_init) ~= "function" then
    fail("ISLANDGAME hooks missing (flag must be set before dofile)")
end

IG.on_init()

local ctx = rmlui.contexts["main"]
if ctx == nil then fail("rmlui.contexts['main'] missing") end

local DOC_MENU = "ui/main_menu.rml"
local DOC_PAUSE = "ui/pause_menu.rml"
local DOC_OPTIONS = "ui/options.rml"
local DOC_HUD = "ui/hud.rml"

local function expect_vis(path, want)
    local d = ctx.documents[path]
    if d == nil then fail("document not loaded: " .. path) end
    ctx:Update()
    local v = d.style.visibility
    if v ~= want then
        fail(path .. " visibility=" .. tostring(v) .. ", want " .. want)
    end
end

-- boot: menu up, everything else down
for _, p in ipairs({ DOC_MENU, DOC_PAUSE, DOC_OPTIONS, DOC_HUD }) do
    if not GameUI.IsLoaded(p) then fail("GameUI.IsLoaded false: " .. p) end
end
if IG.state() ~= "menu" then fail("initial state=" .. tostring(IG.state())) end
expect_vis(DOC_MENU, "visible")
expect_vis(DOC_PAUSE, "hidden")
expect_vis(DOC_OPTIONS, "hidden")
expect_vis(DOC_HUD, "hidden")

-- menu -> game via the real button click (inline onclick -> Menu_Start)
local start_btn = ctx.documents[DOC_MENU]:GetElementById("start")
if start_btn == nil then fail("main_menu #start missing") end
start_btn:DispatchEvent("click", { button = 0 })
if IG.state() ~= "game" then fail("after Start state=" .. tostring(IG.state())) end
expect_vis(DOC_MENU, "hidden")
expect_vis(DOC_HUD, "visible")
expect_vis(DOC_PAUSE, "hidden")

-- one on_update tick: menu drift camera + pl:Execute run headless
IG.on_update(1.0 / 60.0)

-- game -> pause (ESC edge path: same to_pause on_update calls)
IG.to_pause()
if IG.state() ~= "pause" then fail("after to_pause state=" .. tostring(IG.state())) end
expect_vis(DOC_PAUSE, "visible")
expect_vis(DOC_HUD, "hidden")
expect_vis(DOC_MENU, "hidden")

-- pause -> game via Continue
Menu_Continue()
if IG.state() ~= "game" then fail("after Continue state=" .. tostring(IG.state())) end
expect_vis(DOC_HUD, "visible")

-- user path back to the pause screen: options opened from pause return to it
Menu_OpenOptions("pause")
expect_vis(DOC_OPTIONS, "visible")
expect_vis(DOC_PAUSE, "hidden")
Menu_CloseOptions()
expect_vis(DOC_PAUSE, "visible")
expect_vis(DOC_OPTIONS, "hidden")

-- pause -> menu
Menu_ExitToMain()
if IG.state() ~= "menu" then fail("after ExitToMain state=" .. tostring(IG.state())) end
expect_vis(DOC_MENU, "visible")
expect_vis(DOC_PAUSE, "hidden")

-- options from the main menu: toggles flip the segment text, close saves
Menu_OpenOptions("main")
expect_vis(DOC_OPTIONS, "visible")
expect_vis(DOC_MENU, "hidden")

local vsync_seg = ctx.documents[DOC_OPTIONS]:GetElementById("vsync")
if vsync_seg == nil then fail("options #vsync missing") end
local vsync_before = (cfg_value("vsync") == "true") -- S seeded from cfg/default
Opt_Toggle("vsync")
ctx:Update()
local seg_want = vsync_before and "OFF" or "ON"
if vsync_seg.inner_rml ~= seg_want then
    fail("vsync segment=" .. tostring(vsync_seg.inner_rml) .. ", want " .. seg_want)
end
Opt_FpsCap(90)
Opt_Fov(80)

Menu_CloseOptions() -- saves cfg, back to the main menu
expect_vis(DOC_MENU, "visible")

if cfg_value("vsync") ~= tostring(not vsync_before) then
    fail("cfg vsync=" .. tostring(cfg_value("vsync")) .. ", want " .. tostring(not vsync_before))
end
-- numbers may round-trip through the slider widget as floats ("80.0")
if tonumber(cfg_value("fps_cap")) ~= 90 then
    fail("cfg fps_cap=" .. tostring(cfg_value("fps_cap")) .. ", want 90")
end
if tonumber(cfg_value("fov")) ~= 80 then
    fail("cfg fov=" .. tostring(cfg_value("fov")) .. ", want 80")
end

-- Exit to Desktop saves again (Window.Close is a no-op headless)
Menu_Exit()
if cfg_value("vsync") ~= tostring(not vsync_before) then
    fail("cfg vsync after Menu_Exit=" .. tostring(cfg_value("vsync")))
end

cfg_restore()
print("game_ui_demo OK")
os.exit(0)
