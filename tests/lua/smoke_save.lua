-- tests/lua/smoke_save.lua — round-trip a scene through the Lua save binding.
--
-- Verifies the Lua save path works end-to-end. Output format is inferred
-- from arg[1]'s extension (.json -> SaveFile, .bin -> SaveCompressedFile).
-- NOT a replacement for `fury convert` (which already does headless format
-- conversion); only proves the Lua binding can save.
--
-- Usage:
--   ./fury exec scene.json tests/lua/smoke_save.lua /tmp/out.bin
--   ./fury exec scene.bin tests/lua/smoke_save.lua /tmp/out.json

local out_path = arg[1]
if not out_path or out_path == "" then
    io.stderr:write("smoke_save: expected <out_path> as arg[1]\n")
    os.exit(1)
end

local scene = Scene.GetActive()
if not scene then
    io.stderr:write("smoke_save: Scene.GetActive() returned nil\n")
    os.exit(1)
end

local ok = Scene.SaveActive(out_path)
if not ok then
    io.stderr:write("smoke_save: SaveActive('" .. out_path .. "') failed\n")
    os.exit(1)
end

print("wrote " .. out_path)