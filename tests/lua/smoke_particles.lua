-- tests/lua/smoke_particles.lua — regression guards for the particle system.
--
-- Covers the scriptable traps from the 2026-07-31 debug session (opsx
-- add-particle-system-fire-smoke-editor task 8.6; the dynamic-mesh
-- SetDirty + content-browser tile-cast traps are GPU/GUI-only and are
-- covered by the furye screenshot captures instead):
--
--   1. Name-based resolution: every ParticleRenderer in the scene
--      resolves its ParticleSystem asset via GetSystem() (the asset
--      model — systems live in the EntityManager, renderers reference
--      them by name).
--   2. Serialization round-trip: save → reload reproduces the systems
--      with their module state intact.
--   3. Importer.MergeInto transfers ParticleSystem assets (the original
--      trap: PS was missing from the transfer list → emitters stranded
--      in the discarded source scene + dropped on re-save).
--
-- Usage (from examples/):
--   ./fury exec Projects/outdoor/outdoor_water.bin ../../tests/lua/smoke_particles.lua

local function fail(msg)
    io.stderr:write("smoke_particles: FAIL: " .. msg .. "\n")
    os.exit(1)
end

local function walk(node, fn)
    fn(node)
    for i = 0, node:GetChildCount() - 1 do
        walk(node:GetChildAt(i), fn)
    end
end

local function collect_renderers(scene, out)
    out = out or {}
    walk(scene:GetRootNode(), function(node)
        local pr = node:GetParticleRenderer()
        if pr then out[#out + 1] = { node = node, pr = pr } end
    end)
    return out
end

local scene = Scene.GetActive()
if not scene then fail("Scene.GetActive() returned nil") end

-- ---- 1. name-based resolution ----------------------------------------------
local renderers = collect_renderers(scene)
if #renderers ~= 2 then
    fail("expected 2 ParticleRenderers, found " .. #renderers)
end
local seen = {}
for _, r in ipairs(renderers) do
    local sys = r.pr:GetSystem()
    if not sys then
        fail("renderer '" .. r.pr:GetName() .. "' did not resolve system '"
            .. tostring(r.pr:GetSystemName()) .. "'")
    end
    seen[sys:GetName()] = true
end
for _, want in ipairs({ "FireEmber", "SmokePlume" }) do
    if not seen[want] then fail("system '" .. want .. "' not referenced by any renderer") end
    if not scene:GetParticleSystem(want) then
        fail("Scene:GetParticleSystem('" .. want .. "') returned nil")
    end
end
-- Module spot-check against the authored values (add_particle_emitters.py).
local ember = scene:GetParticleSystem("FireEmber")
if math.abs(ember:GetLifetime() - 0.8) > 1e-4 then
    fail("FireEmber lifetime " .. ember:GetLifetime() .. " ~= 0.8")
end
if ember:GetMaxParticles() ~= 512 then
    fail("FireEmber maxParticles " .. ember:GetMaxParticles() .. " ~= 512")
end
print("1. name resolution OK (2 renderers -> 2 systems)")

-- ---- 2. serialization round-trip --------------------------------------------
local tmp = os.tmpname() .. ".json"
if not FileUtil.SaveFile(scene, tmp) then fail("SaveFile('" .. tmp .. "') failed") end
local reloaded = Importer.LoadScene(tmp)
os.remove(tmp)
if not reloaded then fail("reload of saved scene failed") end
for _, want in ipairs({ "FireEmber", "SmokePlume" }) do
    if not reloaded:GetParticleSystem(want) then
        fail("round-trip dropped ParticleSystem '" .. want .. "'")
    end
end
if math.abs(reloaded:GetParticleSystem("FireEmber"):GetLifetime() - 0.8) > 1e-4 then
    fail("round-trip changed FireEmber lifetime")
end
if #collect_renderers(reloaded) ~= 2 then
    fail("round-trip dropped ParticleRenderer nodes")
end
print("2. serialization round-trip OK (systems + renderers + modules survive)")

-- ---- 3. MergeInto transfers ParticleSystem assets ---------------------------
local target = Scene.Create("merge_target", FileUtil.GetAbsPath())
local merged = Importer.MergeInto(target, reloaded)
if merged <= 0 then fail("MergeInto merged 0 top-level children") end
for _, want in ipairs({ "FireEmber", "SmokePlume" }) do
    if not target:GetParticleSystem(want) then
        fail("MergeInto did NOT transfer ParticleSystem '" .. want
            .. "' (asset stranded in source scene)")
    end
end
-- And the renderers in the merged tree must still resolve — against the
-- TARGET scene's transferred assets.
Scene.SetActive(target)
for _, r in ipairs(collect_renderers(target)) do
    if not r.pr:GetSystem() then
        fail("post-merge renderer '" .. r.pr:GetName() .. "' lost its system")
    end
end
print("3. MergeInto transfer OK (systems resolve in the merged scene)")

print("smoke_particles: OK")
