-- Regenerate example projects with meters->cm scale correction:
-- import, measure world AABB, scale roots by 100 when tiny (< 100 units).
local jobs = {
    { src = "Projects/tank/tank.fbx",       dst = "Projects/tank/scene.bin" },
    { src = "Projects/outdoor/outdoor.fbx", dst = "Projects/outdoor/outdoor.bin" },
    { src = "Projects/james/james.fbx",     dst = "Projects/james/james.bin" },
}
for _, j in ipairs(jobs) do
    print("[gen] importing " .. j.src)
    local scene = Importer.LoadFbx(j.src)
    if not scene then error("LoadFbx failed: " .. j.src) end
    local bmin, bmax = scene:ComputeWorldAABB()
    if bmin then
        local size = bmax - bmin
        local dim = math.max(size.x, size.y, size.z)
        if dim < 100.0 then
            print(string.format("[gen] %.2f units across — scaling roots x100", dim))
            local root = scene:GetRootNode()
            for i = 0, root:GetChildCount() - 1 do
                local c = root:GetChildAt(i)
                c:SetLocalScale(c:GetLocalScale() * 100.0)
                c:Recompose(true)
            end
        end
    end
    print("[gen] saving " .. j.dst)
    if not FileUtil.SaveCompressedFile(scene, j.dst) then error("save failed: " .. j.dst) end
end
print("[gen] done")
