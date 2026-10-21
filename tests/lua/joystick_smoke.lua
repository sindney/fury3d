-- tests/lua/joystick_smoke.lua -- joystick API surface smoke test.
--
-- Verifies the JoyInputUtil joystick bindings are exposed and behave
-- without crashing on a headless host that has no controller.
--
--   ./fury exec Projects/ocean/ocean_island.bin ../tests/lua/joystick_smoke.lua

local function fail(msg)
    io.stderr:write("joystick_smoke FAIL: " .. msg .. "\n")
    os.exit(1)
end

local inst = JoyInputUtil.Instance()
if inst == nil then fail("JoyInputUtil.Instance() is nil") end

if type(JoyInputUtil.MaxJoysticks) ~= "number" then
    fail("JoyInputUtil.MaxJoysticks is not a number")
end
if JoyInputUtil.MaxJoysticks < 1 then
    fail("JoyInputUtil.MaxJoysticks = " .. tostring(JoyInputUtil.MaxJoysticks))
end

-- Every joystick id must report connected=false on a controller-less host
-- without throwing, and the button/axis getters must return safe defaults.
for i = 0, JoyInputUtil.MaxJoysticks - 1 do
    if inst:IsConnected(i) ~= false then
        fail("IsConnected(" .. i .. ") ~= false")
    end
    if inst:IsButtonDown(i, 0) ~= false then
        fail("IsButtonDown(" .. i .. ", 0) ~= false")
    end
    if inst:WasButtonPressed(i, 0) ~= false then
        fail("WasButtonPressed(" .. i .. ", 0) ~= false")
    end
    if type(inst:GetAxis(i, JoystickAxis.X)) ~= "number" then
        fail("GetAxis(" .. i .. ", X) did not return a number")
    end
    if inst:GetAxis(i, JoystickAxis.X) ~= 0.0 then
        fail("GetAxis on disconnected joystick should be 0")
    end
end

-- JoystickAxis must expose the same axes the bindings accept.
for _, name in ipairs({"X","Y","Z","R","U","V","PovX","PovY"}) do
    if type(JoystickAxis[name]) ~= "number" then
        fail("JoystickAxis." .. name .. " missing")
    end
end

print("joystick_smoke OK")
io.stdout:flush()
os.exit(0)
