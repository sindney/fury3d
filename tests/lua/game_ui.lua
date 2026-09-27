-- tests/lua/game_ui.lua -- RmlUi game-UI Lua API test.
--
-- Covers the engine GameUI table (path-keyed document control) plus the
-- rmlui plugin surface the game scripts build on: per-document visibility,
-- inline onclick handlers, AddEventListener, and Lua data models driving
-- {{ }} text interpolation. No GL anywhere.
--
--   ./fury exec Projects/ocean/ocean_island.bin ../tests/lua/game_ui.lua

local function fail(msg)
    io.stderr:write("game_ui FAIL: " .. msg .. "\n")
    os.exit(1)
end

-- Inline onclick="OnTestClick()" compiles to a closure that resolves the
-- global at event time, so defining it before the click is enough.
function OnTestClick()
    _G._clicks = (_G._clicks or 0) + 1
end

-- 1. GameUI table + LoadDocument/IsLoaded.
if type(GameUI) ~= "table" then
    fail("GameUI table missing (type=" .. type(GameUI) .. ")")
end

local ctx = rmlui.contexts and rmlui.contexts["main"]
if ctx == nil then
    fail("rmlui.contexts['main'] missing")
end

-- The {{ message }} binding resolves against the data model at load time,
-- so it must exist before the document is loaded.
local model = ctx:OpenDataModel("test", { message = "hello" })
if model == nil then
    fail("ctx:OpenDataModel('test', ...) returned nil")
end

local path = "Engine/Ui/game_ui_test.rml"
local loaded = GameUI.LoadDocument(path)
if loaded ~= path then
    fail("GameUI.LoadDocument returned " .. tostring(loaded) .. ", expected " .. path)
end
if not GameUI.IsLoaded(path) then
    fail("GameUI.IsLoaded('" .. path .. "') false after load")
end

local doc = ctx.documents[path]
if doc == nil then
    fail("rmlui.contexts['main'].documents['" .. path .. "'] nil after load")
end
ctx:Update()

-- 2. Path-keyed Show/Hide/Toggle. The Lua bindings expose no IsVisible;
-- doc.style.visibility reads the computed property Hide()/Show() set.
if GameUI.Show(path) ~= true then fail("GameUI.Show('" .. path .. "') did not return true") end
ctx:Update()
if doc.style.visibility ~= "visible" then
    fail("after Show: doc.style.visibility = " .. tostring(doc.style.visibility))
end

if GameUI.Hide(path) ~= true then fail("GameUI.Hide('" .. path .. "') did not return true") end
ctx:Update()
if doc.style.visibility ~= "hidden" then
    fail("after Hide: doc.style.visibility = " .. tostring(doc.style.visibility))
end

if GameUI.Show(path) ~= true then fail("GameUI.Show (second) did not return true") end
ctx:Update()
if doc.style.visibility ~= "visible" then
    fail("after re-Show: doc.style.visibility = " .. tostring(doc.style.visibility))
end

if GameUI.Toggle(path) ~= true then fail("GameUI.Toggle #1 did not return true") end
ctx:Update()
if doc.style.visibility ~= "hidden" then
    fail("after Toggle #1: doc.style.visibility = " .. tostring(doc.style.visibility))
end
if GameUI.Toggle(path) ~= true then fail("GameUI.Toggle #2 did not return true") end
ctx:Update()
if doc.style.visibility ~= "visible" then
    fail("after Toggle #2: doc.style.visibility = " .. tostring(doc.style.visibility))
end

if GameUI.Show("ui/nope.rml") ~= false then
    fail("GameUI.Show('ui/nope.rml') did not return false")
end

-- 3. Inline onclick handler via a dispatched click.
local btn = doc:GetElementById("btn")
if btn == nil then fail("GetElementById('btn') nil") end
_G._clicks = 0
btn:DispatchEvent("click", { button = 0 })
ctx:Update()
if _G._clicks ~= 1 then
    fail("inline onclick: _clicks = " .. tostring(_G._clicks) .. ", expected 1")
end

-- 4. AddEventListener sees the same click; inline handler still fires.
_G._heard = false
btn:AddEventListener("click", function(ev) _G._heard = true end)
btn:DispatchEvent("click", { button = 0 })
ctx:Update()
if _G._heard ~= true then
    fail("AddEventListener callback not invoked on click")
end
if _G._clicks ~= 2 then
    fail("after listener click: _clicks = " .. tostring(_G._clicks) .. ", expected 2")
end

-- 5. Data model drove the label text at load; assigning through the model
-- userdata marks the variable dirty and Update() rebinds the text.
local label = doc:GetElementById("label")
if label == nil then fail("GetElementById('label') nil") end
if label.inner_rml ~= "hello" then
    fail("label.inner_rml = " .. tostring(label.inner_rml) .. ", expected 'hello'")
end
model.message = "world"
ctx:Update()
if label.inner_rml ~= "world" then
    fail("label.inner_rml = " .. tostring(label.inner_rml) .. ", expected 'world'")
end

-- 6. Capture queries return booleans without error.
if type(GameUI.WantCaptureMouse()) ~= "boolean" then
    fail("GameUI.WantCaptureMouse() did not return a boolean")
end
if type(GameUI.WantCaptureKeyboard()) ~= "boolean" then
    fail("GameUI.WantCaptureKeyboard() did not return a boolean")
end

-- 2b. Close by path: destruction is deferred to the next Update().
if GameUI.Close(path) ~= true then fail("GameUI.Close('" .. path .. "') did not return true") end
ctx:Update()
if GameUI.IsLoaded(path) ~= false then
    fail("GameUI.IsLoaded still true after Close + Update")
end

print("game_ui OK")
os.exit(0)
