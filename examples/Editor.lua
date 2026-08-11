-- Editor.lua — flythrough camera + scene-editor glue.
-- Loaded by the `fury` executable. Provides:
--   * WASD/arrow camera (carried over from Demo.lua)
--   * Scene-IO callbacks (New / Open / Import / Save As) registered with
--     the C++ editor via Editor.SetSceneIO
--   * Camera-tuning controls registered with Editor.SetCameraSettings
--   * Optional Camera top-level menu (Settings shortcut)
--
-- Scene-graph mutations (add child / delete / duplicate / rename) are
-- applied by the C++ Scene Inspector's right-click / hover / F2 / drag-drop
-- affordances. Lua scripts that need to mutate the hierarchy from code
-- can call SceneNode:AddChild / RemoveChild / RemoveFromParent /
-- Clone / CloneTree / SetName / GetName / AddComponent / RemoveComponent
-- directly.
--
-- Menu ownership:
--   The C++ editor (engine/Fury/Editor) owns File and Window menus.
--   This script provides project-specific behavior via Editor.* callbacks.
--   Gui.SetMenuBarCallback emits the per-project Camera menu so it renders
--   between Window and any trailing engine menus.
--
-- The camera node lives OUTSIDE the scene's root tree, so File -> New
-- (which calls scene:Clear()) doesn't drop the camera. The camera is its
-- own root SceneNode that the pipeline references directly via
-- Pipeline.SetCurrentCamera.
--
-- Startup scene:
--   * No argv: load Projects/tank/scene.bin (engine's bundled tank scene).
--   * argv:    `./fury Editor.lua <name>` opens <name> at startup.
--              The path is tried literally first, then with Projects/ and
--              Resource/Scene/ prefixes, then any extension Importer.LoadScene supports.
--              On failure: fall back to scene.bin so the editor stays interactive.

local octree           = nil
local cam_node         = nil
local cam_pos          = nil           -- Vector4, set in on_init

-- Which pipeline JSON we last loaded into Pipeline.GetActive().
-- Compared against renderSettings.pipelinePath on File → Open so
-- we only reload when the saved scene references a different
-- pipeline. Set in on_init after the initial LoadPipelineFromFile.
local g_LoadedPipelinePath = ""
local yaw              = 0.0
local pitch            = -math.rad(30.0)
local move_speed       = 500.0         -- cm/s (engine unit = 1 cm)
local mouse_sensitivity = 0.004

-- Mouse-drag state.
local dragging  = false
local last_mx   = 0
local last_my   = 0

-- ---------------------------------------------------------------------------
-- Status helper — pushes a one-line note to the editor's Console window.

local function set_status(msg)
    Editor.Log("info", msg)
end

-- Forward-declared helpers (their bodies appear later in the file but are
-- referenced from earlier local functions). Lua needs the name in scope at
-- closure-capture time.
local ensure_default_sun
local frame_scene

-- Replace the active scene's content with `new_scene`. Camera + pipeline survive.
local function replace_active_scene(new_scene)
    local active = Scene.GetActive()
    -- Clear the C++ selection before active:Clear() orphans the node.
    Editor.SetSelectedSceneNode(nil)
    active:Clear()
    Importer.MergeInto(active, new_scene)
    -- Inherit the imported scene's working_dir so file-backed textures resolve.
    active:SetWorkingDir(new_scene:GetWorkingDir())

    -- VERIFY_SHADOWS hook: with FURY_SHADOW_DEBUG=1, force
    -- cast_shadows=true on every light of the opened scene. Mutates the
    -- in-memory scene (a later save persists it) — debug runs only.
    if os.getenv("FURY_SHADOW_DEBUG") then
        local function walk(node, fn)
            fn(node)
            for i = 0, node:GetChildCount() - 1 do
                walk(node:GetChildAt(i), fn)
            end
        end
        walk(active:GetRootNode(), function(node)
            local l = node:GetLight()
            if l then l:SetCastShadows(true) end
        end)
        print("verify_shadows: cast_shadows=true on every light")
    end
    -- MergeInto doesn't transfer renderSettings; copy them so
    -- HDR/CSM/chain survive File → Open. CopyChainFrom preserves
    -- per-entry uniform overrides (the old AddEffect loop dropped
    -- them).
    local src_rs = new_scene:GetRenderSettings()
    local dst_rs = active:GetRenderSettings()
    if src_rs and dst_rs then
        dst_rs:SetPipelinePath(src_rs:GetPipelinePath())
        dst_rs:SetHDR(src_rs:IsHDR())
        dst_rs:SetCascadedShadowMap(src_rs:IsCascadedShadowMap())
        dst_rs:CopyChainFrom(src_rs)
    end
    ensure_default_sun(active)
    -- Reload the pipeline when the opened scene references a
    -- different JSON (g_LoadedPipelinePath tracks what's loaded).
    if src_rs and dst_rs then
        local saved_path = src_rs:GetPipelinePath()
        if saved_path and saved_path ~= "" and saved_path ~= g_LoadedPipelinePath then
            local full = FileUtil.GetAbsPath(saved_path)
            if FileUtil.FileExist(full) then
                local active_pl = Pipeline.GetActive()
                if active_pl then
                    FileUtil.LoadPipelineFromFile(active_pl, full)
                    g_LoadedPipelinePath = saved_path
                    print("Editor: reloaded pipeline " .. saved_path)
                end
            else
                print("Editor: saved pipeline not found: " .. full)
            end
        end
        -- Apply the scene's HDR + chain to the (possibly reloaded) pipeline.
        local active_pl = Pipeline.GetActive()
        if active_pl then
            Pipeline.ApplyRenderSettings(active_pl, dst_rs)
        end
    end
    -- active:Clear() destroyed the old editor camera; create a
    -- fresh one and point the pipeline at it.
    -- First purge stale EditorCamera nodes loaded from the file (pre-
    -- editorOnly scenes carry one leaked copy per past save). The fresh
    -- camera below is flagged editor-only so future saves skip it.
    local root = active:GetRootNode()
    local stale = {}
    for i = 0, root:GetChildCount() - 1 do
        local child = root:GetChildAt(i)
        if child:GetName() == "EditorCamera" then
            stale[#stale + 1] = child
        end
    end
    for _, node in ipairs(stale) do
        root:RemoveChild(node)
    end
    local editor_cam = SceneNode.Create("EditorCamera")
    editor_cam:SetEditorOnly(true)
    editor_cam:SetLocalPosition(Vector4(0.0, 170.0, 400.0, 1.0))
    editor_cam:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
    editor_cam:Recompose(false)
    editor_cam:AddComponent(Transform.Create())
    local editor_camera = Camera.Create()
    editor_camera:PerspectiveFov(0.7854, 1.778, 1, 5000)
    editor_camera:SetShadowFar(2000)
    editor_camera:SetShadowBounds(Vector4(-500), Vector4(500))
    editor_cam:AddComponent(editor_camera)
    editor_cam:Recompose(true)
    active:GetRootNode():AddChild(editor_cam)
    cam_node = editor_cam
    if Pipeline.GetActive() then
        Pipeline.GetActive():SetCurrentCamera(editor_cam)
    end
end

-- True for formats the engine can save back to (.json / .bin). Any other
-- extension routes File → Save through the Save As dialog so imported
-- assets aren't silently overwritten.
local function is_native_format(filename)
    local ext = filename:lower():match("%.[^.]+$")
    return ext == ".json" or ext == ".bin"
end

-- All formats the engine's Importer.LoadScene can read. Used as the
-- default filter for both Editor.OpenDialog (Open) and the import
-- flow. nfd's filter spec is a comma-separated list of extensions
-- without leading dots.
local SCENE_FILE_FILTER = "json,bin,gltf,glb,fbx"

-- Unit-scale detection runs only for mesh-exchange formats; native
-- .json/.bin scenes are already engine units (1 unit = 1 cm).
local function is_gltf_family(path)
    local ext = path:lower():match("%.[^.]+$")
    return ext == ".gltf" or ext == ".glb" or ext == ".fbx"
end

-- Normal generation mode for imports, from the Settings → Import combo.
local function current_normal_mode()
    return Editor.GetImportFlag("normals_smooth", true) and "smooth" or "flat"
end

-- Returns scale, dim when `scene` looks unit-mismatched: largest
-- bounds dimension under 100 units (1 m). scale is the smallest
-- power of 100 bringing the bounds to >= 1 m. Returns nil when the
-- check is disabled or doesn't apply (healthy size / no meshes).
local function suggest_unit_scale(scene)
    if not Editor.GetImportFlag("auto_scale_detect", true) then return nil end
    local bmin, bmax = scene:ComputeWorldAABB()
    if not bmin then return nil end
    local size = bmax - bmin
    local dim = math.max(size.x, size.y, size.z)
    if dim >= 100.0 then return nil end
    local scale = 100.0
    while dim * scale < 100.0 do scale = scale * 100.0 end
    return scale, dim
end

-- Multiply the local scale of every top-level node of an imported
-- scene (MergeInto moves root children, so the root's own transform
-- never reaches the active scene — scale the children instead).
local function scale_import_roots(scene, scale)
    local root = scene:GetRootNode()
    if not root then return end
    for i = 0, root:GetChildCount() - 1 do
        local child = root:GetChildAt(i)
        child:SetLocalScale(child:GetLocalScale() * scale)
    end
end

-- Queue the Yes/No auto-scale dialog; `commit(scale_or_nil)` runs on
-- either answer (Yes → scaled, No/Esc → unscaled).
local function prompt_unit_scale(path, dim, scale, commit)
    local name = path:match("[^/\\]+$") or path
    Editor.RequestConfirmDialog(
        "Small Import Detected",
        string.format(
            "\"%s\" is only %.2f units across (~%.1f cm) - likely authored in a smaller unit.\n\nScale imported root node(s) by %dx?",
            name, dim, dim, scale),
        function(yes)
            if yes then
                commit(scale)
                set_status(string.format("auto-scaled %s by %dx", name, scale))
            else
                commit(nil)
            end
        end)
end

-- `path` is an absolute path on disk (returned by Editor.OpenDialog
-- single-select). File → Open... / Ctrl+O route through the native
-- dialog and then call this directly with the absolute path.
-- Returns true when the file loaded (an unanswered auto-scale
-- dialog still counts — the commit runs when it's answered).
local function open_scene_at_path(path)
    local imported = Importer.LoadScene(path, current_normal_mode())
    if not imported then
        set_status("failed to open " .. path)
        return false
    end
    local function commit(scale)
        if scale then scale_import_roots(imported, scale) end
        replace_active_scene(imported)
        -- The dialog's chosen path is the source of truth: native
        -- formats (.json / .bin) we can save back to in-place, others
        -- we cannot.
        local basename = path:match("[^/\\]+$") or path
        Editor.SetCurrentScene(path, is_native_format(basename))
        set_status("opened " .. path)
        frame_scene(Scene.GetActive())
    end
    if is_gltf_family(path) then
        local scale, dim = suggest_unit_scale(imported)
        if scale then
            prompt_unit_scale(path, dim, scale, commit)
            return true -- commit runs when the dialog is answered
        end
    end
    commit(nil)
    return true
end

local function import_scene_at_path(full)
    local imported = Importer.LoadScene(full, current_normal_mode())
    if not imported then
        set_status("failed to import " .. full)
        return
    end
    local function commit(scale)
        if scale then scale_import_roots(imported, scale) end
        local n = Importer.MergeInto(Scene.GetActive(), imported)
        ensure_default_sun(Scene.GetActive())
        set_status("imported " .. n .. " node(s) from " .. full)
    end
    if is_gltf_family(full) then
        local scale, dim = suggest_unit_scale(imported)
        if scale then
            prompt_unit_scale(full, dim, scale, commit)
            return -- commit runs when the dialog is answered
        end
    end
    commit(nil)
end

-- Shared core for File → Save (in-place) and File → Save As (named target).
-- Both pass an absolute path under Resource/Scene/ and expect a .json or .bin
-- extension. Returns true on success.
local function write_scene_to_path(full)
    local ok = FileUtil.SaveByExtension(Scene.GetActive(), full)
    if ok then
        Editor.SetCurrentScene(full, true)
        set_status("wrote " .. full)
    else
        set_status("save failed for " .. full)
    end
    return ok
end

local function save_active_scene(_unused_filename)
    -- File → Save As entry point. Driven by a native Editor.SaveDialog
    -- (the previous ImGui "Save Scene As" modal was retired in tandem
    -- with the nfd integration). The `_unused_filename` argument comes
    -- from the legacy SceneIO.on_save_as(string) signature and is
    -- ignored — the destination path comes from the dialog.
    --
    -- On cancel (Editor.SaveDialog returns nil) we do nothing and leave
    -- the dirty flag unchanged, per the spec.
    local current = Editor.GetCurrentScenePath() or ""
    local default_name = "untitled.json"
    if current ~= "" then
        local basename = current:match("[^/\\]+$") or current
        local stem = basename:match("^(.*)%.[^.]+$") or basename
        local lower = basename:lower()
        if lower:sub(-5) == ".json" or lower:sub(-4) == ".bin" then
            default_name = basename
        else
            default_name = stem .. ".json"
        end
    end

    local path = Editor.SaveDialog({
        filter = "json,bin",
        default_path = FileUtil.GetAbsPath("Projects/"),
        default_name = default_name,
    })
    if not path then return end
    if write_scene_to_path(path) then
        Editor.ClearSceneDirty()
    end
end

local function save_scene_in_place(full)
    -- Save (no rename): Cmd+S already verified the path is native, so the
    -- extension dispatch in write_scene_to_path is a tautological double-check.
    if write_scene_to_path(full) then
        Editor.ClearSceneDirty()
    end
end


-- ---------------------------------------------------------------------------
-- Default-sun fallback
--
-- The deferred-Lambert pipeline has no built-in ambient term — anything not
-- inside a light's radius renders pixel-black. To keep imported scenes
-- visible by default, attach a faint white directional sun when the scene
-- has none. Toggleable from Settings → Import → Auto-Add Default Sun.

local function tree_has_directional_light(node)
    if not node then return false end
    local l = node:GetLight()
    if l and l:GetType() == LightType.DIRECTIONAL then return true end
    for i = 0, node:GetChildCount() - 1 do
        if tree_has_directional_light(node:GetChildAt(i)) then return true end
    end
    return false
end

local function ensure_default_sun_impl(scene)
    if not scene then return end
    if not Editor.GetImportFlag("auto_default_sun", true) then return end
    if tree_has_directional_light(scene:GetRootNode()) then return end
    local sun = Light.Create()
    sun:SetType(LightType.DIRECTIONAL)
    sun:SetColor(Color(1.0, 1.0, 1.0, 1.0))
    sun:SetIntensity(0.3)
    sun:SetCastShadows(false)
    sun:CalculateAABB()
    local sun_node = SceneNode.Create("DefaultSun")
    sun_node:SetLocalRoattion(MathUtil.EulerRadToQuat(0.0, -math.rad(45.0), 0.0))
    sun_node:Recompose(false)
    sun_node:AddComponent(Transform.Create())
    sun_node:AddComponent(sun)
    sun_node:Recompose(true)
    scene:GetRootNode():AddChild(sun_node)
    scene:GetSceneManager():AddSceneNodeRecursively(sun_node)
    set_status("imported scene had no directional light — added DefaultSun")
end
ensure_default_sun = ensure_default_sun_impl

-- ---------------------------------------------------------------------------
-- Startup scene resolution

local function load_default_scene()
    local path = FileUtil.GetAbsPath("Projects/tank/scene.bin")
    -- Route through open_scene_at_path so the loading scene's
    -- working_dir is set correctly for texture path resolution.
    if open_scene_at_path(path) then return end
    FileUtil.LoadSceneFromCompressedFile(Scene.GetActive(), path)
    Editor.SetCurrentScene(path, true)
end

local function file_exists(path)
    local f = io.open(path, "rb")
    if f then f:close(); return true end
    return false
end

local function resolve_startup_scene(name)
    if not name or name == "" then return nil end
    local literal = FileUtil.GetAbsPath(name)
    if file_exists(literal) then return literal end
    local proj = FileUtil.GetAbsPath("Projects/" .. name)
    if file_exists(proj) then return proj end
    local prefixed = FileUtil.GetAbsPath("Resource/Scene/" .. name)
    if file_exists(prefixed) then return prefixed end
    return nil
end

-- ---------------------------------------------------------------------------

-- Frame the camera on a SceneNode. Invoked by the C++ Scene

-- After loading a scene (startup or File -> Open), frame the camera on the whole-scene AABB. Toggled by the auto_focus editor setting (default ON; persisted via the editor settings handler — same path as auto_scale_detect).
-- Same yaw/pitch math as frame_selection so the camera convention stays consistent.
frame_scene = function(scene)
    if not Editor.GetImportFlag("auto_focus", true) then return end
    if not scene then return end
    local bmin, bmax = scene:ComputeWorldAABB()
    if not bmin or not bmax then return end
    local center = (bmin + bmax) * 0.5
    local size = bmax - bmin
    local radius = math.max(size.x, size.y, size.z) * 0.5

    local distance = (radius > 1e-4)
        and (radius / math.tan(0.7854 * 0.5) * 1.1)
        or 10.0
    if distance < 1.0 then distance = 1.0 end

    -- Big scenes (tank / outdoor auto-scaled to ~1.4k units) need a
    -- far plane beyond the editor's default 5000 to keep the
    -- terrain visible from the framed pose. Without this the
    -- background mesh is sliced off when auto-scale grows the
    -- scene above 1250 radius.
    local cam = cam_node and cam_node:GetCamera()
    if cam and distance * 4.0 > 5000.0 then
        cam:PerspectiveFov(0.7854, 1.778, math.max(1.0, distance * 0.001), distance * 4.0)
    end

    local dir_len = math.sqrt(1.0 + 0.36 + 1.0)  -- normalized (1, 0.6, 1)
    local dirx = 1.0 / dir_len
    local diry = 0.6 / dir_len
    local dirz = 1.0 / dir_len

    local eye = Vector4(
        center.x + dirx * distance,
        center.y + diry * distance,
        center.z + dirz * distance,
        1.0)

    local dx = eye.x - center.x
    local dy = eye.y - center.y
    local dz = eye.z - center.z
    local horiz = math.sqrt(dx * dx + dz * dz)

    if horiz < 1e-6 then yaw = 0.0 else yaw = math.atan(dx, dz) end
    pitch = math.atan(-dy, horiz)
    local limit = math.rad(89.0)
    if pitch >  limit then pitch =  limit end
    if pitch < -limit then pitch = -limit end

    cam_pos = eye

    -- Apply immediately so the camera is at its framed pose before
    -- the next mouse event lands. Without this the camera sits at
    -- its pre-frame yaw/pitch for one tick, and the first drag's
    -- delta gets applied on top of the old (out-of-sync) state.
    if cam_node then
        cam_node:SetLocalPosition(cam_pos)
        cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
        cam_node:Recompose(false)
    end
end

-- Inspector's leaf-double-click via Editor::FrameSelection.
local function frame_selection(node)
    if not node or not node:GetParent() then return end

    local center = node:GetWorldPosition()
    local radius = 0.0

    local mr = node:GetMeshRender()
    if mr then
        local aabb = node:GetWorldAABB()
        if aabb and aabb:Valid() and not aabb:GetInfinite() then
            center = aabb:GetCenter()
            local mn = aabb:GetMin()
            local mx = aabb:GetMax()
            radius = (mx - mn):Length() * 0.5
        end
    end

    local distance = (radius > 1e-4)
        and (radius / math.tan(0.7854 * 0.5) * 1.25)  -- fit AABB into 45° FOV
        or 10.0  -- non-renderable / zero-size leaf

    if distance < 1.0    then distance = 1.0    end
    if distance > 5000.0 then distance = 5000.0 end

    if center.x ~= center.x or center.y ~= center.y or center.z ~= center.z then return end

    local dir_len = math.sqrt(1.0 + 0.36 + 1.0)  -- normalized (1, 0.6, 1)
    local dirx = 1.0 / dir_len
    local diry = 0.6 / dir_len
    local dirz = 1.0 / dir_len

    local eye = Vector4(
        center.x + dirx * distance,
        center.y + diry * distance,
        center.z + dirz * distance,
        1.0)

    local dx = eye.x - center.x
    local dy = eye.y - center.y
    local dz = eye.z - center.z
    local horiz = math.sqrt(dx * dx + dz * dz)

    -- Lua 5.4 removed math.atan2; math.atan(y, x) is the equivalent.
    if horiz < 1e-6 then yaw = 0.0 else yaw = math.atan(dx, dz) end
    pitch = math.atan(-dy, horiz)
    local limit = math.rad(89.0)
    if pitch >  limit then pitch =  limit end
    if pitch < -limit then pitch = -limit end

    cam_pos = eye
end

-- ---------------------------------------------------------------------------

local function on_init()
    octree = OcTree.Create()

    Scene.SetActive(Scene.Create("main", FileUtil.GetAbsPath(), octree))

    -- Camera + default LDR pipeline must exist BEFORE the startup
    -- scene's renderSettings decide whether to reload the pipeline.
    --
    -- Engine unit = 1 cm (see docs/ARCHITECTURE.md). Camera near/far
    -- and shadow frustum are in cm.
    local camera = Camera.Create()
    camera:PerspectiveFov(0.7854, 1.778, 1, 5000)
    camera:SetShadowFar(2000)
    camera:SetShadowBounds(Vector4(-500), Vector4(500))

    -- Camera start position in cm (back + above origin).
    cam_pos  = Vector4(0.0, 170.0, 400.0, 1.0)

    cam_node = SceneNode.Create("camNode")
    cam_node:SetEditorOnly(true) -- never serialize the editor camera (see replace_active_scene)
    cam_node:SetLocalPosition(cam_pos)
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
    cam_node:Recompose(false)
    cam_node:AddComponent(Transform.Create())
    cam_node:AddComponent(camera)
    cam_node:Recompose(true)

    Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
    Pipeline.GetActive():SetCurrentCamera(cam_node)
    FileUtil.LoadPipelineFromFile(
        Pipeline.GetActive(),
        FileUtil.GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"))
    g_LoadedPipelinePath = "Resource/Pipeline/DefferedLightingLambert.json"

    -- Load postprocess effects from the project's registry BEFORE
    -- opening the startup scene. ApplyRenderSettings (called from
    -- replace_active_scene) resolves effect names against this
    -- registry; loading effects AFTER the open means the chain
    -- is built with empty resolutions and every entry is dropped
    -- with a warning.
    local pp_loaded = PostProcess.LoadFromDirectory("Resource/PostProcess")
    if pp_loaded and pp_loaded > 0 then
        print("Editor: loaded " .. pp_loaded .. " postprocess effect(s)")
    end

    local startup = arg and arg[1]
    if startup and startup ~= "" then
        local resolved = resolve_startup_scene(startup)
        if resolved then
            -- Route through the shared open path so startup imports
            -- get the same unit-scale prompt as File → Open.
            if not open_scene_at_path(resolved) then
                print("Editor.lua: Importer.LoadScene rejected '" .. startup .. "'; falling back to scene.bin")
                set_status("failed to open " .. startup .. " — using default scene")
                load_default_scene()
            end
        else
            print("Editor.lua: startup scene '" .. startup .. "' not found (tried literal and Resource/Scene/ prefix); falling back to scene.bin")
            set_status("not found: " .. startup .. " — using default scene")
            load_default_scene()
        end
    else
        load_default_scene()
    end

    -- arg[2] (automation): comma-separated "type:name" asset editors to
    -- open after the startup scene settles — e.g.
    --   ./furye Editor.lua Projects/outdoor/outdoor_water.bin \
    --       particle:FireEmber,mesh:Feu --screenshot /tmp/x.png
    -- Lets verification scripts screenshot the per-asset editors
    -- headlessly without driving the content browser by hand.
    local editors = arg and arg[2]
    if editors and editors ~= "" then
        for spec in string.gmatch(editors, "[^,]+") do
            local kind, name = spec:match("^(%w+):(.+)$")
            if kind == "particle" and Editor.OpenParticleEditor then
                Editor.OpenParticleEditor(name)
            elseif kind == "mesh" and Editor.OpenMeshEditor then
                Editor.OpenMeshEditor(name)
            elseif kind == "terrain" and Editor.OpenTerrainEditor then
                Editor.OpenTerrainEditor(name)
            elseif kind == "sky" and Editor.OpenSkyEditor then
                Editor.OpenSkyEditor(name)
            end
        end
    end

    -- FURY_SELECT=name (automation): select a node after the startup scene
    -- settles, so screenshot runs can show its inspector sections.
    local sel = os.getenv("FURY_SELECT")
    if sel and sel ~= "" then
        local n = Scene.GetActive():GetRootNode():FindChildRecursively(sel)
        if n then Editor.SetSelectedSceneNode(n) end
    end

    -- Reflect the currently-loaded pipeline back into renderSettings.
    -- The startup scene's renderSettings may already point at a
    -- different pipeline (handled above); for scenes whose saved
    -- renderSettings were cleared (legacy scenes, File → New) we
    -- keep the default LDR pipeline. This block is purely
    -- informational — it doesn't reload the pipeline.
    local settings = Scene.GetActive():GetRenderSettings()
    if settings and settings:GetPipelinePath() == "" then
        settings:SetPipelinePath(g_LoadedPipelinePath)
    end

    -- Wire the editor's File menu callbacks. The C++ side dispatches
    -- File → Open... (Ctrl+O), File → Import... (Ctrl+Shift+I), and
    -- File → Save As... (Ctrl+Shift+S) by invoking these callbacks
    -- — the native dialogs run inside the callbacks, the C++ side
    -- only routes the menu / shortcut events.
    Editor.SetSceneIO({
        on_new     = function()
            -- Clear the C++ selection before active:Clear() orphans the
            -- node (same ordering as replace_active_scene — File → Open
            -- goes through that path, so this matches it).
            Editor.SetSelectedSceneNode(nil)
            Scene.GetActive():Clear()
            Editor.ClearCurrentScene()
            set_status("scene cleared")
        end,
        on_open    = function()
            -- File → Open... / Ctrl+O: native single-select dialog,
            -- then replace the active scene. Accepts any engine
            -- loadable format (.json / .bin / .gltf / .glb / .fbx).
            local path = Editor.OpenDialog({
                filter = SCENE_FILE_FILTER,
                default_path = FileUtil.GetAbsPath("Projects/"),
            })
            if not path then return end -- user cancelled
            open_scene_at_path(path)
        end,
        on_import  = function()
            -- File → Import... / Ctrl+Shift+I: native multi-select
            -- dialog, then merge each chosen scene into the active
            -- one. Accepts any engine loadable format.
            local paths = Editor.OpenDialog({
                filter = SCENE_FILE_FILTER,
                default_path = FileUtil.GetAbsPath("Projects/"),
                multi = true,
            })
            if not paths then return end -- user cancelled
            if type(paths) == "string" then paths = { paths } end
            for _, path in ipairs(paths) do
                import_scene_at_path(path)
            end
        end,
        on_save    = save_scene_in_place,
        on_save_as = save_active_scene,
        scene_dir  = function() return FileUtil.GetAbsPath("Projects/") end,
    })

    -- Import flags: persisted to imgui.ini by the FuryEditor settings
    -- handler (ImportFlag=<name>=<0|1>). On first launch the g_ImportFlags
    -- map is empty, so GetImportFlag returns the C++ default (true for all
    -- four). On subsequent launches the handler restores persisted values
    -- before this runs — don't overwrite them here.

    -- Wire the Scene Inspector's leaf-double-click → camera-frame path.
    -- The C++ inspector calls Editor::FrameSelection(node), which invokes
    -- this handler; the handler writes the Lua-owned cam_pos/yaw/pitch
    -- upvalues so the next on_update pushes them to cam_node. See
    -- frame_selection above for the framing math.
    Editor.SetFrameSelectionHandler(frame_selection)

    -- Camera tuning lives inside Settings → Camera now.
    Editor.SetCameraSettings({
        controls = {
            { label = "Move Speed",
              kind  = "slider",
              min   = 0.5, max = 50.0,
              get   = function() return move_speed end,
              set   = function(v) move_speed = v end },
            { label = "Mouse Sensitivity",
              kind  = "slider",
              min   = 0.0005, max = 0.02,
              get   = function() return mouse_sensitivity end,
              set   = function(v) mouse_sensitivity = v end },
        }
    })


    


    

    

    -- Console: evaluate ad-hoc Lua snippets. Errors land in the Console as
    -- error-level entries instead of crashing the engine.

Editor.SetCommandHandler(function(line)
        local fn, err = load(line, "console")
        if not fn then
            Editor.Log("error", err)
            return
        end
        local ok, result = pcall(fn)
        if not ok then
            Editor.Log("error", tostring(result))
        elseif result ~= nil then
            Editor.Log("info", tostring(result))
        end
    end)

    -- Headless verify hook: FURY_CAM="px,py,pz,yawDeg,pitchDeg" pins the
    -- viewport camera (matches the Profiler → GBuffer pos/rot readout,
    -- so a reported view can be reproduced exactly for --screenshot).
    -- Runs LAST in on_init — the scene-open path's auto-focus reframes
    -- the camera, so applying earlier gets overwritten.
    do
        local cam_env = os.getenv("FURY_CAM")
        if cam_env then
            local px, py, pz, yawd, pitchd = cam_env:match(
                "^%s*([-%d.]+)%s*,%s*([-%d.]+)%s*,%s*([-%d.]+)%s*,%s*([-%d.]+)%s*,%s*([-%d.]+)%s*$")
            if px then
                cam_pos = Vector4(tonumber(px), tonumber(py), tonumber(pz), 1.0)
                yaw     = math.rad(tonumber(yawd))
                pitch   = math.rad(tonumber(pitchd))
                cam_node:SetLocalPosition(cam_pos)
                cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
                print(string.format("FURY_CAM: pos=(%.2f, %.2f, %.2f) yaw=%.2f pitch=%.2f",
                    cam_pos.x, cam_pos.y, cam_pos.z, tonumber(yawd), tonumber(pitchd)))
            else
                print("FURY_CAM: could not parse '" .. cam_env .. "' (want px,py,pz,yawDeg,pitchDeg)")
            end
        end
    end

    -- Repro hook: FURY_NODE_ROT="NodeName,x,y,z" (euler degrees,
    -- inspector convention) / FURY_NODE_POS="NodeName,x,y,z" (local
    -- position) — pins a node's local transform for headless
    -- light-aim scenarios.
    do
        local function find(node, n)
            if node:GetName() == n then return node end
            for i = 0, node:GetChildCount() - 1 do
                local f = find(node:GetChildAt(i), n)
                if f then return f end
            end
        end
        local rot_env = os.getenv("FURY_NODE_ROT")
        if rot_env then
            local name, rx, ry, rz = rot_env:match(
                "^%s*([^,]+)%s*,%s*([-%d.]+)%s*,%s*([-%d.]+)%s*,%s*([-%d.]+)%s*$")
            local target = name and find(Scene.GetActive():GetRootNode(), name)
            if target then
                target:SetLocalRoattion(MathUtil.EulerRadToQuat(
                    math.rad(tonumber(rx)), math.rad(tonumber(ry)), math.rad(tonumber(rz))))
                target:Recompose(true)
                print(string.format("FURY_NODE_ROT: %s -> (%s, %s, %s) deg", name, rx, ry, rz))
            else
                print("FURY_NODE_ROT: bad args or node not found: '" .. rot_env .. "'")
            end
        end
        local pos_env = os.getenv("FURY_NODE_POS")
        if pos_env then
            local name, px, py, pz = pos_env:match(
                "^%s*([^,]+)%s*,%s*([-%d.]+)%s*,%s*([-%d.]+)%s*,%s*([-%d.]+)%s*$")
            local target = name and find(Scene.GetActive():GetRootNode(), name)
            if target then
                target:SetLocalPosition(Vector4(tonumber(px), tonumber(py), tonumber(pz), 1.0))
                target:Recompose(true)
                print(string.format("FURY_NODE_POS: %s -> (%s, %s, %s)", name, px, py, pz))
            else
                print("FURY_NODE_POS: bad args or node not found: '" .. pos_env .. "'")
            end
        end
    end
end

-- Forward and right vectors derived from yaw/pitch in the engine's convention.
local function camera_basis()
    local cy, sy = math.cos(yaw),   math.sin(yaw)
    local cp, sp = math.cos(pitch), math.sin(pitch)
    local fwd = Vector4(-cp * sy,  sp, -cp * cy, 0.0)
    local rgt = Vector4( cy,       0.0, -sy,      0.0)
    return fwd, rgt
end

local function on_update(dt)
    local input  = InputUtil.Instance()
    local has_kb = not Gui.WantCaptureKeyboard()
    -- The Viewport window is a real ImGui window, so hovering it sets
    -- WantCaptureMouse=true. We still want the camera-drag and wheel to
    -- work over the viewport, so treat the mouse as "available" when
    -- either ImGui doesn't want it OR the cursor is inside the viewport
    -- content rect (NOT the title bar / resize borders — IsViewportHovered
    -- would fire there too and cause the camera to rotate while the user
    -- drags the undocked viewport window around).
    local has_mo = (not Gui.WantCaptureMouse()) or Editor.IsViewportContentHovered()
    local focused = input:GetWindowFocused()

    -- ── mouse-drag yaw / pitch ────────────────────────────────────────────
    -- Suppress the camera-drag while a pick is resolving (defense in depth
    -- alongside the click-vs-drag gate in the C++ editor): a true click
    -- schedules a pick whose readback completes over the next two frames,
    -- and we don't want an immediately-following drag to fight it.
    local lmb_down = focused and has_mo and input:GetMouseDown(MouseButton.Left) and not Editor.IsPickInFlight()
    if lmb_down then
        local mx, my = input:GetMousePosition()
        if dragging then
            yaw   = yaw   - (mx - last_mx) * mouse_sensitivity
            pitch = pitch - (my - last_my) * mouse_sensitivity
            local limit = math.rad(89.0)
            if pitch >  limit then pitch =  limit end
            if pitch < -limit then pitch = -limit end
        end
        last_mx, last_my = mx, my
        dragging = true
    else
        dragging = false
    end

    -- ── WASD / arrows translate ───────────────────────────────────────────
    -- Gated on content-rect hover (not just OS window focus), so WASD
    -- doesn't translate the camera while the user is interacting with a
    -- docked panel or the viewport window's chrome. Combined with the
    -- same gate on the LMB drag, this guarantees drag and WASD share a
    -- single predicate — they cannot fall out of sync with each other.
    if focused and Editor.IsViewportContentHovered() then
        local fwd, rgt = camera_basis()
        local move     = Vector4(0.0, 0.0, 0.0, 0.0)

        if input:GetKeyDown(Key.W) or input:GetKeyDown(Key.Up)    then move = move + fwd end
        if input:GetKeyDown(Key.S) or input:GetKeyDown(Key.Down)  then move = move - fwd end
        if input:GetKeyDown(Key.A) or input:GetKeyDown(Key.Left)  then move = move - rgt end
        if input:GetKeyDown(Key.D) or input:GetKeyDown(Key.Right) then move = move + rgt end

        local mlen = move:Length()
        if mlen > 0.0 then
            local boost = input:GetKeyDown(Key.LShift) and 5.0 or 1.0
            cam_pos = cam_pos + move * (move_speed * boost * dt / mlen)
        end
    end

    -- ── mouse-wheel adjusts the base move speed (clamped) ─────────────────
    if has_mo then
        local wheel = input:GetMouseWheel()
        if wheel ~= 0.0 then
            move_speed = move_speed + wheel
            if move_speed < 0.5  then move_speed = 0.5  end
            if move_speed > 50.0 then move_speed = 50.0 end
        end
    end

    -- ── push state back into the SceneNode ────────────────────────────────
    cam_node:SetLocalPosition(cam_pos)
    cam_node:SetLocalRoattion(MathUtil.EulerRadToQuat(yaw, pitch, 0.0))
    cam_node:Recompose(false)

    -- ── ImGui overlays ────────────────────────────────────────────────────
    -- With WITH_EDITOR=ON, Editor::Tick has already emitted the menu bar,
    -- dockspace, and built-in windows during the engine's main loop (it
    -- runs between Gui::NewFrame and the user callback). With WITH_EDITOR=
    -- OFF, Gui.ShowDefault still runs the menu-bar callback so the Camera
    -- menu shows up; the editor windows simply don't exist.
    Gui.ShowDefault(dt)

    Pipeline.GetActive():Execute(octree)

    -- Gui::Render is invoked from the engine's main loop (Engine::Run),
    -- after Editor::TickPostRender, so ImGui composites on top of the 3D
    -- scene (including the Viewport window's render-target image). The
    -- pipeline no longer calls Gui::Render itself.
end

local function on_shutdown()
    octree   = nil
    cam_node = nil
    cam_pos  = nil
end

Engine.run({
    on_init     = on_init,
    on_update   = on_update,
    on_shutdown = on_shutdown,
})
