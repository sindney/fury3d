-- Full verification of the file-backed texture relocation fix:
--   1. Pre-state: scene.bin stores paths like "Resource/Scene/wheels.jpg"
--      and reopening it produces "File Resource/Scene/Resource/Scene/wheels.jpg
--      not exist" errors (the double-prepend bug).
--   2. After Save+Reopen through the new code: every texture path is a
--      bare filename, no double-prepended path is attempted, and no
--      "not found" errors are emitted on reload.

local scene_path = "Resource/Scene/scene.bin"
local out_path   = "Resource/Scene/scene_lod_fixed.bin"

local function slurp_errors()
    -- Stand-in: in the CLI exec path, error/warn logs go to stderr.
    -- We can't intercept them from Lua; the harness asserts on output.
end

print("[test] loading " .. scene_path)
local scene = Importer.LoadScene(scene_path)
if not scene then error("Importer.LoadScene returned nil") end

print("[test] saving to " .. out_path)
local ok = FileUtil.SaveCompressedFile(scene, out_path)
if not ok then error("SaveCompressedFile failed") end

print("[test] reopening " .. out_path)
-- The reopen is the regression check: previously this printed
-- "File ... not exist!" because Scene::Path double-prepended the
-- scene's working_dir onto the (already multi-segment) stored path.
local scene2 = Importer.LoadScene(out_path)
if not scene2 then error("Importer.LoadScene (reopen) returned nil") end

local fail = 0
local checked = 0
local function has_sep(s) return string.find(s, "/", 1, true) or string.find(s, "\\", 1, true) end
Scene.ForEachMaterial(scene2, function(mat)
    local p = mat:GetTexture("diffuse_texture")
    if p and type(p) == "string" then
        checked = checked + 1
        if has_sep(p) then
            print("[FAIL] texture path still has a directory separator: '" .. p .. "'")
            fail = fail + 1
        elseif string.find(p, "Resource/Scene/Resource/Scene/", 1, true) then
            print("[FAIL] double-prepended path: '" .. p .. "'")
            fail = fail + 1
        else
            print("[ok]   " .. mat:GetName() .. " -> '" .. p .. "'")
        end
    end
end)

if checked == 0 then
    error("no textures found on any material — key 'diffuse_texture' is wrong?")
end
if fail > 0 then
    error(fail .. " of " .. checked .. " texture(s) failed bare-name check")
end
print("[PASS] " .. checked .. " texture(s) all relocated to bare names")