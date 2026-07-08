## ADDED Requirements

### Requirement: A CMake-time generator SHALL emit `docs/LUA_API.md` from `engine/Fury/LuaBindings.cpp`

A doc-generator script (located at `engine/Tools/lua_api_docgen.py` or `engine/Fury/Tools/LuaApiDocGen.cpp`) SHALL scan `engine/Fury/LuaBindings.cpp` for `sol::new_usertype<T>(...)` calls and `lua.create_named_table(...)` calls, and emit a Markdown reference at `docs/LUA_API.md`. The reference SHALL contain, per binding:

- The Lua name (type name or namespace name).
- For usertypes: every method and property bound (in source order), with a best-effort signature derived from the C++ member pointer or lambda.
- For namespace tables: every function bound (in source order), with a best-effort signature.

For bindings the scanner cannot fully introspect (e.g., complex lambda-bound members, computed-property bindings), the generator SHALL emit a marker `<!-- docgen: unable to introspect, see LuaBindings.cpp:LINE -->` at that binding's section so the gap is visible and reviewable.

The generator's output SHALL be deterministic: re-running on the same `LuaBindings.cpp` produces byte-identical `docs/LUA_API.md`. Bindings are listed in registration order (the order they appear in `LuaBindings::Register`); members within a binding are listed in source order.

#### Scenario: Generator emits docs/LUA_API.md
- **WHEN** the build runs the `lua_api_docgen` custom target
- **THEN** `docs/LUA_API.md` exists in the repository
- **AND** it contains a section for every `new_usertype` and `create_named_table` call in `LuaBindings.cpp`

#### Scenario: Generator output is deterministic
- **WHEN** the generator runs twice on the same `LuaBindings.cpp`
- **THEN** the two outputs are byte-identical

#### Scenario: Unparseable bindings are marked
- **WHEN** the scanner encounters a binding it cannot fully introspect (e.g., a lambda-bound property)
- **THEN** the output contains a `<!-- docgen: unable to introspect, see LuaBindings.cpp:LINE -->` marker at that binding's section
- **AND** the generator continues to the next binding without aborting

#### Scenario: Generator covers newly added bindings
- **WHEN** a developer appends a `Mesh` usertype binding to `LuaBindings.cpp` and rebuilds
- **THEN** `docs/LUA_API.md` contains a new section for `Mesh` with all its bound members

### Requirement: The docgen target SHALL be wired into the default build

The `lua_api_docgen` custom target SHALL be registered in `engine/CMakeLists.txt` and SHALL run as a `POST_BUILD` step of the `fury` target (so a normal `cmake --build build` regenerates `docs/LUA_API.md` whenever `LuaBindings.cpp` changes). The generator SHALL NOT block the build on parse errors — it SHALL emit markers for unparseable bindings and exit 0.

`docs/LUA_API.md` SHALL be checked into the repository (so agents reading the repo without building can see the API). A stale `docs/LUA_API.md` (one that doesn't match the generator's output for the current `LuaBindings.cpp`) SHALL be silently overwritten on the next build.

#### Scenario: Build regenerates docs/LUA_API.md
- **WHEN** the user runs `cmake --build build` after editing `LuaBindings.cpp`
- **THEN** `docs/LUA_API.md` is regenerated and reflects the new bindings
- **AND** the build completes successfully

#### Scenario: Generator does not block build on parse errors
- **WHEN** the scanner hits an unparseable binding in `LuaBindings.cpp`
- **THEN** the generator emits a marker at that section
- **AND** the generator exits 0
- **AND** the build continues to completion

#### Scenario: docs/LUA_API.md is checked in
- **WHEN** the repository is freshly cloned without a build
- **THEN** `docs/LUA_API.md` exists at the repo root (or `docs/` subdirectory) with content matching the last build

### Requirement: `docs/LUA.md` SHALL keep narrative sections, remove per-binding reference, and point to `docs/LUA_API.md`

`docs/LUA.md` SHALL retain its narrative sections: the `Engine.run` callback contract, the `arg` table convention, gotchas, screenshot flags, and the "Future expansion" recipe. The per-binding API reference portions SHALL be removed (now in `docs/LUA_API.md`). A pointer to `docs/LUA_API.md` and to `engine/Fury/LuaBindings.cpp` (as the source of truth) SHALL be added at the top of `docs/LUA.md`.

#### Scenario: docs/LUA.md points to docs/LUA_API.md
- **WHEN** the user reads `docs/LUA.md`
- **THEN** it contains a pointer to `docs/LUA_API.md` at the top
- **AND** it contains a pointer to `engine/Fury/LuaBindings.cpp` as the source of truth

#### Scenario: docs/LUA.md retains narrative
- **WHEN** the user reads `docs/LUA.md`
- **THEN** it retains the `Engine.run` contract section
- **AND** it retains the gotchas section
- **AND** it retains the "Future expansion" recipe

#### Scenario: docs/LUA.md no longer contains per-binding reference
- **WHEN** the user reads `docs/LUA.md`
- **THEN** it does NOT contain per-binding API reference sections (those now live in `docs/LUA_API.md`)
