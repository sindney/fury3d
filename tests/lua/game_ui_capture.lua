-- tests/lua/game_ui_capture.lua -- capture gating (WantCaptureMouse) asserts.
--
-- Loads the island demo documents headless and drives the context mouse via
-- ProcessMouseMove (bound on the rmlui Lua Context): hovering an interactive
-- panel/button captures the mouse; hovering document background (the main
-- menu's pointer-events:none dead zone, the HUD's pointer-events:none
-- crosshair/fps) does not.
--
--   ./fury exec Projects/ocean/ocean_island.bin ../tests/lua/game_ui_capture.lua

local function fail(msg)
    io.stderr:write("game_ui_capture FAIL: " .. msg .. "\n")
    os.exit(1)
end

local ctx = rmlui.contexts["main"]
if ctx == nil then fail("rmlui.contexts['main'] missing") end

-- Documents cache failed font lookups: fonts before documents.
rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Regular.ttf")
rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Bold.ttf")

local DOC_MENU = "ui/main_menu.rml"
local DOC_PAUSE = "ui/pause_menu.rml"
local DOC_HUD = "ui/hud.rml"

for _, p in ipairs({ DOC_MENU, DOC_PAUSE, DOC_HUD }) do
    if GameUI.LoadDocument(p) ~= p then fail("LoadDocument failed: " .. p) end
end
GameUI.Show(DOC_MENU)
GameUI.Hide(DOC_PAUSE)
GameUI.Hide(DOC_HUD)
-- two updates: layout + font metric resolution
ctx:Update()
ctx:Update()

local W = ctx.dimensions.x
local H = ctx.dimensions.y
if W < 640 or H < 480 then
    fail("context too small for layout assumptions: " .. W .. "x" .. H)
end

-- absolute center of an element, walking the offset_parent chain
local function element_point(el)
    local x = el.offset_left + el.offset_width / 2
    local y = el.offset_top + el.offset_height / 2
    local p = el.offset_parent
    while p ~= nil do
        x = x + p.offset_left
        y = y + p.offset_top
        p = p.offset_parent
    end
    return x, y
end

local function hover(x, y)
    ctx:ProcessMouseMove(math.floor(x), math.floor(y), 0)
    ctx:Update()
    return GameUI.WantCaptureMouse()
end

-- main menu: buttons inside the left panel capture
local menu = ctx.documents[DOC_MENU]
local start_btn = menu:GetElementById("start")
if start_btn == nil then fail("main_menu #start missing") end
local bx, by = element_point(start_btn)
if not hover(bx, by) then
    fail("hover over main-menu button: WantCaptureMouse false")
end

-- main menu dead zone right of the panel: .menu-root is pointer-events:none,
-- so the document background hovers and must NOT capture
local panel = menu:QuerySelector(".menu-left")
if panel == nil then fail("main_menu .menu-left missing") end
local _, py = element_point(panel)
local dead_x = panel.offset_left + panel.offset_width + 40 -- panel is at root origin
if hover(dead_x, py) then
    fail("hover over main-menu dead zone: WantCaptureMouse true")
end

-- HUD: crosshair and fps are pointer-events:none, hovering them must
-- fall through to the document background (no capture)
GameUI.Hide(DOC_MENU)
GameUI.Show(DOC_HUD)
ctx:Update()
local hud = ctx.documents[DOC_HUD]
local cross = hud:GetElementById("crosshair")
if cross == nil then fail("hud #crosshair missing") end
local cx, cy = element_point(cross)
if hover(cx, cy) then
    fail("hover over HUD crosshair: WantCaptureMouse true")
end
local fps = hud:GetElementById("fps")
if fps == nil then fail("hud #fps missing") end
local fx, fy = element_point(fps)
if hover(fx, fy) then
    fail("hover over HUD fps: WantCaptureMouse true")
end

-- pause menu: buttons capture, and so does the full-screen .backdrop away
-- from the centered modal
GameUI.Hide(DOC_HUD)
GameUI.Show(DOC_PAUSE)
ctx:Update()
local pause = ctx.documents[DOC_PAUSE]
local cont = pause:GetElementById("continue")
if cont == nil then fail("pause_menu #continue missing") end
local px, py2 = element_point(cont)
if not hover(px, py2) then
    fail("hover over pause-menu button: WantCaptureMouse false")
end
if not hover(24, H - 24) then
    fail("hover over pause backdrop: WantCaptureMouse false")
end

-- nothing visible: no capture
GameUI.Hide(DOC_PAUSE)
ctx:Update()
if GameUI.WantCaptureMouse() then
    fail("no documents visible: WantCaptureMouse true")
end

print("game_ui_capture OK")
os.exit(0)
