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
local options_from = "main"    -- main | pause

local menu_cam_node = nil
local player_node = nil
local player_cam_node = nil
local player_camera = nil
local controller = nil
local spawn_pos = nil

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
	csm = true, csm_res = 2048,
	fov = 75, sens = 1.0,
	hdr = true, ocean_ssr = true,
	hide_cursor = true,
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
	elseif key == "csm" then
		if rs then rs:SetCascadedShadowMap(S.csm); rs_apply() end
	elseif key == "csm_res" then
		if rs then rs:SetCsmMapSize(S.csm_res); rs_apply() end
	elseif key == "hdr" then
		if rs then rs:SetHDR(S.hdr); rs_apply() end
	elseif key == "ocean_ssr" then
		local node = scene:GetRootNode():FindChildRecursively("Ocean")
		local ocean = node and node:GetOceanComponent()
		if ocean then ocean:SetSsrEnabled(S.ocean_ssr) end
	elseif key == "fov" then
		if player_camera then player_camera:SetFov(math.rad(S.fov)) end
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
	settings_apply("csm")
	settings_apply("csm_res")
	settings_apply("hdr")
	settings_apply("ocean_ssr")
	settings_apply("fov")
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
	for _, key in ipairs({ "vsync", "postfx", "ssao", "ssr", "fxaa", "csm", "hdr", "hide_cursor" }) do
		set_seg(doc, key, S[key], S[key] and "ON" or "OFF")
	end
	set_seg(doc, "ocean-ssr", S.ocean_ssr, S.ocean_ssr and "ON" or "OFF")
	-- csm res
	for _, n in ipairs({ 1024, 2048, 4096 }) do
		set_seg(doc, "csmres-" .. n, S.csm_res == n)
	end
	-- sliders
	local fov_el = doc:GetElementById("fov")
	if fov_el then fov_el:SetAttribute("value", tostring(S.fov)) end
	local fov_val = doc:GetElementById("fov-value")
	if fov_val then fov_val.inner_rml = tostring(math.floor(S.fov + 0.5)) end
	local sens_el = doc:GetElementById("sens")
	if sens_el then sens_el:SetAttribute("value", tostring(S.sens)) end
	local sens_val = doc:GetElementById("sens-value")
	if sens_val then sens_val.inner_rml = string.format("%.1f", S.sens) end
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
	options_sync()
	if options_from == "pause" then
		show_only(false, false, true, false)
	else
		show_only(false, false, true, false)
	end
end

function Menu_CloseOptions()
	settings_save()
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

function Opt_CsmRes(n)
	S.csm_res = n
	settings_apply("csm_res")
	options_sync()
end

-- The change event fires BEFORE the widget writes the value attribute,
-- so handlers take the event parameter, not GetAttribute.
function Opt_Fov(v)
	v = tonumber(v)
	if v then
		S.fov = v
		settings_apply("fov")
	end
	options_sync()
end

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
	local sky = skyNode and skyNode:GetComponent(SkyAtmosphere)
	if sky then
		-- The scene's volumetric clouds cover most altitudes and obscure the
		-- island when the menu camera frames it from above. Disable clouds
		-- for the menu; re-enabled in to_game() for the play view.
		sky:SetCloudsEnabled(false)
	end
	drift_center = Vector4(0.0, 1500.0, 0.0, 1.0)
	drift_radius = 18000.0
	drift_height = 4500.0
	drift_pitch = -0.35
	menu_cam_node = SceneNode.Create("MenuCamera")
	menu_cam_node:AddComponent(Transform.Create())
	menu_cam_node:AddComponent(Camera.Create())
	scene:GetRootNode():AddChild(menu_cam_node)

	-- player: 1.8 m capsule, no visible mesh, camera at eye height
	player_node = SceneNode.Create("Player")
	player_node:AddComponent(Transform.Create())
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
	player_camera:PerspectiveFov(math.rad(S.fov), 16.0 / 9.0, 1.0, 500000.0)
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

local function on_update(dt)
	local input = InputUtil.Instance()
	local esc_now = input:GetKeyDown(Key.Escape)
	local esc_edge = esc_now and not esc_was
	esc_was = esc_now

	if state == "game" then
		if esc_edge then to_pause() end

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
	Engine.run({ on_init = on_init, on_update = on_update })
end
