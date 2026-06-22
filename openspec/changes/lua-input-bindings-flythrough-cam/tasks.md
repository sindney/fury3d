# Implementation Tasks — lua-input-bindings-flythrough-cam

## 1. Verify SFML 3 key-name availability

- [x] 1.1 In `engine/ThirdParty/SFML/include/SFML/Window/Keyboard.hpp`, confirm each of these names exists on `sf::Keyboard::Key`: `A` through `Z`, `Num0` through `Num9`, `Space`, `LShift`, `RShift`, `LControl`, `RControl`, `LAlt`, `RAlt`, `Up`, `Down`, `Left`, `Right`, `Escape`, `Enter`, `Tab`, `Backspace`. Note any renamed/dropped names; drop them from the binding list or substitute the SFML 3 name.
- [x] 1.2 Confirm `sf::Mouse::Button::Left`, `Right`, `Middle` exist (these were stable across SFML 2→3).

## 2. C++ bindings: enums

- [x] 2.1 In `engine/Fury/LuaBindings.cpp`, after the existing usertype blocks, add a `Key` global table populated with `lua["Key"]["A"] = static_cast<int>(sf::Keyboard::Key::A)` (one line per key from §1.1's verified list).
- [x] 2.2 Add a `MouseButton` global table with `Left`, `Right`, `Middle` entries.

## 3. C++ bindings: InputUtil usertype

- [x] 3.1 Include `Fury/InputUtil.h` at the top of `LuaBindings.cpp` (if not already included).
- [x] 3.2 Bind `InputUtil` as a usertype via `lua.new_usertype<InputUtil>("InputUtil", ...)`. Methods to bind (using lambdas where casts are needed):
  - `GetKeyDown(int)` → casts to `sf::Keyboard::Key`, returns bool.
  - `GetMouseDown()` (no-arg overload).
  - `GetMouseDown(int)` → casts to `sf::Mouse::Button`, returns bool. Combine via `sol::overload`.
  - `GetMousePosition()` → returns `std::make_tuple(int, int)` for multi-return.
  - `GetMouseWheel()` → direct member-pointer bind.
  - `GetWindowFocused()` → direct member-pointer bind.
  - `GetWindowSize()` → returns `std::make_tuple(int, int)` for multi-return.
- [x] 3.3 Add `lua["InputUtil"]["Instance"] = []() { return InputUtil::Instance(); };` mirroring the RenderUtil pattern.

## 4. C++ bindings: ImGui-capture queries

- [x] 4.1 In `engine/Fury/Gui.h`, declare two free functions: `bool FURY_API WantCaptureMouse();` and `bool FURY_API WantCaptureKeyboard();`.
- [x] 4.2 In `engine/Fury/Gui.cpp`, implement both to return `ImGui::GetIO().WantCaptureMouse` and `WantCaptureKeyboard` respectively. Wrap in `#ifdef _FURY_GUI_IMP_` like the rest of the file.
- [x] 4.3 In `LuaBindings.cpp`, extend the existing `Gui` namespace binding with `gui_tbl["WantCaptureMouse"] = &Gui::WantCaptureMouse;` and `gui_tbl["WantCaptureKeyboard"] = &Gui::WantCaptureKeyboard;`.

## 5. C++ bindings: SceneNode forward vector (if missing)

- [x] 5.1 Check current `SceneNode` Lua bindings (around `LuaBindings.cpp:186-205`): does `GetLocalRotation` exist? Does `Quaternion:Rotate(Vector4)` (or equivalent) exist?
- [x] 5.2 If `GetLocalRotation` is unbound, add it to the `SceneNode` usertype block.
- [x] 5.3 If `Quaternion` lacks a vector-rotation method binding, add it (the C++ method is typically named `Rotate(const Vector4&)` or `operator*`). Verify the C++ name in `engine/Fury/Quaternion.h` first.

## 6. Demo.lua: flythrough camera

- [x] 6.1 Add local state at the top of `Demo.lua`: `local move_speed = 8.0`, `local mouse_sensitivity = 0.0035`, `local pitch = 0.0`, `local yaw = math.rad(-30.0)` (initial values match the existing camera pose).
- [x] 6.2 Add `local last_mx, last_my = 0, 0` initialized once for mouse-drag delta tracking; capture initial position on the frame the left-mouse-button transitions down.
- [x] 6.3 In `on_update(dt)`, before the existing `Gui.ShowDefault` / `Pipeline:Execute`, do the flythrough math:
  - Read `InputUtil.Instance()` once into a local.
  - Skip movement if `Gui.WantCaptureKeyboard()` is true; skip mouse-look if `Gui.WantCaptureMouse()` is true.
  - Build a move vector in camera-local space from the WASD/arrow/Space/LControl key state.
  - Apply LShift 5× multiplier; integrate over `dt`; add the wheel-driven base-speed bump.
  - Apply mouse-drag yaw/pitch (clamp pitch to `[-math.rad(89), math.rad(89)]`).
  - Compose `Quaternion` from `yaw` then `pitch`, set `cam_node:SetLocalRoattion(q)`, set `cam_node:SetLocalPosition(new_pos)`, call `cam_node:Recompose(false)`.
- [x] 6.4 Keep the existing `Gui.ShowDefault(dt) / Gui.Render() / Pipeline.GetActive():Execute(octree)` block at the end of `on_update` unchanged.

## 7. Gui: ImGui slider/window forwarders for live tuning

- [x] 7.1 In `engine/Fury/Gui.h`, add four free functions guarded by `#ifdef _FURY_GUI_IMP_`:
  - `bool FURY_API Begin(const char* title);`
  - `void FURY_API End();`
  - `float FURY_API SliderFloat(const char* label, float current, float vmin, float vmax);`
  - `void FURY_API Text(const char* str);`
- [x] 7.2 In `engine/Fury/Gui.cpp`, implement each as a thin forward to ImGui. `Begin` returns `ImGui::Begin(title)`'s bool. `SliderFloat` makes a local `float v = current; ImGui::SliderFloat(label, &v, vmin, vmax); return v;` so the value flows in and out by value (sidesteps Lua/C++ pointer marshalling).
- [x] 7.3 In `engine/Fury/LuaBindings.cpp`, bind the four new functions onto the existing `gui_tbl` (`gui_tbl["Begin"] = &Gui::Begin;` etc.).

## 8. Demo.lua: live tuning panel

- [x] 8.1 In `Demo.lua` `on_update`, after `Gui.ShowDefault(dt)` and before `Gui.Render()`, draw the tuning panel:
  ```lua
  if Gui.Begin("Camera") then
      move_speed        = Gui.SliderFloat("Move Speed",        move_speed,        0.5,    50.0)
      mouse_sensitivity = Gui.SliderFloat("Mouse Sensitivity", mouse_sensitivity, 0.0005, 0.02)
  end
  Gui.End()
  ```
- [x] 8.2 Confirm the flythrough math (§6.3) reads `move_speed` and `mouse_sensitivity` as upvalues from these locals — not a frozen copy — so slider drags apply on the next frame.

## 9. Docs: LUA.md

- [x] 9.1 In `docs/LUA.md`, add a new `### Input` section under the `## Bound API reference` heading, before or after the existing `### Gui` block. Document `Key`, `MouseButton`, `InputUtil.Instance()`, and the available methods with Lua-side examples.
- [x] 9.2 Extend the `### Gui` section with `Gui.WantCaptureMouse()` / `Gui.WantCaptureKeyboard()` / `Gui.Begin(title)` / `Gui.End()` / `Gui.SliderFloat(label, value, min, max)` / `Gui.Text(str)`. Note the `value = Gui.SliderFloat(...)` return-the-new-value idiom explicitly.
- [x] 9.3 In the Gotchas section, remove the "No SFML-enum bindings yet. Keyboard / mouse events are not exposed to Lua in this round. Demo.lua has no input handling." entry. Replace with a shorter note: only the keys listed in `LuaBindings.cpp`'s `Key` table are pre-bound; add more there if needed.

## 10. README.md (English)

- [x] 10.1 Remove the FBX SDK bullet from Features. Add a Lua scripting bullet.
- [x] 10.2 Update C++11 → C++17 in the Introduction.
- [x] 10.3 Update the Plans section: mark "Implement GLTF file format parsing, dropping support for FbxSDK" as in-progress (the FBX SDK is dropped; tinygltf is vendored; the importer itself is the follow-up change). Leave the HDR/PBR bullet as-is.
- [x] 10.4 Rewrite the Compatibility section: remove the MSVC 2013 / Apple LLVM 7 line, replace with AppleClang 16, GCC 9+, MSVC 16+ (Visual Studio 2019+). Remove the "FBXSDK only offers MSVC builds" paragraph. Note `cmake -S engine -B build-engine && cmake --build build-engine --target fury` as the build path.
- [x] 10.5 Update "Tested libraries": SFML 3.1.0 (vendored), Rapidjson 1.1.0 (vendored), Lua 5.4.7 (vendored), sol2 v3.5.0 (vendored), tinygltf v2.9.7 (vendored). Note all are git submodules.
- [x] 10.6 Replace the C++ demo code snippet with a Lua snippet that mirrors `examples/Demo.lua`'s shape (scene load + camera + pipeline). Drop the `FbxParser::Instance()` line entirely.
- [x] 10.7 Add a short "Run the demo" subsection: `git clone --recursive`, `cmake -S engine -B build-engine`, `cmake --build build-engine --target fury`, `cd examples/bin && ./fury Demo.lua`. Note that WASD + mouse-drag controls the camera (this change's feature).
- [x] 10.8 Update Special Thanks: remove FBX SDK and ASSIMP entries; add tinygltf, sol2, Lua.

## 11. README.ZH-CN.md (Chinese)

- [x] 11.1 Mirror every change from §10 in the Chinese README. Keep the same section ordering and link structure.
- [x] 11.2 The Chinese phrasing for new entries: Lua 脚本绑定 (sol2 + Lua 5.4), C++17, tinygltf 取代 FBX SDK, SFML 3.1 等. The `examples/Demo.lua` snippet stays the same code, just with Chinese inline comments matching the existing translation style.

## 12. Build and run

- [x] 12.1 `/Applications/CMake.app/Contents/bin/cmake --build build-engine --target fury -j` from the repo root. Resolve any compile errors (most likely: an unrecognized SFML 3 key name).
- [x] 12.2 Copy fresh binary: `cp build-engine/fury examples/bin/fury`.
- [x] 12.3 Run `./fury Demo.lua` from `examples/bin/`. Confirm the window opens, the scene renders, and WASD + arrows + mouse-drag move the camera. Hold LShift → 5× speed. Mouse wheel → speed adjust. Click into the ImGui Profiler → camera should NOT move while focus is inside the widget.
- [x] 12.4 Drag the new "Move Speed" and "Mouse Sensitivity" sliders during the run; confirm camera feel changes immediately, no restart needed.
- [x] 12.5 Inspect `Log.txt` for `EROR`-level entries; none expected.

## 13. Commit (do NOT push)

- [ ] 13.1 `git status` — verify the modified set: `engine/Fury/Gui.{h,cpp}`, `engine/Fury/LuaBindings.cpp`, `examples/Demo.lua`, `docs/LUA.md`, `README.md`, `README.ZH-CN.md`, and the openspec change directory.
- [ ] 13.2 Stage and commit with a single message: `Add Lua input bindings + WASD/mouse flythrough demo + live tuning sliders; refresh READMEs`.
- [x] 13.3 Run `openspec validate lua-input-bindings-flythrough-cam --strict` and confirm.

## 14. Out-of-scope notes for archive

- [x] 14.1 Capture: signal-table bindings (`Input.OnKeyDown:Connect(closure)`) are deferred. Polling via `GetKeyDown` is sufficient for continuous input; signals matter only for one-shot events.
- [x] 14.2 Capture: the `Key` table is hand-rolled with ~40 entries; future scripts can add more by editing the `lua["Key"][...]` block.
- [x] 14.3 Capture: mouse cursor stays visible during drag (no `Mouse::setPosition` re-center). If multi-second drags feel cramped, add a "captured mouse" mode in a follow-up.
- [x] 14.4 Capture: the `Gui.SliderFloat / Begin / End / Text` set is the minimum useful subset of ImGui. Bind `Checkbox`, `InputFloat`, `Combo`, etc., as future demos need them — same return-the-new-value pattern.

## 15. Scope expansions during apply (captured for archive)

- [x] 15.1 Menu bar refactor in C++: dropped the no-op `Edit` menu; replaced with `View → Profiler / GBuffer / Shadow Buffers` toggles; all windows default-hidden. The original `Edit` menu items (Undo/Cut/Paste) were never wired.
- [x] 15.2 `Gui::SetMenuBarCallback(std::function<void()>)` added so Lua scripts can append top-level menus. Bound on the Lua side as `Gui.SetMenuBarCallback(fn_or_nil)`. The launcher's `Engine.run` lambda clears the callback after `Engine::Run` returns so the captured `sol::protected_function` doesn't dangle past `sol::state` teardown.
- [x] 15.3 Extra ImGui forwarders bound beyond the originally-planned set: `Gui.Checkbox`, `Gui.Button`, `Gui.Separator`, `Gui.BeginMenu`, `Gui.EndMenu`, `Gui.MenuItem`. Same return-the-new-value pattern as `SliderFloat`.
- [x] 15.4 `Gui.Begin` reshape: now takes `(title, open)` and returns `(still_open, visible)` so Lua-drawn windows get an X close button. The two-value return lets the caller persist visibility across frames. Original spec only had `Gui.Begin(title) -> bool`.
- [x] 15.5 `Demo.lua` consequences: Camera panel hidden by default; toggled via the new `Camera → Settings` Lua menu. The default look is a clean view with just the menu bar.
