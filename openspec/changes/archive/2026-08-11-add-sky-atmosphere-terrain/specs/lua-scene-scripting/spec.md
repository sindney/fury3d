# lua-scene-scripting (delta)

## ADDED Requirements

### Requirement: Lua SHALL expose the SkyAtmosphere component

`SkyAtmosphere` SHALL be registered in `LuaBindings.cpp` with `Create()`,
getters/setters for time-of-day (hours, day length, auto-advance,
sun-from-TOD), sun-light node name, cloud settings (enable, coverage,
altitude, scale, wind), and the atmosphere coefficients, plus the typed
`SceneNode` accessors (`AddComponent`/`GetComponent`/`RemoveComponent`
overloads and a `GetSkyAtmosphere()` getter per the existing per-type
pattern).

#### Scenario: Script configures a sky

- **WHEN** a script does `node:AddComponent(SkyAtmosphere.Create())`, sets TOD to 18.5, sets the sun light name, and enables clouds
- **THEN** the reloaded scene restores those values and the rendered sky matches evening with clouds

### Requirement: Lua SHALL expose the Terrain component

`Terrain` SHALL be registered in `LuaBindings.cpp` with `Create()`,
setters for heightmap/splatmap paths, layer entries (name, texture, tiling),
chunk and LOD counts, a `Rebuild()` action, and `GetHeight(worldX, worldZ)`,
plus the typed `SceneNode` accessors per the existing per-type pattern.

#### Scenario: Script builds terrain and places a prop on it

- **WHEN** a script adds a `Terrain`, sets `Terrain/height.r16`, calls `Rebuild()`, then positions a crate at `y = terrain:GetHeight(x, z)`
- **THEN** the crate rests on the terrain surface after save/reload and in play mode

### Requirement: Lua SHALL expose Color lerp

The `Color` usertype SHALL expose a `Lerp(a, b, t)` function (static or
method) so scripts can drive TOD-tinted colors without manual channel math.

#### Scenario: Lerp midpoint

- **WHEN** a script evaluates `Color.Lerp(Color(0,0,0,1), Color(1,1,1,1), 0.5)`
- **THEN** the result's channels equal 0.5 (within float epsilon)
