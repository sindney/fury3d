-- dump_ground.lua — print the ground material's water-relevant uniforms.
-- Usage: ./fury exec <scene> ../tests/lua/dump_ground.lua
local scene = Scene.GetActive()
if not scene then error("no scene") end
local function fmt(u)
    if type(u) == "table" then
        local parts = {}
        for i = 1, #u do parts[#parts + 1] = string.format("%.3f", u[i]) end
        return "(" .. table.concat(parts, ",") .. ")"
    end
    return string.format("%.3f", u)
end
scene:ForEachMaterial(function(m)
    if m:GetName() == "Material_Ground" or m:GetName() == "Puddle" then
        local str = m:GetName()
        for _, key in ipairs({ "diffuse_color", "roughness_factor", "metallic_factor", "shininess" }) do
            local u = m:GetUniform(key)
            str = str .. " " .. key .. "=" .. (u and fmt(u) or "nil")
        end
        print(str)
    end
end)
