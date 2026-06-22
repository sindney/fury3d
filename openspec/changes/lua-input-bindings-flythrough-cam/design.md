## Context

`InputUtil` already does the right thing C++-side. `Engine::HandleEvent` (post SFML 3 migration) dispatches each event variant via `event.is<...>()` / `event.getIf<...>()` and populates `m_KeyDown[sf::Keyboard::KeyCount]`, `m_MouseDown[sf::Mouse::ButtonCount]`, `m_MousePosition`, `m_MouseWheel`, `m_WindowFocused`, `m_WindowSize` — and fires `OnKeyDown`/`OnKeyUp`/`OnMouseMove`/etc. signals. The Lua layer just can't see any of it.

Constraints:

- `sf::Keyboard::Key` is a scoped enum class in SFML 3. Sol2 *can* bind C++ enums directly via `lua.new_enum<>`, but that produces a usertype with limited ergonomics from Lua (`Key.W` would work; `if key == Key.W` would work only if the comparison goes through the bound `==` operator). A plain integer-valued Lua table is easier to use and easier to extend.
- `InputUtil` is a singleton via the `Singleton<T, int, int>` template. Sol2 binds singletons cleanly through `new_usertype` + a static `Instance()` accessor (the pattern is already established for `RenderUtil` in this codebase).
- The flythrough camera math touches `SceneNode::SetLocalPosition`, `SetLocalRoattion`, `Recompose`, plus `Quaternion` ops and `Vector4` arithmetic. All are already bound — the change is purely Lua-side once the input layer is wired.
- `Engine::HandleEvent` already calls `Gui::HandleEvent(event)` after dispatch, so ImGui captures input when the cursor is over a widget. If we don't gate camera movement on `!io.WantCaptureMouse / !io.WantCaptureKeyboard`, the user dragging the Profiler window will also yaw the camera. Decide in this design.

## Goals / Non-Goals

**Goals:**

- Make every common keyboard key and the three mouse buttons addressable from Lua.
- Make `InputUtil` queryable from Lua with the same polling pattern as C++ (`if InputUtil:GetKeyDown(Key.W) then ...`).
- Ship a working WASD + mouse-drag flythrough camera in `Demo.lua` that demonstrates the binding surface end-to-end.
- Keep the binding compile-time stable — no `lua.new_enum<>` template explosion that adds 10s to `LuaBindings.cpp` compile time.
- Update the two READMEs to reflect the actual state of the project (post FBX→tinygltf, post SFML 2→3, post sol2/Lua landing).

**Non-Goals:**

- Full coverage of `sf::Keyboard::Key`. SFML 3 has ~95 keys; we bind ~40. The binding table is plain Lua, so adding `Key.F11` is one line.
- A reusable C++ `FlythroughController` component. The flythrough lives in `Demo.lua`.
- Mouse cursor hiding / locking. Visible cursor + drag is fine for a demo.
- Connecting the `OnKeyDown` / `OnMouseMove` signals from Lua. The C++ `Signal<>` API binds member-function pointers, not arbitrary closures — bridging that to Lua is a separate, larger effort. Polling via `GetKeyDown` is sufficient for the flythrough.
- Gamepad / joystick.
- Adding `vsync` to `EngineOptions`. Listed as future work in the previous change; still future.

## Decisions

### Decision 1: Bind `Key` and `MouseButton` as plain Lua tables of integers, not sol2 enums

In `LuaBindings.cpp` we will write:

```cpp
sol::table key = lua.create_named_table("Key");
key["A"] = static_cast<int>(sf::Keyboard::Key::A);
key["W"] = static_cast<int>(sf::Keyboard::Key::W);
// ... ~40 entries
key["Space"]    = static_cast<int>(sf::Keyboard::Key::Space);
key["LShift"]   = static_cast<int>(sf::Keyboard::Key::LShift);
// etc.
```

Sol2's `new_enum<>` would give us a typed enum on the Lua side, which is "safer" but harder to extend (`Key["F11"] = sf::Keyboard::Key::F11` doesn't work — you have to re-declare the enum block). Plain integer table = trivially extensible, costs nothing at compile time.

**Alternative considered:** generate the binding block via a macro that takes a list of enum names. Rejected — premature, the ~40-entry block is fine hand-written for this round.

### Decision 2: Bind `InputUtil` as a usertype with `Instance()`, mirroring `RenderUtil`

The existing pattern in `LuaBindings.cpp:250` is:

```cpp
lua["RenderUtil"]["Instance"] = []() { return RenderUtil::Instance(); };
```

Do the same for `InputUtil`. Then bind the methods. For `GetKeyDown(sf::Keyboard::Key key)` the C++ signature takes an enum; from Lua we get an integer. Wrap with a lambda that casts:

```cpp
lua.new_usertype<InputUtil>("InputUtil",
    "GetKeyDown",      [](InputUtil &self, int key) { return self.GetKeyDown(static_cast<sf::Keyboard::Key>(key)); },
    "GetMouseDown",    sol::overload(
        [](InputUtil &self) { return self.GetMouseDown(); },
        [](InputUtil &self, int btn) { return self.GetMouseDown(static_cast<sf::Mouse::Button>(btn)); }),
    "GetMousePosition", [](InputUtil &self) { auto p = self.GetMousePosition(); return std::make_tuple(p.first, p.second); },
    "GetMouseWheel",    &InputUtil::GetMouseWheel,
    "GetWindowFocused", &InputUtil::GetWindowFocused,
    "GetWindowSize",    [](InputUtil &self) { int w, h; self.GetWindowSize(w, h); return std::make_tuple(w, h); });
lua["InputUtil"]["Instance"] = []() { return InputUtil::Instance(); };
```

The `std::make_tuple` returns become multi-return values in Lua: `local x, y = InputUtil.Instance():GetMousePosition()`.

**Alternative considered:** flatten everything into a global `Input.IsKeyDown(key)`, `Input.GetMouse()`, etc., and hide the singleton. Rejected — staying close to the C++ shape makes the binding's compile-time correctness easier to reason about, and Lua-side aliases can be written in `LUA.md` if we want ergonomics later.

### Decision 3: Do NOT bind the `On*` signals in this change

`Signal<Args...>::Connect` takes a member function pointer of a class that derives from `std::enable_shared_from_this`. To connect a Lua closure we would need to:

1. Wrap the closure in a C++ class that holds a `sol::function` and a `shared_from_this`.
2. Manage that class's lifetime against the Lua state's lifetime.
3. Handle Lua-side errors inside the dispatch (signals don't currently take `protected_function`).

That is a meaningful binding-layer expansion. The flythrough doesn't need it — `on_update(dt)` polls `GetKeyDown` every frame, which is the right pattern for continuous motion anyway. Document the gap; defer signals.

### Decision 4: Gate camera input on ImGui's `WantCaptureMouse` / `WantCaptureKeyboard`

If the user grabs the Profiler window and drags it, we don't want the camera to spin. Inside the flythrough's `on_update`, before touching `cam_node`, check ImGui's wants. We need a minimal Lua-side query for this. Two paths:

- (a) Bind `ImGui.GetIO().WantCaptureMouse` / `WantCaptureKeyboard` — clean, but pulls in more ImGui surface than this change needs.
- (b) Bind two small `Gui::WantCaptureMouse()` / `Gui::WantCaptureKeyboard()` accessors as C++ free functions in `Gui.{h,cpp}`, then bind those.

Choose **(b)**. One-line C++ additions, no ImGui-API leak into the Lua surface beyond what's needed.

### Decision 5: Flythrough math lives in `Demo.lua`, not a C++ helper

This is the hello-world for the binding surface. Putting it in C++ would defeat the demonstration. Math is straightforward:

```lua
-- Pseudocode for on_update(dt)
local move_speed = base_speed * (InputUtil.Instance():GetKeyDown(Key.LShift) and 5 or 1)
local forward = cam_node:GetLocalForward()  -- if not bound, derive from local rotation
-- ... accumulate desired delta from key state ...
cam_node:SetLocalPosition(new_pos)
cam_node:SetLocalRoattion(new_rot)
cam_node:Recompose(false)
```

Open: does `SceneNode` expose `GetLocalForward()` to Lua? If not, derive forward from `GetLocalRotation():RotateVector(Vector4(0, 0, -1, 0))` (which assumes both `GetLocalRotation` and `Quaternion:RotateVector` are bound — verify in implementation, add the missing accessor binding if needed; treat that as a small extension within this change rather than a separate one).

### Decision 6: Tuning sliders via a narrow C++ helper, not a full ImGui binding

For the live `Move Speed` / `Mouse Sensitivity` panel, we have two paths:

- (a) **Bind enough of ImGui to Lua** that the script writes `ImGui.Begin("Camera"); ImGui.SliderFloat("Move Speed", move_speed, 0.5, 50.0); ImGui.End()`. This means binding `Begin`/`End`/`SliderFloat`/`Text` and figuring out the in/out semantics for `SliderFloat`'s pointer argument across the Lua/C++ boundary (sol2 does not auto-convert a Lua number to a mutable `float*`).
- (b) **Ship a purpose-built `Gui::ShowFlythroughControls` helper** in `Gui.{h,cpp}` that takes references to the two values and draws the panel. Binding it to Lua needs the same pointer-vs-value workaround.
- (c) **A middle path**: add a small bridge of `Gui::SliderFloat(label, current, min, max) -> float` that takes the current value by value and returns the (possibly updated) new value. Lua calls `move_speed = Gui.SliderFloat("Move Speed", move_speed, 0.5, 50.0)`. Same pattern for `Gui.Begin(title) / Gui.End()`. Three tiny C++ wrappers, no pointer-semantics gymnastics, and the Lua side stays in the script.

Choose **(c)**. It introduces three new free functions (`Begin`, `End`, `SliderFloat`) plus an optional `Gui.Text(str)` for labels — each one a one-line forwarder over ImGui. The Lua signature `value = Gui.SliderFloat(label, value, min, max)` is idiomatic Lua and trivially extensible: when we want a `Gui.Checkbox` later, the same return-the-new-value pattern works.

**Alternative considered:** ship a single fat `Gui::ShowFlythroughControls(...)` that hardcodes the demo's sliders. Rejected — too specific. The thin generic-slider path is reusable.

**Trade-off:** we're growing the `Gui` surface by ~four functions. Acceptable; they're each thin forwarders and they unblock the entire class of "small Lua-driven tuning panels" that this project will want as more demos land.

### Decision 7: Update both READMEs to current state in this same change

The READMEs are stale enough to be misleading — they recommend FBX SDK and SFML 2.4.1. Folding the refresh into this change is justified because:

- This is the third post-migration change; the cumulative drift is now large.
- Anyone discovering the project via README has no path to the working build.
- Doing it separately means another commit + review cycle for a doc-only refresh.

Scope: drop FBX SDK / fbxsdk paragraphs, replace with tinygltf + JSON/LZ4 scene path. Update SFML 2.4.1 → 3.1, C++11 → C++17, fix the demo code snippet to use the new API or replace with the Lua snippet, refresh Special Thanks (add tinygltf, sol2; remove FBX SDK, ASSIMP). Keep version badge `v0.2.1` (a bump is a separate decision).

## Risks / Trade-offs

- **Risk: AppleClang refuses `static_cast<int>(sf::Keyboard::Key::Tilde)` in the binding table because Tilde was renamed.** → Mitigation: only bind keys we know exist in SFML 3.1. SFML 3 did rename some keys (e.g., `Tilde` → `Grave`, `BackSlash` → `Backslash`); the proposal's list (A–Z, 0–9, Space, LShift/RShift, LControl/RControl, LAlt/RAlt, Up/Down/Left/Right, Escape, Enter, Tab, Backspace) only contains uncontroversial names. Cross-check each against `engine/ThirdParty/SFML/include/SFML/Window/Keyboard.hpp` before the binding lands.

- **Risk: Sol2's `std::make_tuple` multi-return doesn't work for the `GetMousePosition` / `GetWindowSize` patterns.** → Mitigation: it does work in sol2 v3.5; verify with a one-line test before relying on it. If it doesn't, fall back to returning a 2-element table.

- **Risk: The flythrough turns out to feel terrible because of dt scaling, key sticky on focus loss, etc.** → Mitigation: the spec scenarios cover the obvious failure modes (move speed, pitch clamping, focus loss). Iterate during apply if the feel is off.

- **Risk: README updates conflict with concurrent doc edits.** → Mitigation: there are none in flight. Land the change in one commit.

- **Trade-off: We're not binding signals.** → Documented. Polling is correct for continuous input. One-shot bindings (toggle screenshot on F12) will need either signal bindings later or a poll-with-edge-detect helper in Lua.

- **Trade-off: We bind only ~40 keys.** → Documented as extensible. Any user can edit the `key[...]` block in `LuaBindings.cpp`.

## Migration Plan

1. Land the C++ binding additions in `LuaBindings.cpp` (new `Key` table, `MouseButton` table, `InputUtil` usertype, two `Gui::WantCapture*` accessors + bindings).
2. If needed, expose `SceneNode::GetLocalRotation` and `Quaternion::RotateVector` to Lua — depends on what's already bound.
3. Rewrite `examples/Demo.lua` to add the flythrough camera. Keep `on_init` / `on_update` / `on_shutdown` shape; add per-frame camera logic to `on_update`.
4. Update `docs/LUA.md`: add §Input section, remove the obsolete "No SFML-enum bindings" gotcha.
5. Update `README.md` and `README.ZH-CN.md` per Decision 6.
6. Build, run, verify the spec scenarios manually.
7. Commit (do not push).

Rollback: revert the single commit. The binding additions are purely additive — nothing existing is rebound or renamed.

## Open Questions

- Should we also bind `InputUtil` signals (`OnKeyDown`, `OnMouseWheel`) in this change? **Decided: no** (Decision 3). Tracked for a follow-up `lua-signals` change.
- Move-speed default for the flythrough — `5 units/sec`? `10`? Decide during apply by feel.
- README version badge — bump to `v0.3.0` to reflect the post-Lua state, or leave at `v0.2.1`? **Decided: leave at `v0.2.1`** for this change; version bumps deserve their own discussion.
- Whether to update the `screenshots/` images. Out of scope.
