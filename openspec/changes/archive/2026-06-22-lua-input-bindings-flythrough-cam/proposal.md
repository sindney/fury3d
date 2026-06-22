## Why

`Demo.lua` runs and renders, but the camera is bolted in place at `(0, 10, 25)` looking down 30° — no WASD, no arrows, no mouse-drag. This isn't a regression: the historical C++ `Demo.cpp` (commit `cc79a90`) was also view-only, and the SFML 2 → 3 migration left `Engine::HandleEvent` correctly populating `InputUtil::m_KeyDown[]` and emitting `OnKeyDown` / `OnMouseMove`. The missing piece is the Lua bridge: `LuaBindings.cpp` exposes zero of `InputUtil`, `sf::Keyboard::Key`, or `sf::Mouse::Button`, so a script literally cannot ask "is W pressed?". `docs/LUA.md` already calls this out as a known gap ("No SFML-enum bindings yet"). This change closes it and proves the surface with a flythrough camera in `Demo.lua`. As a parallel concern, `README.md` and `README.ZH-CN.md` still describe a project that uses FBX SDK, SFML 2.4.1, and a C++11 `FbxParser` API that no longer exists — they predate the tinygltf, SFML 3.1, and sol2 changes that landed in the last three commits. Bring them into sync.

## What Changes

- Add Lua bindings for `InputUtil` accessors: `InputUtil.GetKeyDown(key)`, `InputUtil.GetMouseDown(btn)`, `InputUtil.GetMousePosition()`, `InputUtil.GetMouseWheel()`, `InputUtil.GetWindowFocused()`, `InputUtil.GetWindowSize()`. Returned as a singleton-style table (`InputUtil.Instance():GetKeyDown(...)` to mirror the C++ shape, with a short-hand `Input.IsKeyDown(...)` etc. for ergonomics — finalize in design).
- Add Lua enum tables `Key` and `MouseButton` covering the keys and buttons the demo and likely first-party tools will use: `A`–`Z`, `0`–`9`, `Space`, `LShift`, `LControl`, `LAlt`, `Up`, `Down`, `Left`, `Right`, `Escape`, `Enter`, plus `MouseButton.Left` / `Right` / `Middle`. Full enum coverage is out of scope; the binding surface is extensible by editing `LuaBindings.cpp` later.
- Add signal-table bindings: `Input.OnKeyDown:Connect(function(key) ... end)` and `Input.OnMouseMove:Connect(function(x, y) ... end)`. Polling via `GetKeyDown` is the primary path; signals are for one-shot events (e.g., toggling something on a single keypress) — finalize the signal-bind shape in design.
- Update `examples/Demo.lua` to implement a simple WASD flythrough camera:
  - WASD / arrow keys translate the camera in its local frame.
  - Hold left mouse button + drag → yaw + pitch.
  - Mouse wheel → adjust move speed.
  - Hold LShift → 5× move-speed multiplier.
  - All driven from `on_update(dt)` by reading `InputUtil` state and calling `cam_node:SetLocalPosition(...)` / `cam_node:SetLocalRoattion(...)` followed by `cam_node:Recompose(false)`.
- Add a tiny ImGui tuning panel so the user can feel-test the camera while it's running. Either:
  - (a) Bind enough of ImGui to Lua for the script to draw its own `SliderFloat`-based panel, or
  - (b) Ship a purpose-built `Gui::ShowFlythroughControls(...)` C++ helper that draws two sliders (`move_speed`, `mouse_sensitivity`) and writes into Lua-owned values via reference. Finalize in design.
- Add a tiny ImGui tuning panel so the user can feel-test the camera while it's running. Either:
  - (a) Bind enough of ImGui to Lua for the script to draw its own `SliderFloat`-based panel, or
  - (b) Ship a purpose-built `Gui::ShowFlythroughControls(...)` C++ helper that draws two sliders (`move_speed`, `mouse_sensitivity`) and writes into Lua-owned values via reference. Finalize in design.
- Refresh `README.md` and `README.ZH-CN.md`:
  - Drop the entire FBX SDK section, replace with tinygltf (planned for the follow-up `GltfImporter` change) and the existing JSON/LZ4 scene path.
  - Update SFML 2.4.1 → SFML 3.1, Rapidjson 1.1.0 wording (vendored as submodule now, not "tested with").
  - Add Lua scripting bullet to features + a short "Try it" snippet pointing at `examples/Demo.lua`.
  - Update C++11 → C++17.
  - Update toolchain wording: AppleClang 16 / GCC 9+ / MSVC 16+, no FBX SDK requirement.
  - Update build instructions to current `cmake -S engine -B build-engine && cmake --build build-engine --target fury` flow and the `./fury Demo.lua` launch command.
  - Update Special Thanks: remove FBX SDK and ASSIMP (engine doesn't use ASSIMP), add tinygltf and sol2.
  - Keep version badge `v0.2.1` for now — bumping versions is a separate concern.
- Document the new bindings in `docs/LUA.md` (new §`Input` section after `Gui`) and drop the "no SFML-enum bindings yet" gotcha line that's now obsolete.

## Capabilities

### New Capabilities
- `lua-input`: The Lua-facing input surface — keyboard / mouse enum tables, `InputUtil` accessor bindings, signal connections. The capability covers what state is queryable, how enums map between SFML and Lua, and what guarantees the bindings make about per-frame freshness.

### Modified Capabilities
<!-- None. lua-scripting is not yet archived. engine-presentation (from fix-demo-fps-profiler-retina) is also not yet archived. No archived spec to MODIFY. -->

## Impact

- Code: `engine/Fury/LuaBindings.{h,cpp}` (new bindings block), `engine/Fury/Gui.{h,cpp}` (two `WantCapture*` accessors + a minimal ImGui slider helper for the flythrough panel), `examples/Demo.lua` (flythrough camera + bind the panel).
- Documentation: `docs/LUA.md` (new §Input + drop obsolete gotcha), `README.md` + `README.ZH-CN.md` (major refresh).
- No C++ engine code changes — `InputUtil`, `Engine::HandleEvent`, and the SFML 3 event dispatch are already correct.
- No new submodules, no new dependencies.
- Out of scope:
  - Binding the full ~90-key `sf::Keyboard::Key` enum. We bind the demo's needs + the common-case set; future scripts can extend the binding table directly.
  - A reusable `CameraController` C++ component. The flythrough lives in `Demo.lua`; if a second demo wants it, refactor then.
  - Gamepad / joystick support (SFML supports it, `InputUtil` does not).
  - Mouse cursor hiding / pointer locking while dragging. SFML 3's `Mouse::setPosition` works, but we leave the cursor visible for now to keep the demo simple.
- Verification: rebuild, launch `./fury Demo.lua`, drive the camera around the scene with WASD + mouse-drag, confirm no `EROR` lines in `Log.txt`.
