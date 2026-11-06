-- FeatureGame.lua - feature showcase demo shell.
--   ./fury FeatureGame.lua [hdr=0|hdr=1]   (or FEATUREGAME_HDR=0|1)
-- Loads Projects/feature/feature.bin with its saved render settings
-- (PBR pipeline + CSM + ACES/FXAA); hdr= overrides the saved HDR flag.
-- No IslandGame menus, options, or player state machine here.

local octree = nil
local scene = nil
local pl = nil

local DOC_OVERLAY = "ui/feature_overlay.rml"

-- ---------------------------------------------------------------- hdr seam

-- nil = keep the scene's saved setting; "0"/"1" = force HDR off/on.
local hdr_override = nil
do
	local env = os.getenv("FEATUREGAME_HDR")
	if env ~= nil and env ~= "" then hdr_override = env end
	local a = arg and arg[1]
	if a then
		local v = a:match("^hdr=(%w+)$")
		if v then hdr_override = v end
	end
end
if hdr_override ~= nil then
	hdr_override = (hdr_override ~= "0" and hdr_override ~= "false") and "1" or "0"
end

-- ---------------------------------------------------------------- boot

local function on_init()
	octree = OcTree.Create()

	Scene.SetActive(Scene.Create("feature_game", "Projects/feature/", octree))
	if not Scene.LoadActive("Projects/feature/feature.bin") then
		io.stderr:write("FeatureGame: failed to load feature.bin\n")
		Window.Close()
		return
	end
	scene = Scene.GetActive()

	Pipeline.SetActive(DeferredPipeline.Create("pipeline"))
	pl = Pipeline.GetActive()
	local rs = scene:GetRenderSettings()
	local pipe_path = rs and rs:GetPipelinePath() or ""
	if pipe_path == "" then
		pipe_path = "Resource/Pipeline/DeferredLightingPBR.json"
	end
	local full_pipe = FileUtil.GetAbsPath(pipe_path)
	-- The pipeline file needs a GL context; headless tests (FEATUREGAME_NO_RUN) skip it.
	if FileUtil.FileExist(full_pipe) and not _G.FEATUREGAME_NO_RUN then
		FileUtil.LoadPipelineFromFile(pl, full_pipe)
	end
	PostProcess.LoadFromDirectory("Resource/PostProcess")

	-- HDR seam: identical scene/camera/content, only the output config flips.
	if rs then
		if hdr_override then rs:SetHDR(hdr_override == "1") end
		Pipeline.ApplyRenderSettings(pl, rs)
		print(string.format("FeatureGame: hdr=%s csm=%s pipeline=%s",
			tostring(rs:IsHDR()), tostring(rs:IsCascadedShadowMap()),
			rs:GetPipelinePath()))
	end

	-- saved inspection camera
	local cam_node = scene:GetRootNode():FindChildRecursively("MainCamera")
	if cam_node then pl:SetCurrentCamera(cam_node) end

	-- overlay (fonts first: the document resolves LatoLatin via the shared stylesheet)
	rmlui:LoadFontFace("Engine/Ui/Fonts/LatoLatin-Regular.ttf")
	rmlui:LoadFontFace("Engine/Ui/Fonts/LatoLatin-Bold.ttf")
	rmlui.contexts["main"]:Update()
	GameUI.LoadDocument(DOC_OVERLAY)
	-- Show then Update so RmlUi resolves fonts and text metrics before the first frame.
	local doc = rmlui.contexts["main"].documents[DOC_OVERLAY]
	if doc then
		local badge = doc:GetElementById("hdr-badge")
		if badge then
			local on = rs and rs:IsHDR()
			badge.inner_rml = on and "HDR ON" or "HDR OFF"
			badge:SetClass("active", on and true or false)
		end
	end
	GameUI.Show(DOC_OVERLAY)
	rmlui.contexts["main"]:Update()
	rmlui.contexts["main"]:Update()
end

local fps_accum, fps_frames = 0.0, 0
local esc_was = false

local function on_update(dt)
	local input = InputUtil.Instance()
	if input then
		local esc_now = input:GetKeyDown(Key.Escape)
		if esc_now and not esc_was then Window.Close() end
		esc_was = esc_now
	end

	-- Headless tests have no GL context; nothing renders.
	if not _G.FEATUREGAME_NO_RUN then
		pl:Execute(octree)
	end

	-- overlay fps counter, refreshed 4x per second
	fps_accum = fps_accum + dt
	fps_frames = fps_frames + 1
	if fps_accum >= 0.25 then
		local doc = rmlui.contexts["main"].documents[DOC_OVERLAY]
		local el = doc and doc:GetElementById("fps")
		if el then
			el.inner_rml = tostring(math.floor(fps_frames / fps_accum + 0.5))
		end
		fps_accum, fps_frames = 0.0, 0
	end
end

-- Headless tests set FEATUREGAME_NO_RUN and drive these hooks instead of Engine.run.
if _G.FEATUREGAME_NO_RUN then
	_G.FEATUREGAME = {
		on_init = on_init,
		on_update = on_update,
		hdr = function()
			local rs = scene and scene:GetRenderSettings()
			if rs == nil then return nil end
			return rs:IsHDR()
		end,
	}
else
	Engine.run({ on_init = on_init, on_update = on_update })
end
