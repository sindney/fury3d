# engine/Tools

Internal Python utilities. None are build-time dependencies — they're
hand-run during asset / docs iteration.

## Lua API docgen

`lua_api_docgen.py` regenerates `docs/LUA_API.md` from
`engine/Fury/LuaBindings.cpp`. Runs as a CMake post-build step, but
also safe to invoke manually:

```bash
python3 engine/Tools/lua_api_docgen.py \
  --src engine/Fury/LuaBindings.cpp \
  --out docs/LUA_API.md
```