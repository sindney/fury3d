-- tests/lua/dump_lights.lua — dump light pos, type, intensity, radius.
-- NOTE: only bindings that exist (docs/LUA_API.md): no GetWorldMatrix /
-- GetOutterAngle / GetEffectiveRadius / GetCastShadows on the Lua side.
local function walk(node, fn)
    fn(node)
    for i = 0, node:GetChildCount() - 1 do
        walk(node:GetChildAt(i), fn)
    end
end

-- LightType: 0 = DIRECTIONAL, 1 = POINT, 2 = SPOT (EnumUtil.h).
walk(Scene.GetActive():GetRootNode(), function(node)
    local l = node:GetLight()
    if l then
        local wp = node:GetWorldPosition()
        print(string.format(
            "%s pos=(%.1f,%.1f,%.1f) type=%d intensity=%.3f radius=%.1f",
            node:GetName(), wp.x, wp.y, wp.z,
            l:GetType(), l:GetIntensity(), l:GetRadius()))
    end
end)
