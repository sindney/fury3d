// fury — engine launcher AND CLI.
//
// Routing in main():
//   - If argv[1] is a known CLI subcommand token (`convert`, `info`, `help`,
//     `--help`, `-h`, `version`, `--version`), dispatch to fury::Cli::Run.
//     The CLI path skips SFML window creation, engine initialization, and the
//     Lua VM — it's pure C++ asset workflows.
//   - Otherwise, treat argv[1] (or "Editor.lua" if no arg) as a Lua script
//     path. Open a window, initialize the engine, hand control to the
//     script's Engine.run() call, then shut down cleanly.
//
// Runtime flags (not subcommands) that the launcher path recognizes:
//   --screenshot <path>         capture a PNG of the rendered scene
//   --screenshot-frame <N>      configure the frame index for capture
// These are stripped from argv before the Lua `arg` table is built, so user
// scripts never see them.
//
// See docs/CLI.md for the CLI surface; docs/LUA.md for the Lua surface.

#include <SFML/Window.hpp>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <sol/sol.hpp>

#include <Fury/Cli.h>
#include <Fury/Fury.h>
#include <Fury/Gui.h>
#include <Fury/LuaBindings.h>

#undef near
#undef far
#undef max

namespace
{
	// Walk argv[2..argc-1] and split out the launcher's runtime flags. The
	// flags are written into `out_options`; everything else is appended to
	// `out_filtered` in original order. Returns true on success; false (with
	// a message on stderr) if an option is malformed (missing value or out
	// of range), in which case main() returns 1.
	bool ParseLauncherFlags(int argc, char **argv,
		fury::LuaBindings::LauncherEngineOptions &out_options,
		std::vector<std::string> &out_filtered)
	{
		for (int i = 2; i < argc; ++i)
		{
			const char *a = argv[i];
			if (std::strcmp(a, "--screenshot") == 0)
			{
				if (i + 1 >= argc)
				{
					std::cerr << "fury: --screenshot requires a path argument\n";
					return false;
				}
				out_options.screenshot_path = argv[++i];
				continue;
			}
			if (std::strcmp(a, "--screenshot-frame") == 0)
			{
				if (i + 1 >= argc)
				{
					std::cerr << "fury: --screenshot-frame requires an integer argument\n";
					return false;
				}
				const char *value = argv[++i];
				try
				{
					int n = std::stoi(value);
					if (n <= 0)
					{
						std::cerr << "fury: --screenshot-frame must be a positive integer (got "
							<< value << ")\n";
						return false;
					}
					if (n > 1000)
					{
						std::cerr << "fury: --screenshot-frame clamped to 1000 (was " << n << ")\n";
						n = 1000;
					}
					out_options.screenshot_frame = n;
				}
				catch (const std::exception &)
				{
					std::cerr << "fury: --screenshot-frame must be an integer (got "
						<< value << ")\n";
					return false;
				}
				continue;
			}
			out_filtered.push_back(a);
		}
		return true;
	}
}

int main(int argc, char *argv[])
{
	// Fast-path: if argv[1] looks like a CLI subcommand, take the offline
	// path. No window, no engine, no Lua. Exit code from Cli::Run propagates.
	if (argc >= 2 && fury::Cli::LooksLikeSubcommand(argv[1]))
		return fury::Cli::Run(argc, argv);

	// Strip launcher-only flags (--screenshot, --screenshot-frame) before
	// the rest of argv flows on to the Lua `arg` table.
	fury::LuaBindings::LauncherEngineOptions launcher_options;
	std::vector<std::string> filtered_args;
	if (!ParseLauncherFlags(argc, argv, launcher_options, filtered_args))
		return 1;

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
		sf::Style::Titlebar | sf::Style::Resize | sf::Style::Close,
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
		fury::LuaBindings::SetLauncherOptions(&launcher_options);
		lua["__window"] = &window;

		const std::string script_path = (argc > 1) ? argv[1] : "Editor.lua";

		// Forward extra command-line arguments to the script via the standard
		// Lua `arg` table (matches the convention of the `lua` interpreter):
		//   arg[0]    = script path
		//   arg[1..N] = filtered_args (launcher flags removed)
		// Editor.lua reads arg[1] as an optional startup-scene override.
		sol::table arg_tbl = lua.create_named_table("arg");
		arg_tbl[0] = script_path;
		for (size_t i = 0; i < filtered_args.size(); ++i)
			arg_tbl[static_cast<int>(i) + 1] = filtered_args[i];

		FURYI << "Loading Lua script: " << script_path;

		sol::protected_function_result result =
			lua.safe_script_file(script_path, &sol::script_pass_on_error);
		if (!result.valid())
		{
			sol::error err = result;
			FURYE << "Lua error: " << err.what();
			exit_code = 1;
		}

		// If the engine reported a screenshot exit code, prefer it over
		// whatever the script's run-status produced.
		if (launcher_options.exit_code != -1)
			exit_code = launcher_options.exit_code;

		fury::LuaBindings::SetLauncherOptions(nullptr);
	}

	fury::Engine::Shutdown();
	return exit_code;
}
