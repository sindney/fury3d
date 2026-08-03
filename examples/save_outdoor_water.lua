local scene = Importer.LoadScene("Projects/outdoor/outdoor_water.json", "smooth")
if not scene then error("load failed") end
print("loaded, root children: " .. scene:GetRootNode():GetChildCount())
print("working_dir: " .. scene:GetWorkingDir())
local ok = FileUtil.SaveCompressedFile(scene, "Projects/outdoor/outdoor_water.bin")
if not ok then error("save failed") end
print("wrote bin")
Engine.Quit = true
