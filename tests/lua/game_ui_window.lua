-- tests/lua/game_ui_window.lua -- runtime window/input/fps-plumbing smoke.
--
--   ./fury exec Projects/ocean/ocean_island.bin ../tests/lua/game_ui_window.lua

local function fail(msg)
    io.stderr:write("game_ui_window FAIL: " .. msg .. "\n")
    os.exit(1)
end

-- Window.* runtime controls (no-op headless, must not crash)
Window.SetFpsCap(60)
Window.SetFpsCap(false)
Window.SetVsync(true)
Window.SetVsync(false)
Window.SetResolution(640, 480)

-- InputUtil cursor + delta accumulator
local input = InputUtil.Instance()
input:SetCursorGrabbed(false)
input:SetCursorVisible(true)
if type(input:GetCursorGrabbed()) ~= "boolean" then
    fail("GetCursorGrabbed did not return boolean")
end
-- delta accumulator: with cursor not grabbed it must stay zero
local dx, dy = input:ConsumeMouseDelta()
if dx ~= 0 or dy ~= 0 then
    fail("mouse delta should be zero when cursor not grabbed, got " .. dx .. "," .. dy)
end

-- CharacterController first-person knobs (round-trip on a temporary instance)
local ctl = CharacterController.Create()
if ctl == nil then fail("CharacterController.Create returned nil") end
ctl:SetFirstPerson(true)
if not ctl:GetFirstPerson() then fail("GetFirstPerson after SetFirstPerson(true)") end
ctl:SetFirstPerson(false)
if ctl:GetFirstPerson() then fail("GetFirstPerson after SetFirstPerson(false)") end
ctl:SetMouseSensitivity(2.5)
if math.abs(ctl:GetMouseSensitivity() - 2.5) > 0.001 then
    fail("GetMouseSensitivity round-trip: " .. tostring(ctl:GetMouseSensitivity()))
end

-- Camera SetFov (project a temp camera; only SetFov is the new binding)
local cam = Camera.Create()
cam:PerspectiveFov(math.rad(75), 1.778, 0.1, 1000.0)
local before = cam:GetFov()
if before <= 0.0 then fail("Camera.GetFov returned " .. tostring(before)) end
cam:SetFov(math.rad(90))
local after = cam:GetFov()
if math.abs(after - math.rad(90)) > 0.001 then
    fail("Camera.SetFov did not apply: " .. tostring(after))
end
-- SetFov should preserve the aspect ratio
cam:SetAspect(2.0)
cam:SetFov(math.rad(60))
local fov = cam:GetFov()
if math.abs(fov - math.rad(60)) > 0.001 then
    fail("SetFov after SetAspect drifted: " .. tostring(fov))
end

print("game_ui_window OK")
os.exit(0)