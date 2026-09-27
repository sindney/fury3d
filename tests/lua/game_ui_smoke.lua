-- tests/lua/game_ui_smoke.lua -- RmlUi Lua plugin smoke test.
--
-- The rmlui global must be registered into the engine's sol2 state
-- (LuaBindings::Register), the windowless "main" context must exist,
-- and a document must load + answer tree queries with no GL anywhere.
--
--   ./fury exec Projects/ocean/ocean_island.bin ../tests/lua/game_ui_smoke.lua

local function fail(msg)
    io.stderr:write("game_ui_smoke FAIL: " .. msg .. "\n")
    os.exit(1)
end

-- The rmlui global is a plugin userdata (not a table) with __index
-- metamethods; what matters is that it exists and resolves contexts.
if rmlui == nil or rmlui.contexts == nil then
    fail("rmlui global or rmlui.contexts missing (type=" .. type(rmlui) .. ")")
end

local ctx = rmlui.contexts and rmlui.contexts["main"]
if ctx == nil then
    fail("rmlui.contexts['main'] missing")
end

local doc = ctx:LoadDocument("Engine/Ui/smoke.rml")
if doc == nil then
    fail("LoadDocument('Engine/Ui/smoke.rml') returned nil")
end

local box = doc:GetElementById("box")
if box == nil then
    fail("GetElementById('box') returned nil")
end

doc:Show()
ctx:Update()
-- RmlUi 6.3 runs layout inside Render(), not Update(); the renderer only
-- records here, so this is safe with no GL.
ctx:Render()
local w = box.offset_width
if math.abs(w - 100.0) > 0.5 then
    fail("box.offset_width = " .. tostring(w) .. ", expected 100")
end

-- sol2 still works in the same state (coexistence check).
local scene = Scene.GetActive()
if scene == nil then
    fail("Scene.GetActive() nil - sol2 bindings broken")
end

print("game_ui_smoke OK")
os.exit(0)
