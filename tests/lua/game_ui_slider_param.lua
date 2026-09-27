-- Slider change-event plumbing: the handler must receive the value via
-- event.parameters.value (WidgetSlider dispatches change BEFORE writing
-- the value attribute).
Scene.SetActive(Scene.Create("ui_slider_param", "Projects/ocean/", OcTree.Create()))
rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Regular.ttf")

local got_fov, got_sens = nil, nil
function Opt_Fov(v) got_fov = v end
function Opt_Sens(v) got_sens = v end

assert(GameUI.LoadDocument("ui/options.rml"), "options.rml loads")
local doc = rmlui.contexts["main"].documents["ui/options.rml"]
assert(doc, "options doc in context")

local fov = doc:GetElementById("fov")
assert(fov, "fov element")
fov:DispatchEvent("change", { value = 99 })
assert(got_fov == 99, "Opt_Fov got event value 99, got " .. tostring(got_fov))

local sens = doc:GetElementById("sens")
assert(sens, "sens element")
sens:DispatchEvent("change", { value = 2.5 })
assert(got_sens == 2.5, "Opt_Sens got event value 2.5, got " .. tostring(got_sens))

print("game_ui_slider_param OK")
os.exit(0)
