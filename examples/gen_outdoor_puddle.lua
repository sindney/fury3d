-- Adds an SSR test puddle to the outdoor project:
-- a big flat quad, roughness ~0 (mirror), dark blue, at y=0, plus a
-- bright red low-poly box floating above it so there's something to
-- reflect. Enables HDR + an [SSAO, SSR, ACES] chain and re-saves.
-- Run from examples/: ./fury gen_outdoor_puddle.lua
local scene = Importer.LoadScene("Projects/outdoor/outdoor.bin")
if not scene then error("failed to load outdoor.bin") end

-- puddle: 200x200 quad at origin, roughness 0.02 (mirror-like)
local puddle_mat = Material.Create("Puddle")
puddle_mat:SetUniform("diffuse_color", {0.06, 0.08, 0.10, 1})
puddle_mat:SetUniform("metallic_factor", 0.0)
puddle_mat:SetUniform("roughness_factor", 0.1)
scene:AddMaterial(puddle_mat)

local puddle = SceneNode.Create("Puddle")
puddle:SetLocalPosition(Vector4(500, 1, 500, 1))
puddle:SetLocalScale(Vector4(200, 200, 200, 1))
puddle:AddComponent(Transform.Create())
local puddle_mesh = MeshUtil.CreateQuad()
scene:AddMesh(puddle_mesh)
local pr = MeshRender.Create(puddle_mat, puddle_mesh)
puddle:AddComponent(pr)
puddle:Recompose(true)
scene:GetRootNode():AddChild(puddle)
scene:GetSceneManager():AddSceneNodeRecursively(puddle)

-- floating bright box above the puddle (reflection subject)
local box_mat = Material.Create("PuddleBox")
box_mat:SetUniform("diffuse_color", {0.9, 0.15, 0.1, 1})
box_mat:SetUniform("metallic_factor", 0.0)
box_mat:SetUniform("roughness_factor", 0.8)
box_mat:SetUniform("emissive_color", {0.6, 0.1, 0.05, 1})
box_mat:SetUniform("emissive_factor", 0.5)
scene:AddMaterial(box_mat)

local box = SceneNode.Create("PuddleBox")
box:SetLocalPosition(Vector4(500, 90, 500, 1))
box:SetLocalScale(Vector4(40, 40, 40, 1))
box:AddComponent(Transform.Create())
local box_mesh = MeshUtil.CreateCube()
scene:AddMesh(box_mesh)
local br = MeshRender.Create(box_mat, box_mesh)
box:AddComponent(br)
box:Recompose(true)
scene:GetRootNode():AddChild(box)
scene:GetSceneManager():AddSceneNodeRecursively(box)

-- HDR + SSR chain
local rs = scene:GetRenderSettings()
rs:SetPipelinePath("Resource/Pipeline/DefferedLightingPBR.json")
rs:SetHDR(true)
rs:ClearChain()
rs:AddEffect("SSAO", true)
rs:AddEffect("SSR", true)
rs:AddEffect("ACES", true)

if not FileUtil.SaveCompressedFile(scene, "Projects/outdoor/outdoor.bin") then
    error("save failed")
end
print("[puddle] outdoor.bin updated")
