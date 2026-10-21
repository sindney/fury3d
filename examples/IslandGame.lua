-- IslandGame.lua - island demo game shell (change: add-rmlui-game-ui).
--
--   ./fury IslandGame.lua
--
-- Flow: main menu over a drifting island camera -> Start -> FPS player on
-- the beach -> ESC pause menu -> options (shared) / exit paths. Settings
-- persist to Projects/ocean/game_settings.cfg (flat key=value).

local octree = nil
local scene = nil
local pl = nil

local state = "menu"           -- menu | game | pause
local options_open = false     -- options overlay shown on top of menu/pause
local options_from = "main"    -- main | pause

local menu_cam_node = nil
local player_node = nil
local player_cam_node = nil
local player_camera = nil
local controller = nil
local spawn_pos = nil
local sky_comp = nil

-- swim state (game state only); ocean/terrain cached at on_init
local ocean = nil          -- OceanComponent
local terrain = nil        -- Terrain component
local swimming = false
local cam_height = 170.0   -- eased eye height: 170 standing, 162 swimming
local drift_yaw = 0.0
local drift_center = Vector4(0.0, 1500.0, 0.0, 1.0)
local drift_radius = 6500.0
local drift_height = 1800.0
local drift_pitch = -0.18

-- documents (resolved against the scene working dir = Projects/ocean/)
local DOC_MENU = "ui/main_menu.rml"
local DOC_PAUSE = "ui/pause_menu.rml"
local DOC_OPTIONS = "ui/options.rml"
local DOC_HUD = "ui/hud.rml"

-- ---------------------------------------------------------------- settings

local S = {
	res_w = 1280, res_h = 720,
	fps_cap = 144, vsync = false,
	postfx = true, ssao = true, ssr = true, fxaa = true,
	-- shadow quality: 0 = off, 1 = low (1024), 2 = med (2048), 3 = high (4096)
	shadow_quality = 2,
	sens = 1.0,
	hdr = true,
	hide_cursor = true,
	rhi = "",   -- "" = platform default; applies at next launch
}

local CFG_PATH = "Projects/ocean/game_settings.cfg"

local function settings_save()
	local f = io.open(CFG_PATH, "w")
	if not f then return end
	for k, v in pairs(S) do
		if k:sub(1, 1) ~= "_" then   -- "_"-prefixed keys are session state
			f:write(tostring(k) .. "=" .. tostring(v) .. "\n")
		end
	end
	f:close()
end

local function settings_load()
	local f = io.open(CFG_PATH, "r")
	if not f then return end
	for line in f:lines() do
		local k, v = line:match("^([%w_]+)=(.+)$")
		if k and v and S[k] ~= nil then
			if type(S[k]) == "number" then S[k] = tonumber(v) or S[k]
			elseif type(S[k]) == "boolean" then S[k] = (v == "true")
			else S[k] = v end
			if k == "res_w" then S._res_persisted = true end
		end
	end
	f:close()
end

local function rs_apply()
	local rs = scene:GetRenderSettings()
	if rs then
		Pipeline.ApplyRenderSettings(pl, rs)
	end
end

-- PostProcess.SetEffectEnabled wants a 1-based chain index, not a name.
-- Walk the chain table (its iter index already matches SetEffectEnabled).
local function effect_index(rs, name)
	if not rs then return -1 end
	for i, e in ipairs(rs:GetChain()) do
		if e.effectName == name then return i end
	end
	return -1
end

local function effect_set(rs, name, enabled)
	local idx = effect_index(rs, name)
	if idx >= 0 then rs:SetEffectEnabled(idx, enabled) end
end

local function settings_apply(key)
	local rs = scene:GetRenderSettings()
	if key == "fps_cap" then
		Window.SetFpsCap(S.fps_cap > 0 and S.fps_cap or false)
	elseif key == "vsync" then
		Window.SetVsync(S.vsync)
	elseif key == "resolution" then
		Window.SetResolution(S.res_w, S.res_h)
	elseif key == "postfx" then
		if rs then
			effect_set(rs, "SSAO", S.postfx and S.ssao)
			effect_set(rs, "SSR", S.postfx and S.ssr)
			effect_set(rs, "FXAA", S.postfx and S.fxaa)
			rs_apply()
		end
	elseif key == "ssao" or key == "ssr" or key == "fxaa" then
		if rs then
			effect_set(rs, key:upper(), S[key] and S.postfx)
			rs_apply()
		end
	elseif key == "shadow_quality" then
		if rs then
			local q = S.shadow_quality
			rs:SetCascadedShadowMap(q > 0)
			if q > 0 then rs:SetCsmMapSize(({ 0, 1024, 2048, 4096 })[q]) end
			rs_apply()
		end
	elseif key == "hdr" then
		if rs then rs:SetHDR(S.hdr); rs_apply() end
	elseif key == "sens" then
		if controller then controller:SetMouseSensitivity(S.sens) end
	elseif key == "hide_cursor" then
		if state == "game" then cursor_for_game() end
	end
end

local function settings_apply_all()
	settings_apply("fps_cap")
	settings_apply("vsync")
	settings_apply("postfx")
	settings_apply("shadow_quality")
	settings_apply("hdr")
	settings_apply("sens")
end

-- ---------------------------------------------------------------- ui sync

local function ui_doc(path)
	return rmlui.contexts["main"].documents[path]
end

local function set_seg(doc, id, active, text)
	local el = doc and doc:GetElementById(id)
	if not el then return end
	el:SetClass("active", active)
	if text then el.inner_rml = text end
end

local function options_sync()
	local doc = ui_doc(DOC_OPTIONS)
	if not doc then return end
	-- resolution
	for _, w in ipairs({ 1280, 1920, 2560, 3840 }) do
		set_seg(doc, "res-" .. w, S.res_w == w)
	end
	-- fps cap
	for _, n in ipairs({ 60, 90, 144, 0 }) do
		set_seg(doc, "fps-" .. n, S.fps_cap == n)
	end
	-- toggles
	for _, key in ipairs({ "vsync", "postfx", "ssao", "ssr", "fxaa", "hdr", "hide_cursor" }) do
		set_seg(doc, key, S[key], S[key] and "ON" or "OFF")
	end
	-- shadow quality
	for _, n in ipairs({ 0, 1, 2, 3 }) do
		set_seg(doc, "shadow-" .. n, S.shadow_quality == n, ({ "OFF", "LOW", "MED", "HIGH" })[n + 1])
	end
	-- sliders
	local sens_el = doc:GetElementById("sens")
	if sens_el then sens_el:SetAttribute("value", tostring(S.sens)) end
	local sens_val = doc:GetElementById("sens-value")
	if sens_val then sens_val.inner_rml = string.format("%.1f", S.sens) end
	-- render backend: platform-supported list, generated (varies by OS)
	local rhi_ctl = doc:GetElementById("rhi-controls")
	if rhi_ctl then
		local names = Engine.GetSupportedRhiBackends()
		local html = {}
		for _, n in ipairs(names) do
			html[#html + 1] = '<div class="seg" id="rhi-' .. n ..
				'" onclick="Opt_Rhi(\'' .. n .. '\')">' .. n:upper() .. '</div>'
		end
		rhi_ctl.inner_rml = table.concat(html)
		for _, n in ipairs(names) do
			set_seg(doc, "rhi-" .. n, S.rhi == n or (S.rhi == "" and n == "opengl"))
		end
		local note = doc:GetElementById("rhi-note")
		if note then
			note.inner_rml = "applies at next launch (active: " .. Engine.GetRhiBackend() .. ")"
		end
	end
end

-- ---------------------------------------------------------------- states

local function cursor_for_menu()
	local input = InputUtil.Instance()
	input:SetCursorGrabbed(false)
	input:SetCursorVisible(true)
end

local function cursor_for_game()
	local input = InputUtil.Instance()
	input:SetCursorGrabbed(true)
	input:SetCursorVisible(not S.hide_cursor)
end

local function show_only(menu, pause, options, hud)
	if menu then GameUI.Show(DOC_MENU) else GameUI.Hide(DOC_MENU) end
	if pause then GameUI.Show(DOC_PAUSE) else GameUI.Hide(DOC_PAUSE) end
	if options then GameUI.Show(DOC_OPTIONS) else GameUI.Hide(DOC_OPTIONS) end
	if hud then GameUI.Show(DOC_HUD) else GameUI.Hide(DOC_HUD) end
	rmlui.contexts["main"]:Update()
end

local function to_menu()
	state = "menu"
	if controller then controller:Deactivate() end   -- freeze physics+input
	pl:SetCurrentCamera(menu_cam_node)
	cursor_for_menu()
	show_only(true, false, false, false)
end

local function to_game()
	state = "game"
	if controller then
		PlayerController.ActivateFirst(scene:GetRootNode())
	end
	-- clouds are off for the menu orbit; restore for the play view
	if sky_comp then sky_comp:SetCloudsEnabled(true) end
	cursor_for_game()
	show_only(false, false, false, true)
end

local function to_pause()
	state = "pause"
	if controller then controller:Deactivate() end
	cursor_for_menu()
	show_only(false, true, false, false)
end

-- menu callbacks (inline RML handlers land here)
function Menu_Start()
	-- fresh run from the main menu: respawn at the spawn point
	if controller and spawn_pos then controller:Teleport(spawn_pos) end
	to_game()
end
function Menu_Continue() to_game() end
function Menu_ExitToMain() to_menu() end
function Menu_Exit()
	settings_save()
	Window.Close()
end

function Menu_OpenOptions(from)
	options_from = from or "main"
	options_open = true
	options_sync()
	if options_from == "pause" then
		show_only(false, false, true, false)
	else
		show_only(false, false, true, false)
	end
end

function Menu_CloseOptions()
	settings_save()
	options_open = false
	if options_from == "pause" then
		show_only(false, true, false, false)
	else
		show_only(true, false, false, false)
	end
end

-- options callbacks
function Opt_Resolution(w, h)
	S.res_w, S.res_h = w, h
	settings_apply("resolution")
	options_sync()
end

function Opt_FpsCap(n)
	S.fps_cap = n
	settings_apply("fps_cap")
	options_sync()
end

function Opt_Toggle(key)
	S[key] = not S[key]
	settings_apply(key)
	options_sync()
end

function Opt_ShadowQuality(n)
	S.shadow_quality = n
	settings_apply("shadow_quality")
	options_sync()
end

-- Backend switching is startup-only; the choice persists and feeds the
-- Engine.run rhi option on the next launch.
function Opt_Rhi(name)
	S.rhi = name
	settings_save()
	options_sync()
end

-- The change event fires BEFORE the widget writes the value attribute,
-- so handlers take the event parameter, not GetAttribute.

function Opt_Sens(v)
	v = tonumber(v)
	if v then
		S.sens = v
		settings_apply("sens")
	end
	options_sync()
end

-- ---------------------------------------------------------------- boot

local function pick_spawn()
	-- an editor-placed PlayerSpawn node wins over the terrain probes
	local spawn_node = scene:GetRootNode():FindChildRecursively("PlayerSpawn")
	if spawn_node then
		return spawn_node:GetWorldPosition() + Vector4(0.0, 100.0, 0.0, 0.0)
	end
	local terrain_node = scene:GetRootNode():FindChildRecursively("Terrain")
	local terrain = terrain_node and terrain_node:GetTerrain()
	if terrain then
		for _, c in ipairs({ { 0, 0 }, { 3000, 0 }, { -3000, 3000 }, { 0, -4000 }, { 5000, 5000 } }) do
			local h = terrain:GetHeight(c[1], c[2])
			if h and h > 600.0 then
				return Vector4(c[1], h + 100.0, c[2], 1.0)
			end
		end
	end
	-- fallback: high above the island center, the capsule drops onto land
	return Vector4(0.0, 3000.0, 0.0, 1.0)
end

local function on_init()
	octree = OcTree.Create()

	Scene.SetActive(Scene.Create("island_game", "Projects/ocean/", octree))
	if not Scene.LoadActive("Projects/ocean/ocean_island.bin") then
		io.stderr:write("IslandGame: failed to load ocean_island.bin\n")
		Window.Close()
		return
	end
	scene = Scene.GetActive()

	do
		local ocean_node = scene:GetRootNode():FindChildRecursively("Ocean")
		ocean = ocean_node and ocean_node:GetOceanComponent()
		local terrain_node = scene:GetRootNode():FindChildRecursively("Terrain")
		terrain = terrain_node and terrain_node:GetTerrain()
	end

	Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
	pl = Pipeline.GetActive()
	local rs = scene:GetRenderSettings()
	local pipe_path = rs and rs:GetPipelinePath() or ""
	if pipe_path == "" then
		pipe_path = "Resource/Pipeline/DefferedLightingLambert.json"
	end
	local full_pipe = FileUtil.GetAbsPath(pipe_path)
	-- The pipeline file instantiates GL textures/shaders; headless tests
	-- (ISLANDGAME_NO_RUN) run without a GL context.
	if FileUtil.FileExist(full_pipe) and not _G.ISLANDGAME_NO_RUN then
		FileUtil.LoadPipelineFromFile(pl, full_pipe)
	end
	PostProcess.LoadFromDirectory("Resource/PostProcess")
	if rs then
		Pipeline.ApplyRenderSettings(pl, rs)
	end

	-- menu drift camera: low-orbit establishing shot over the island
	local skyNode = scene:GetRootNode():FindChildRecursively("Sky")
	sky_comp = skyNode and skyNode:GetSkyAtmosphere()
	if sky_comp then
		-- the bin still carries 2D-deck cloud values; tune for the
		-- volumetric model here (no scene re-save)
		sky_comp:SetCloudAltitudeKm(1.5)
		sky_comp:SetCloudThicknessKm(2.5)
		sky_comp:SetCloudFadeKm(20.0)
		sky_comp:SetCloudDensity(22.0)
		sky_comp:SetCloudScale(0.15)
		sky_comp:SetCloudTypeBias(0.15)
		sky_comp:SetCloudCoverage(0.35)
		-- cirrus: higher altitude, wispy streaks, lower density
		sky_comp:SetCirrusAltKm(4.0)
		sky_comp:SetCirrusCoverage(0.5)
		sky_comp:SetCirrusDensity(2.0)
		sky_comp:SetCirrusScale(0.05)
		-- The scene's volumetric clouds cover most altitudes and obscure the
		-- island when the menu camera frames it from above. Disable clouds
		-- for the menu; re-enabled in to_game() for the play view.
		sky_comp:SetCloudsEnabled(false)
	end

	-- pre-z on foliage: the bin's materials predate the Kraut importer's
	-- PreZ flag; wind-enabled materials are exactly the overdraw the pass
	-- targets
	Scene.ForEachNode(scene, function(n)
		local imr = n:GetInstancedMeshRender()
		if imr then
			for i = 0, imr:GetMaterialCount() - 1 do
				local m = imr:GetMaterial(i)
				if m and m:GetWindEnabled() and not m:GetPreZ() then
					m:SetPreZ(true)
				end
			end
		end
	end)
	drift_center = Vector4(0.0, 1500.0, 0.0, 1.0)
	drift_radius = 18000.0
	drift_height = 4500.0
	drift_pitch = -0.35
	menu_cam_node = SceneNode.Create("MenuCamera")
	menu_cam_node:AddComponent(Transform.Create())
	menu_cam_node:AddComponent(Camera.Create())
	scene:GetRootNode():AddChild(menu_cam_node)

	-- player: 1.8 m capsule, no visible mesh, camera at eye height
	player_node = SceneNode.Create("Player")	player_node:AddComponent(Transform.Create())
	spawn_pos = pick_spawn()
	player_node:SetLocalPosition(spawn_pos)
	player_node:Recompose(false)
	controller = CharacterController.Create()
	controller:SetHeight(180.0)
	controller:SetRadius(35.0)
	controller:SetCameraDistance(0.0)   -- first person
	controller:SetCameraHeight(170.0)   -- eye height (cm)
	controller:SetFirstPerson(true)
	controller:SetMouseSensitivity(S.sens)
	controller:SetWalkSpeed(400.0)   -- 2x engine default (200/500 felt slow
	controller:SetRunSpeed(1000.0)   -- at island scale)
	player_node:AddComponent(controller)
	scene:GetRootNode():AddChild(player_node)

	player_cam_node = SceneNode.Create("PlayerCamera")
	player_cam_node:AddComponent(Transform.Create())
	player_camera = Camera.Create()
	player_camera:PerspectiveFov(math.rad(75.0), 16.0 / 9.0, 1.0, 500000.0)
	player_cam_node:AddComponent(player_camera)
	scene:GetRootNode():AddChild(player_cam_node)
	controller:SetCameraNodeName("PlayerCamera")

	-- documents (fonts first: documents reference Lato via the stylesheet)
	rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Regular.ttf")
	rmlui:LoadFontFace("Engine/Fonts/LatoLatin-Bold.ttf")
	rmlui.contexts["main"]:Update()
	GameUI.LoadDocument(DOC_MENU)
	GameUI.LoadDocument(DOC_PAUSE)
	GameUI.LoadDocument(DOC_OPTIONS)
	GameUI.LoadDocument(DOC_HUD)
	-- Show then Update so RmlUi can resolve the font-family -> face
	-- mapping AND compute line-height/text-bbox metrics before the
	-- first user-visible frame. Without this, text returns height=1
	-- rectangles (no metrics) and nothing renders.
	for _, d in ipairs({DOC_MENU, DOC_PAUSE, DOC_OPTIONS, DOC_HUD}) do
		GameUI.Show(d)
	end
	rmlui.contexts["main"]:Update()
	rmlui.contexts["main"]:Update()

	settings_load()
	-- Resolution is special: a persisted value is applied to the window,
	-- otherwise the panel must show the window's real size, not the S default.
	if S._res_persisted then
		settings_apply("resolution")
	else
		local w, h = Window.GetResolution()
		if w > 0 then S.res_w, S.res_h = w, h end
	end
	settings_apply_all()

	to_menu()
end

local fps_accum, fps_frames = 0.0, 0
local esc_was = false

-- ------------------------------------------------------------ joystick ui

-- GamepadButton names are XInput-compatible; future work: per-controller
-- remap via HID capabilities query so non-XInput pads (e.g. Switch Pro,
-- DualShock) read the same buttons.

-- Console-style focus navigation. D-pad / left-stick feeds RmlUi's
-- spatial nav via ProcessKeyDown(arrow), and A dispatches a click on
-- whichever element currently holds focus. RmlUi handles the focus
-- highlight (see .btn:focus / .seg:focus in ui.rcss).

-- Gamepad input mode flags. Mode flips between "mouse" and "gamepad"
-- based on whichever device produced the most recent activity; the OS
-- cursor visibility follows. Verbose per-event diagnostics go through
-- the FURY log system (LogDBUG); set the engine log level to DBUG to
-- see them.
local function glog(msg)
	LogDBUG("[islandgame/joy] " .. msg)
end

-- ISLANDGAME_NO_GAMEPAD=1 disables the entire gamepad UI path so the
-- menu can be exercised with the mouse only.
local no_gamepad = (os.getenv("ISLANDGAME_NO_GAMEPAD") or "") ~= ""
if no_gamepad then
	LogINFO("[islandgame] gamepad UI disabled (ISLANDGAME_NO_GAMEPAD)")
end

local function gp_first_id()
	local inst = JoyInputUtil.Instance()
	if inst == nil then return -1 end
	for i = 0, JoyInputUtil.MaxJoysticks - 1 do
		if inst:IsConnected(i) then return i end
	end
	return -1
end

local function gp_axis(id, axis)
	return JoyInputUtil.Instance():GetAxis(id, axis)
end

-- Stick + dpad to a [-1, 1] direction with a deadzone. Deadzone is
-- applied as a flat threshold on the SFML [-100, 100] range so resting
-- sticks read as zero, then the active range is remapped to [-1, 1].
local DEADZONE = 20.0
local function stick_dir(value)
	local v = value
	if v >  DEADZONE then v = (v - DEADZONE) / (100.0 - DEADZONE)
	elseif v < -DEADZONE then v = (v + DEADZONE) / (100.0 - DEADZONE)
	else v = 0.0 end
	if v >  1.0 then v =  1.0 end
	if v < -1.0 then v = -1.0 end
	return v
end

-- B (and Back / Select) close the current UI: options overlay, pause
-- screen, or no-op on the main menu.
local function ui_back_pressed(id)
	local inst = JoyInputUtil.Instance()
	return inst:WasButtonPressed(id, GamepadButton.B) or inst:WasButtonPressed(id, GamepadButton.BACK)
end

local function ui_click_pressed(id)
	return JoyInputUtil.Instance():WasButtonPressed(id, GamepadButton.A)
end

local function gp_start_pressed(id)
	return JoyInputUtil.Instance():WasButtonPressed(id, GamepadButton.START)
end

local function ui_active_doc()
	if options_open then return DOC_OPTIONS end
	if state == "menu" then return DOC_MENU end
	if state == "pause" then return DOC_PAUSE end
	return nil
end

-- RmlUi 6.3 has no built-in spatial-nav handler -- arrow keys
-- dispatch a generic keydown event that does nothing on its own, and
-- nav-up / nav-down CSS properties don't move focus either. The nav
-- chain is declared explicitly here as a 2-D grid per document: each
-- row is the left-to-right order of focusable elements on that row;
-- vertical nav jumps rows and snaps to whichever column has the
-- closest offset_left to the current element (so a stick RIGHT from
-- the start of row N lands at the start of row N+1, not in the
-- middle). Single-column chains are 1-wide rows.

-- RmlUi Input::KeyIdentifier constants for the keys we drive
-- directly (slider adjust). Stable across RmlUi versions.
local KI_LEFT   = 90
local KI_UP     = 91
local KI_RIGHT  = 92
local KI_DOWN   = 93

local FOCUS_GRIDS = {
	[DOC_MENU] = {
		{ "start" },
		{ "options" },
		{ "exit" },
	},
	[DOC_PAUSE] = {
		{ "continue" },
		{ "options" },
		{ "to-main" },
		{ "to-desktop" },
	},
	[DOC_OPTIONS] = {
		{ "res-1280", "res-1920", "res-2560", "res-3840" },
		{ "fps-60",   "fps-90",   "fps-144",  "fps-0" },
		{ "vsync" },
		{ "postfx" },
		{ "ssao" },
		{ "ssr" },
		{ "fxaa" },
		{ "shadow-0", "shadow-1", "shadow-2", "shadow-3" },
		{ "hdr" },
		{ "sens" },
		{ "hide_cursor" },
		{ "back" },
	},
}

local function grid_locate(grid, id)
	for r, row in ipairs(grid) do
		for c, eid in ipairs(row) do
			if eid == id then return r, c end
		end
	end
	return nil, nil
end

local function grid_neighbor(doc, grid, current, dir_x, dir_y)
	if current == nil then return nil end
	local cr, cc = grid_locate(grid, current.id)
	if cr == nil then return nil end

	if dir_y ~= 0 then
		local target_row = cr + dir_y
		if target_row < 1 or target_row > #grid then return nil end
		local cur_left = current.offset_left or 0
		local best, best_diff = nil, math.huge
		for _, eid in ipairs(grid[target_row]) do
			local el = doc:GetElementById(eid)
			if el then
				local diff = math.abs((el.offset_left or 0) - cur_left)
				if diff < best_diff then
					best = el
					best_diff = diff
				end
			end
		end
		if best then glog(string.format(
			"grid row %d -> %d -> %s", cr, target_row, best.id)) end
		return best
	end

	if dir_x ~= 0 then
		local row = grid[cr]
		local target_col = cc + dir_x
		if target_col < 1 or target_col > #row then return nil end
		local el = doc:GetElementById(row[target_col])
		if el then glog(string.format(
			"grid col %d -> %d -> %s", cc, target_col, el.id)) end
		return el
	end

	return nil
end

-- Input mode flips between "mouse" and "gamepad" based on whichever
-- device produced the most recent activity. The OS cursor visibility
-- follows: hidden under gamepad (so the on-screen focus highlight is
-- the only indicator), visible under mouse.
local input_mode = "mouse"
local last_mouse_x = -1
local last_mouse_y = -1

local function set_mode(new_mode)
	if new_mode == input_mode then return end
	input_mode = new_mode
	local input = InputUtil.Instance()
	if input == nil then return end
	input:SetCursorVisible(new_mode == "mouse")
	LogINFO("[islandgame/joy] mode -> " .. new_mode)
end

local function poll_mouse_motion()
	local input = InputUtil.Instance()
	if input == nil then return end
	local mx, my = input:GetMousePosition()
	if mx ~= last_mouse_x or my ~= last_mouse_y then
		last_mouse_x, last_mouse_y = mx, my
		set_mode("mouse")
	end
end

-- Track the currently-active UI doc so we can seed focus on the first
-- frame it becomes visible (otherwise RmlUi starts with no focus and
-- the first dpad press does nothing).
local gpu_focused_doc = nil

-- Edge-detected nav state. The stick / dpad must return to neutral
-- before the next press fires (one press = one nav step).
local last_nav_x, last_nav_y = 0, 0

-- Slider step accumulator: throttles the analog-stick continuous
-- path so a held stick walks the value smoothly instead of one tick
-- per frame.
local slider_step_accum = 0.0

-- D-pad Y inversion: SFML reports dpad-up as a positive PovY reading
-- on the tester's controller, so the d-pad Y is flipped by default.
-- The analog left stick (Axis.Y) follows standard SFML convention.
-- ISLANDGAME_REVERSE_Y=0 disables the d-pad inversion.
local reverse_y = (os.getenv("ISLANDGAME_REVERSE_Y") ~= "0")

local function gamepad_ui_update(dt)
	if no_gamepad then return end

	poll_mouse_motion()

	local id = gp_first_id()
	if id < 0 then return end

	-- Start pauses / unpauses regardless of focus state.
	if gp_start_pressed(id) then
		set_mode("gamepad")
		glog("Start -> toggle pause (state=" .. state .. ")")
		if state == "game" then to_pause()
		elseif state == "pause" then to_game() end
	end

	local doc_path = ui_active_doc()
	if doc_path == nil then
		gpu_focused_doc = nil
		return
	end

	local ctx = rmlui.contexts["main"]
	if ctx == nil then return end
	local doc = ctx.documents[doc_path]
	if doc == nil then return end

	-- Seed focus the first frame a doc becomes active so the first dpad
	-- press has somewhere to navigate from. The grid's first cell is
	-- always a sensible anchor (top-left of the menu).
	if gpu_focused_doc ~= doc_path then
		local grid = FOCUS_GRIDS[doc_path]
		if grid and grid[1] and grid[1][1] then
			local el = doc:GetElementById(grid[1][1])
			if el then el:Focus() end
			LogINFO("[islandgame/joy] focus seeded on " .. grid[1][1])
		end
		gpu_focused_doc = doc_path
	end

	local x_raw  = gp_axis(id, JoystickAxis.X)
	local y_raw  = gp_axis(id, JoystickAxis.Y)
	local px_raw = gp_axis(id, JoystickAxis.PovX)
	local py_raw = gp_axis(id, JoystickAxis.PovY)

	-- Discrete nav input: true for one frame whenever a new direction
	-- is engaged. Re-triggers only after the stick/dpad returns to
	-- center, so a held stick walks one element at a time.
	local stick_x = 0
	local stick_y = 0
	if      x_raw >  DEADZONE then stick_x =  1
	elseif  x_raw < -DEADZONE then stick_x = -1 end
	if      y_raw >  DEADZONE then stick_y =  1
	elseif      y_raw < -DEADZONE then stick_y = -1 end
	-- Keyboard arrows drive the same nav (one press = one move).
	local input = InputUtil.Instance()
	local key_x, key_y = 0, 0
	if input then
		if input:GetKeyDown(Key.Left)  then key_x = -1 end
		if input:GetKeyDown(Key.Right) then key_x =  1 end
		if input:GetKeyDown(Key.Up)    then key_y = -1 end
		if input:GetKeyDown(Key.Down)  then key_y =  1 end
	end
	local pov_x, pov_y = 0, 0
	if      px_raw >  DEADZONE then pov_x =  1
	elseif  px_raw < -DEADZONE then pov_x = -1 end
	if      py_raw >  DEADZONE then pov_y =  1
	elseif  py_raw < -DEADZONE then pov_y = -1 end
	if reverse_y then pov_y = -pov_y end
	-- Nav priority: d-pad > keyboard > stick.
	local nav_x = pov_x ~= 0 and pov_x or (key_x ~= 0 and key_x or stick_x)
	local nav_y = pov_y ~= 0 and pov_y or (key_y ~= 0 and key_y or stick_y)
	-- x_raw / y_raw are kept raw for the slider continuous-tracking path.
	local x_continuous = (x_raw > DEADZONE or x_raw < -DEADZONE)
		and pov_x == 0 and key_x == 0

	-- Range slider detect + analog-stick continuous path. These run
	-- on EVERY frame the input is held (not gated by the rising edge),
	-- so a held left stick walks the value smoothly. D-pad / keyboard
	-- discrete adjustment happens on the rising edge below.
	local cur = ctx.focus_element
	local function find_slider_input(el)
		if el == nil then return nil end
		if el:IsClassSet("range") then return el end
		local inputs = el:GetElementsByTagName("input")
		for i = 1, #inputs do
			if inputs[i]:GetAttribute("type") == "range" then
				return inputs[i]
			end
		end
		return nil
	end
	local slider_input = find_slider_input(cur)
	local is_slider = slider_input ~= nil

	if is_slider and x_continuous then
		-- The wrapper may not forward keydown to the inner input;
		-- explicitly focus the input widget before dispatching.
		if slider_input ~= cur then slider_input:Focus() end
		local mag = math.max(0.15, math.abs(x_raw) / 100.0)
		local interval = 0.05 / mag
		slider_step_accum = slider_step_accum + dt
		if slider_step_accum >= interval then
			slider_step_accum = 0.0
			local ki = (x_raw > DEADZONE) and KI_RIGHT or KI_LEFT
			ctx:ProcessKeyDown(ki, 0)
			ctx:ProcessKeyUp(ki, 0)
			glog(string.format("slider stick dir=%s mag=%.2f value=%s",
				(x_raw > DEADZONE and "+" or "-"), mag,
				tostring(slider_input:GetAttribute("value"))))
		end
	end

	local is_neutral = (nav_x == 0 and nav_y == 0)
	local last_was_neutral = (last_nav_x == 0 and last_nav_y == 0)
	if not is_neutral and last_was_neutral then
		set_mode("gamepad")
		if is_slider and nav_x ~= 0 then
			-- D-pad / keyboard: one press = one step. (Analog stick
			-- path is above, runs every frame the stick is held.)
			if slider_input ~= cur then slider_input:Focus() end
			if nav_x > 0 then
				ctx:ProcessKeyDown(KI_RIGHT, 0)
				ctx:ProcessKeyUp(KI_RIGHT, 0)
			else
				ctx:ProcessKeyDown(KI_LEFT, 0)
				ctx:ProcessKeyUp(KI_LEFT, 0)
			end
			glog(string.format("slider adjust dir=(%d,0) value=%s",
				nav_x, tostring(slider_input:GetAttribute("value"))))
		elseif is_slider then
			-- Vertical press on a focused slider: leave it to the next row.
			local grid = FOCUS_GRIDS[doc_path]
			local next_el = grid_neighbor(doc, grid, cur, 0, nav_y)
			if next_el then next_el:Focus() end
		else
			local grid = FOCUS_GRIDS[doc_path]
			local next_el = grid_neighbor(doc, grid, cur, nav_x, nav_y)
			if next_el then next_el:Focus() end
			glog(string.format("nav dir=(%d,%d) focus=%s",
				nav_x, nav_y,
				((ctx.focus_element) and ctx.focus_element.id) or "?"))
		end
	end
	last_nav_x, last_nav_y = nav_x, nav_y

	if ui_click_pressed(id) then
		set_mode("gamepad")
		local el = ctx.focus_element
		if el then
			glog("A -> activate " .. (el.id or "?"))
			-- Buttons dispatch their onclick on click; sliders handle
			-- arrow-key adjustments natively once focused. Using 'click'
			-- here matches the existing RML onclick handlers.
			el:DispatchEvent("click", { button = 0 })
		end
	end

	if ui_back_pressed(id) then
		set_mode("gamepad")
		glog("B/Back (state=" .. state ..
			" options_open=" .. tostring(options_open) .. ")")
		if options_open then Menu_CloseOptions()
		elseif state == "pause" then Menu_Continue()
		elseif state == "menu" then -- B on main menu: no-op, keep title screen
		end
	end
end

local function on_update(dt)
	local input = InputUtil.Instance()
	local esc_now = input:GetKeyDown(Key.Escape)
	local esc_edge = esc_now and not esc_was
	esc_was = esc_now

	gamepad_ui_update(dt)

	if state == "game" then
		if esc_edge then to_pause() end

		-- Gamepad input drives the player whenever a pad is connected;
		-- input_mode only governs the OS cursor for RmlUi and would
		-- otherwise leave a fresh pad idle until the user paused.
		local gp_id = gp_first_id()
		local pad_present = gp_id >= 0

		-- Right stick drives first-person look. We push the per-frame
		-- stick delta into the same mouse-delta accumulator the real
		-- mouse uses, so CharacterController::SyncFromPhysics picks it
		-- up with no other changes. SFML on most Windows drivers puts
		-- the right stick at U/V (Z/R are the triggers); the env vars
		-- override that when a non-standard pad swaps them again.
		local look_x_axis = tonumber(os.getenv("ISLANDGAME_LOOK_X")) or JoystickAxis.U
		local look_y_axis = tonumber(os.getenv("ISLANDGAME_LOOK_Y")) or JoystickAxis.V
		if pad_present then
			local rs_x = gp_axis(gp_id, look_x_axis) or 0.0
			local rs_y = gp_axis(gp_id, look_y_axis) or 0.0
			if math.abs(rs_x) > DEADZONE or math.abs(rs_y) > DEADZONE then
				local look_speed = 1200.0
				local dx_px = rs_x / 100.0 * look_speed * dt
				local dy_px = rs_y / 100.0 * look_speed * dt
				input:AddMouseDelta(math.floor(dx_px + 0.5),
				                     math.floor(dy_px + 0.5))
			end
		end

		-- Left stick drives planar move; Y is inverted (SFML reports
		-- positive Y for stick-up on the tester's controller). RB
		-- sprints while held.
		if pad_present and controller then
			local lx = stick_dir(gp_axis(gp_id, JoystickAxis.X) or 0.0)
			local ly = stick_dir(gp_axis(gp_id, JoystickAxis.Y) or 0.0)
			controller:SetMoveInput(-ly, lx)
			controller:SetRunSpeed(
				JoyInputUtil.Instance():IsButtonDown(gp_id,
					GamepadButton.RB) and 1500.0 or 1000.0)
		elseif controller then
			controller:SetMoveInput(0.0, 0.0)
			controller:SetRunSpeed(1000.0)
		end

		-- swim: deep water (can't stand) and the capsule is below the wave
		-- surface; the engine servos the feet to the float line each tick
		if controller and ocean and terrain then
			local p = player_node:GetWorldPosition()
			local wave = ocean:WaveHeightAtWorld(p.x, p.z)
			local ground = terrain:GetHeight(p.x, p.z) or -1.0e9
			local want = (wave - ground > 120.0) and (p.y < wave - 80.0)
			if want ~= swimming then
				swimming = want
				controller:SetSwimming(swimming)
			end
			if swimming then
				controller:SetSwimFloatHeight(wave - 150.0)  -- float deep: head just above water
			end
			local targetH = swimming and 162.0 or 170.0
			cam_height = cam_height + (targetH - cam_height) * math.min(1.0, dt * 6.0)
			controller:SetCameraHeight(cam_height)
		end
	elseif state == "pause" then
		if esc_edge then to_game() end
	else
		-- menu: slow drift around the island
		drift_yaw = drift_yaw + dt * 0.03
		local eye = Vector4(
			drift_center.x + math.cos(drift_yaw) * drift_radius,
			drift_height,
			drift_center.z + math.sin(drift_yaw) * drift_radius, 1.0)
		menu_cam_node:SetLocalPosition(eye)
		local dir = (drift_center - eye):Normalized()
		menu_cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(
			math.atan(-dir.x, -dir.z), math.asin(math.max(-1.0, math.min(1.0, dir.y))) + drift_pitch, 0.0))
		menu_cam_node:Recompose(false)
	end

	-- Headless tests have no GL context; nothing renders.
	if not _G.ISLANDGAME_NO_RUN then
		pl:Execute(octree)
	end

	-- HUD fps counter, refreshed 4x per second
	if state == "game" then
		fps_accum = fps_accum + dt
		fps_frames = fps_frames + 1
		if fps_accum >= 0.25 then
			local hud = ui_doc(DOC_HUD)
			local el = hud and hud:GetElementById("fps")
			if el then
				el.inner_rml = tostring(math.floor(fps_frames / fps_accum + 0.5))
			end
			fps_accum, fps_frames = 0.0, 0
		end
	end
end

-- Headless tests set ISLANDGAME_NO_RUN and drive the exported hooks instead
-- of the windowed run loop.
if _G.ISLANDGAME_NO_RUN then
	_G.ISLANDGAME = {
		on_init = on_init,
		on_update = on_update,
		to_pause = to_pause,
		state = function() return state end,
		debug_teleport = function(x, y, z) if controller then controller:Teleport(Vector4(x, y, z, 1.0)) end end,
		is_swimming = function() return swimming end,
	}
else
	-- Settings load once before Engine.run so a persisted rhi choice feeds
	-- the backend resolution chain (it resolves at device creation inside
	-- run); on_init loads them again for the gameplay-facing values.
	settings_load()
	Engine.run({ on_init = on_init, on_update = on_update },
		{ rhi = S.rhi ~= "" and S.rhi or nil })
end
