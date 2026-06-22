#ifndef _FURY_LUA_BINDINGS_H_
#define _FURY_LUA_BINDINGS_H_

#include <sol/forward.hpp>

namespace fury
{
	namespace LuaBindings
	{
		// Register all Demo-driven engine types and free functions on the given
		// Lua state. Idempotent (sol2 overwrites existing usertypes on re-register
		// but we don't rely on this — call once per state).
		void Register(sol::state_view lua);
	}
}

#endif // _FURY_LUA_BINDINGS_H_
