// fury — engine launcher.
//
// Loads a Lua script (path on argv, defaulting to Demo.lua), boots the engine,
// hands control to the script's Engine.run() call, then shuts down cleanly.

#include <SFML/Window.hpp>

#include <cstdint>
#include <iostream>
#include <string>

#include <sol/sol.hpp>

#include <Fury/Fury.h>
#include <Fury/Gui.h>
#include <Fury/LuaBindings.h>

#undef near
#undef far
#undef max

int main(int argc, char *argv[])
{
	// Window setup. Mirrors the old Demo.cpp window shape.
	sf::ContextSettings settings;
	settings.depthBits = 24;
	settings.stencilBits = 8;
	settings.antiAliasingLevel = 0;
	settings.majorVersion = 3;
	settings.minorVersion = 3;

	sf::Window window(
		sf::VideoMode({1280, 720}),
		"Fury3d",
		sf::Style::Titlebar | sf::Style::Close,
		sf::State::Windowed,
		settings);
	window.setKeyRepeatEnabled(true);
	window.setVerticalSyncEnabled(false);
	(void)window.setActive();

	if (!fury::Engine::Initialize(window, 2, fury::LogLevel::DBUG,
		fury::FileUtil::GetAbsPath("Log.txt").c_str()))
	{
		std::cerr << "Engine::Initialize failed" << std::endl;
		return 1;
	}

	// Lua state lives only as long as the script's Engine.run call. We close it
	// before Engine::Shutdown so any lingering Lua-owned shared_ptrs (scene
	// nodes, pipelines, etc.) drop their refs first.
	int exit_code = 0;
	{
		sol::state lua;
		lua.open_libraries(
			sol::lib::base,
			sol::lib::string,
			sol::lib::math,
			sol::lib::table,
			sol::lib::io,
			sol::lib::os,
			sol::lib::package);

		fury::LuaBindings::Register(lua);
		lua["__window"] = &window;

		const std::string script_path = (argc > 1) ? argv[1] : "Demo.lua";
		FURYI << "Loading Lua script: " << script_path;

		sol::protected_function_result result =
			lua.safe_script_file(script_path, &sol::script_pass_on_error);
		if (!result.valid())
		{
			sol::error err = result;
			FURYE << "Lua error: " << err.what();
			exit_code = 1;
		}
	}  // sol::state destructor closes the Lua VM here

	fury::Engine::Shutdown();
	return exit_code;
}
