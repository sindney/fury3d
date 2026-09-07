# Fury3D — Build flavors and debug overlays

Build-time feature gates for development-only code: which flavor a binary
is, and which visual debug overlays are compiled in. UE-style split:
flavor macros say *what the build is*, `WITH_*` gates say *what's in it*.

## Flavor markers (exactly one per config)

| Macro | Set by | Meaning |
|---|---|---|
| `FURY_BUILD_DEBUG` | `$<CONFIG:Debug>` | debug config, full checks |
| `FURY_BUILD_PROFILE` | `$<CONFIG:RelWithDebInfo>` | optimized + symbols |
| `FURY_BUILD_RELEASE` | `$<CONFIG:Release>` | optimized |
| `FURY_BUILD_SHIPPING` | `-DFURY_SHIPPING=ON` | packaging flavor; strips development-only features regardless of config |

The first three are generator-expression markers (`#ifdef` style, no
value); `FURY_BUILD_SHIPPING` layers on top of any config via the
`FURY_SHIPPING` CMake option. Use `#ifdef`/`#if defined(...)` for all four.

## `WITH_DBG_OVERLAY` — visual debug overlays

Default **ON** in every flavor except Shipping (the option is forced OFF
when `FURY_SHIPPING=ON`; override manually with
`-DFURY_WITH_DBG_OVERLAY=0/1`). Always defined as 0/1 on fury/furye
targets, so both `#if` and `#ifdef` work.

Currently gated:

- **LOD tier tint** (`PipelineSwitch::LOD_DEBUG_COLORS`): the C++ bind in
  `PrelightPipeline` (instanced + non-instanced draw paths), the
  `GetLodDebugColor` palette in `Pipeline.cpp`, and the `lod_debug_color`
  uniform block in `GBuffer.glsl`.

Mechanics: `Shader::Compile` mirrors the C++ macro into GLSL as
`#define WITH_DBG_OVERLAY` (same pattern as `WITH_EDITOR`), so shader
code can be stripped per-flavor too.

### Using the LOD view (editor + headless)

The tint multiplies gbuffer diffuse by a per-tier palette:
LOD 0 green, 1 yellow, 2 red, 3 cyan, 4 magenta, 5 white (wraps modulo 6).

- Editor: viewport Debug views dropdown -> LOD colors
  (`EditorWindows.cpp`).
- Headless: `pl:SetSwitch(5, true)` from Lua (5 = `LOD_DEBUG_COLORS`
  ordinal), e.g. `tests/lua/lod_transition_check.lua` with
  `FURY_LOD_DEBUG=1`.

### Adding a new overlay feature

1. Add a `PipelineSwitch` enum entry (before `LENGTH`) — ordinals are
   Lua-visible, so append only.
2. Gate the C++ side with `#if WITH_DBG_OVERLAY`.
3. Gate the GLSL side with `#ifdef WITH_DBG_OVERLAY`.
4. When the feature has an "off" state in a retained uniform, bind the
   neutral value every frame the switch is off (GL programs retain
   uniforms — see the `lod_debug_color` vec4(0) pattern).
5. Verify: default build shows the overlay; `-DFURY_SHIPPING=ON` build
   renders identically with the switch on or off.

## Related but separate

- `WITH_EDITOR` (0/1 per target): editor shell vs headless runtime —
  ImGui/ImGuizmo/nfd and editor TUs. Independent of flavor: `fury`
  (headless) still carries `WITH_DBG_OVERLAY=1` by default.
- `FURY_TRACY_GPU`: Tracy profiler GPU zones, per-target option.
