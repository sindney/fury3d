-- smoke_hdr_import.lua — verify GltfImporter maps PBR slots when the
-- active pipeline is HDR (opsx camera-postprocess-hdr task 5.2).
-- Headless: ./fury exec <scene> tests/lua/smoke_hdr_import.lua
Pipeline.SetActive(PrelightPipeline.Create("pipeline"))
Pipeline.SetHDRMode(Pipeline.GetActive(), true)

-- tank.fbx path is relative to CWD (run from repo root).
local scene = Importer.LoadScene(FileUtil.GetAbsPath("examples/Resource/Scene/tank.fbx"))
if not scene then
    print("FAIL: tank.fbx import returned nil")
    return
end

local found, checked = 0, 0
scene:ForEachMaterial(function(mat)
    checked = checked + 1
    -- GetUniform returns a plain number for Uniform1f slots.
    local metallic = mat:GetUniform("metallic_factor")
    local roughness = mat:GetUniform("roughness_factor")
    if metallic ~= nil and roughness ~= nil then found = found + 1 end
    if checked <= 4 then
        print(string.format("material %s: metallic=%s roughness=%s",
            mat:GetName(),
            tostring(metallic), tostring(roughness)))
    end
end)

print(string.format("materials with PBR factor slots: %d/%d", found, checked))
print(found > 0 and found == checked and "OK: HDR import maps PBR slots" or "FAIL: PBR slots missing")
