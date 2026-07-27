-- tests/lua/smoke_render_settings_roundtrip.lua
-- Verifies that a Scene's renderSettings block survives save/reload:
-- pipeline path, HDR flag, CSM flag, and the postprocess chain (in
-- order, with enabled flags). Legacy scenes without the block
-- default to LDR + CSM-on + empty chain.
--
-- Usage:
--   ./fury exec scene.json tests/lua/smoke_render_settings_roundtrip.lua
--
-- The script:
--   1. Reads the active scene's renderSettings (after import).
--   2. Records the values (or applies a known config).
--   3. Saves to a temp file, reloads, and compares.
--   4. Exits non-zero on any mismatch.

local scene = Scene.GetActive()
if not scene then
    io.stderr:write("smoke_render_settings_roundtrip: Scene.GetActive() returned nil\n")
    os.exit(1)
end

local rs = scene:GetRenderSettings()
if not rs then
    io.stderr:write("smoke_render_settings_roundtrip: GetRenderSettings() returned nil\n")
    os.exit(1)
end

-- Apply a known config so we can verify all fields survive.
rs:SetPipelinePath("Resource/Pipeline/DefferedLightingLambert.json")
rs:SetHDR(false)
rs:SetCascadedShadowMap(true)
rs:ClearChain()
rs:AddEffect("ACES", true)
rs:AddEffect("FXAA", true)
rs:AddEffect("CRT", false) -- disabled entry — should still round-trip

local function dump(label)
    io.stderr:write(string.format("[%s] pipeline=%q hdr=%s csm=%s chain:",
        label, rs:GetPipelinePath(),
        tostring(rs:IsHDR()), tostring(rs:IsCascadedShadowMap())))
    local chain = rs:GetChain()
    for i = 1, #chain do
        local e = chain[i]
        io.stderr:write(string.format(" %s%s",
            e.effectName, e.enabled and "*" or "-"))
    end
    io.stderr:write("\n")
end

dump("before")

local out_path = os.tmpname() .. ".json"
local ok = Scene.SaveActive(out_path)
if not ok then
    io.stderr:write("smoke_render_settings_roundtrip: SaveActive failed\n")
    os.exit(1)
end

-- Reload into the same scene.
Scene.GetActive():Clear()
local reload_ok = Scene.LoadActive(out_path)
if not reload_ok then
    io.stderr:write("smoke_render_settings_roundtrip: LoadActive failed\n")
    os.exit(1)
end

local rs2 = Scene.GetActive():GetRenderSettings()
dump("after")

-- Verify each field.
if rs2:GetPipelinePath() ~= "Resource/Pipeline/DefferedLightingLambert.json" then
    io.stderr:write("FAIL: pipeline path mismatch\n")
    os.exit(1)
end
if rs2:IsHDR() ~= false then
    io.stderr:write("FAIL: HDR mismatch\n")
    os.exit(1)
end
if rs2:IsCascadedShadowMap() ~= true then
    io.stderr:write("FAIL: CSM mismatch\n")
    os.exit(1)
end
local chain2 = rs2:GetChain()
if #chain2 ~= 3 then
    io.stderr:write("FAIL: chain size mismatch (got " .. #chain2 .. ")\n")
    os.exit(1)
end
if chain2[1].effectName ~= "ACES" or not chain2[1].enabled then
    io.stderr:write("FAIL: chain[1] mismatch\n")
    os.exit(1)
end
if chain2[2].effectName ~= "FXAA" or not chain2[2].enabled then
    io.stderr:write("FAIL: chain[2] mismatch\n")
    os.exit(1)
end
if chain2[3].effectName ~= "CRT" or chain2[3].enabled then
    io.stderr:write("FAIL: chain[3] (disabled CRT) mismatch\n")
    os.exit(1)
end

-- Cleanup
os.remove(out_path)

print("OK: renderSettings round-trip preserved all fields")
os.exit(0)