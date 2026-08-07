-- tests/lua/editor_only_nodes.lua - verifies the editorOnly node flag:
-- flagged subtrees are excluded from serialization, and the Editor.lua
-- purge pattern cleans previously leaked EditorCamera nodes from legacy
-- scenes (outdoor_water.bin carries 4). Spec: scene-round-trip delta.
--
-- Usage:
--   ./fury exec Projects/outdoor/outdoor_water.bin tests/lua/editor_only_nodes.lua

local function fail(msg)
    io.stderr:write("editor_only FAIL: " .. msg .. "\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then fail("no active scene") end
local root = scene:GetRootNode()

local function count_named(parent, name)
    local n = 0
    for i = 0, parent:GetChildCount() - 1 do
        if parent:GetChildAt(i):GetName() == name then n = n + 1 end
    end
    return n
end

-- outdoor_water.bin has 4 leaked EditorCamera nodes (pre-editorOnly saves).
local before = count_named(root, "EditorCamera")
print("editor_only: stale EditorCamera nodes in file: " .. before)
if before < 1 then fail("expected at least 1 leaked EditorCamera in outdoor_water.bin") end

-- Mirror Editor.lua's replace_active_scene: purge stale, add flagged fresh.
local stale = {}
for i = 0, root:GetChildCount() - 1 do
    local c = root:GetChildAt(i)
    if c:GetName() == "EditorCamera" then stale[#stale + 1] = c end
end
for _, node in ipairs(stale) do root:RemoveChild(node) end

local cam = SceneNode.Create("EditorCamera")
cam:SetEditorOnly(true)
root:AddChild(cam)

-- A flagged node deep in the tree must skip its whole subtree.
local helper = SceneNode.Create("EditorHelper")
helper:SetEditorOnly(true)
local marker = SceneNode.Create("EditorHelperChild")
helper:AddChild(marker)
root:AddChild(helper)

-- Unflagged control node must survive.
local keep = SceneNode.Create("KeepMe")
root:AddChild(keep)

local path = "/tmp/editor_only_test.bin"
if not FileUtil.SaveCompressedFile(scene, path) then fail("save failed") end
if not Scene.LoadActive(path) then fail("reload failed") end

local root2 = Scene.GetActive():GetRootNode()
local after = count_named(root2, "EditorCamera")
if after ~= 0 then fail("reloaded file has " .. after .. " EditorCamera nodes, want 0") end
if root2:FindChildRecursively("EditorHelper") then fail("EditorHelper subtree leaked into save") end
if root2:FindChildRecursively("EditorHelperChild") then fail("child of editor-only node leaked") end
if not root2:FindChildRecursively("KeepMe") then fail("unflagged node missing after round-trip") end

print("editor_only: PASS (purged " .. before .. " stale, flagged nodes excluded)")
