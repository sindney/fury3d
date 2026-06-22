// fury — engine launcher AND CLI.
//
// Routing in main():
//   - If argv[1] is a known CLI subcommand token (`convert`, `info`, `help`,
//     `--help`, `-h`, `version`, `--version`), dispatch to fury::Cli::Run.
//     The CLI path skips SFML window creation, engine initialization, and the
//     Lua VM — it's pure C++ asset workflows.
//   - Otherwise, treat argv[1] (or "Demo.lua" if no arg) as a Lua script
//     path. Open a window, initialize the engine, hand control to the
//     script's Engine.run() call, then shut down cleanly.
//
// See docs/CLI.md for the CLI surface; docs/LUA.md for the Lua surface.

#include <SFML/Window.hpp>

#include <cstdint>
#include <iostream>
#include <string>

#include <sol/sol.hpp>

#include <Fury/Cli.h>
#include <Fury/Fury.h>
#include <Fury/Gui.h>
#include <Fury/LuaBindings.h>

#undef near
#undef far
#undef max

int main(int argc, char *argv[])
{
	// Fast-path: if argv[1] looks like a CLI subcommand, take the offline
	// path. No window, no engine, no Lua. Exit code from Cli::Run propagates.
	if (argc >= 2 && fury::Cli::LooksLikeSubcommand(argv[1]))
		return fury::Cli::Run(argc, argv);

	// Otherwise: existing Lua launcher behavior.
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
	}

	fury::Engine::Shutdown();
	return exit_code;
}
