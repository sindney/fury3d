-- A textured sphere needs spherical UVs, a split meridian, and a consistent
-- tangent frame on both hemispheres. Run via the headless manifest.
local function check(ok, message)
    if not ok then error("sphere_uv: " .. message) end
end

-- A simple reference also catches UV reads beyond the two-component channel.
local quad = MeshUtil.CreateQuad()
MeshUtil.CalculateTangent(quad)
local qt = quad:GetTangents()
for k = 1, #qt, 3 do
    check(math.abs(qt[k] - 1) + math.abs(qt[k+1]) + math.abs(qt[k+2]) < 1e-5,
        "quad tangent must point along +X")
end

for _, segments in ipairs({ 3, 8, 32 }) do
    local sphere = MeshUtil.CreateSphere(segments)
    MeshUtil.CalculateTangent(sphere)
    local p, n = sphere:GetPositions(), sphere:GetNormals()
    local uv, t, indices = sphere:GetUVs(), sphere:GetTangents(), sphere:GetIndices()
    check(#uv == #p / 3 * 2 and #n == #p and #t == #p, "missing vertex channel")
    check(#indices == 6 * segments * (segments - 2), "unexpected triangle count")
    local seam = {}
    for i = 1, #p / 3 do
        local k, j = (i - 1) * 3, (i - 1) * 2
        local x, y, z, u, v = p[k+1], p[k+2], p[k+3], uv[j+1], uv[j+2]
        check(u >= 0 and u <= 1 and v >= 0 and v <= 1, "UV out of range")
        check(math.abs(y - 0.5 * math.cos(math.pi * v)) < 1e-5, "latitude is projected")
        check(math.abs(x - 0.5 * math.sin(math.pi * v) * math.cos(2*math.pi*u)) < 1e-5,
            "longitude X mismatch")
        check(math.abs(z - 0.5 * math.sin(math.pi * v) * math.sin(2*math.pi*u)) < 1e-5,
            "longitude Z mismatch")
        check(math.abs(n[k+1] - 2*x) + math.abs(n[k+2] - 2*y) + math.abs(n[k+3] - 2*z) < 1e-4,
            "normal does not point radially out")
        if v > 0 and v < 1 then
            local tx, tz = -math.sin(2*math.pi*u), math.cos(2*math.pi*u)
            check(t[k+1] * tx + t[k+3] * tz > 0.45, "tangent points against U")
            local bx = n[k+2]*t[k+3] - n[k+3]*t[k+2]
            local by = n[k+3]*t[k+1] - n[k+1]*t[k+3]
            local bz = n[k+1]*t[k+2] - n[k+2]*t[k+1]
            local cosv = math.cos(math.pi*v)
            check(bx*cosv*math.cos(2*math.pi*u) - by*math.sin(math.pi*v)
                + bz*cosv*math.sin(2*math.pi*u) > 0.45, "bitangent points against V")
            if u == 0 or u == 1 then
                local key = string.format("%.5f", v)
                seam[key] = (seam[key] or 0) + 1
            end
        end
    end
    local split_count = 0
    for _, count in pairs(seam) do
        check(count == 2, "meridian is not split at U=0/1")
        split_count = split_count + 1
    end
    check(split_count == segments - 2, "missing split meridians")
    for f = 1, #indices, 3 do
        local a, b, c = indices[f], indices[f+1], indices[f+2]
        local ax, ay, az = p[3*a+1], p[3*a+2], p[3*a+3]
        local bx, by, bz = p[3*b+1]-ax, p[3*b+2]-ay, p[3*b+3]-az
        local cx, cy, cz = p[3*c+1]-ax, p[3*c+2]-ay, p[3*c+3]-az
        local nx, ny, nz = by*cz-bz*cy, bz*cx-bx*cz, bx*cy-by*cx
        check(nx*ax + ny*ay + nz*az > 1e-9, "degenerate or inward triangle")
        local ua, ub, uc = uv[2*a+1], uv[2*b+1], uv[2*c+1]
        check(math.max(ua,ub,uc)-math.min(ua,ub,uc) <= 1/segments + 1e-5,
            "triangle interpolates across the wrap")
    end
end
print("sphere_uv: PASS")
