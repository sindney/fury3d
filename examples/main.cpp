// fury — engine launcher AND CLI.
//
// Routing in main():
//   - If argv[1] is `render-mesh`, run the headless mesh-render CLI
//     (loads a scene, renders one mesh to a PNG, exits). Needs a
//     GL context so it goes through the launcher's window path.
//     The implementation lives in Cli::RenderMesh; main() just sets
//     up the GL context + engine and delegates.
//   - If argv[1] is a known headless CLI subcommand token (`convert`,
//     `info`, `help`, `--help`, `-h`, `version`, `--version`,
//     `exec`), dispatch to fury::Cli::Run. The headless CLI skips
//     SFML window creation, engine initialization, and the Lua VM
//     for non-exec subcommands — it's pure C++ asset workflows.
//   - Otherwise, treat argv[1] (or "Editor.lua" if no arg) as a Lua
//     script path. Open a window, initialize the engine, hand control
//     to the script's Engine.run() call, then shut down cleanly.
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
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <sol/sol.hpp>

#include <Fury/AssetBackend.h>
#include <Fury/Cli.h>
#include <Fury/Editor/Editor.h>
#include <Fury/FileUtil.h>
#include <Fury/Fury.h>
#include <Fury/GLLoader.h>
#include <Fury/Gui.h>
#include <Fury/LuaBindings.h>
#include <Fury/PhysicsWorld.h>
#include <Fury/RenderThread.h>

#undef near
#undef far
#undef max

namespace
{
	// Create the window asking for the highest available core profile so
	// optional GL 4.3+ features (compute shaders) are usable when the driver
	// provides them. Attempts descend 4.6 -> 3.3; the negotiated version is
	// read back from the created context and logged. 3.3 core remains the
	// guaranteed floor, so nothing changes on old drivers (or macOS, which
	// caps at 4.1).
	bool CreateWindowNegotiated(sf::Window &window, sf::VideoMode mode,
		const char* title, std::uint32_t style)
	{
		static const int kAttempts[][2] = {{4, 6}, {4, 5}, {4, 3}, {4, 1}, {3, 3}};
		for (const auto &att : kAttempts)
		{
			sf::ContextSettings settings;
			settings.depthBits = 24;
			settings.stencilBits = 8;
			settings.antiAliasingLevel = 0;
			settings.majorVersion = att[0];
			settings.minorVersion = att[1];

			window.create(mode, title, style, sf::State::Windowed, settings);
			if (!window.isOpen())
				continue;

			const auto &actual = window.getSettings();
			if (actual.majorVersion > 3 ||
				(actual.majorVersion == 3 && actual.minorVersion >= 3))
			{
				std::cout << "fury: GL context " << actual.majorVersion << "."
					<< actual.minorVersion << " (requested " << att[0] << "." << att[1] << ")\n";
				return true;
			}
			window.close();
		}
		return false;
	}

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
			// "--screenshot-series path,N,interval": capture N frames every
			// `interval` frames (from --screenshot-frame) into one atlas PNG.
			if (std::strcmp(a, "--screenshot-series") == 0)
			{
				if (i + 1 >= argc)
				{
					std::cerr << "fury: --screenshot-series requires 'path,N,interval'\n";
					return false;
				}
				std::string spec = argv[++i];
				const auto c1 = spec.rfind(',');
				const auto c2 = c1 == std::string::npos ? c1 : spec.rfind(',', c1 - 1);
				if (c1 == std::string::npos || c2 == std::string::npos)
				{
					std::cerr << "fury: --screenshot-series wants 'path,N,interval' (got '"
						<< spec << "')\n";
					return false;
				}
				try
				{
					out_options.screenshot_series_path = spec.substr(0, c2);
					out_options.series_count = std::stoi(spec.substr(c2 + 1, c1 - c2 - 1));
					out_options.series_interval = std::stoi(spec.substr(c1 + 1));
				}
				catch (const std::exception &)
				{
					std::cerr << "fury: --screenshot-series N/interval must be integers (got '"
						<< spec << "')\n";
					return false;
				}
				if (out_options.series_count < 1 || out_options.series_count > 64 ||
					out_options.series_interval < 1)
				{
					std::cerr << "fury: --screenshot-series requires 1..64 frames and interval >= 1\n";
					return false;
				}
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
			if (std::strcmp(a, "--auto-confirm") == 0)
			{
				// Auto-answer editor confirm dialogs (import unit-scale
				// prompt etc.) with the default selection — keeps headless
				// automation running without UI interaction.
				out_options.auto_confirm = true;
				continue;
			}
			if (std::strcmp(a, "--focus") == 0)
			{
				// Frame the camera on the opened scene's content after
				// load (whole-scene bounds) — keeps headless screenshots
				// consistently framed regardless of scene scale.
				out_options.auto_focus = true;
				continue;
			}
			if (std::strcmp(a, "--render-thread") == 0
				|| std::strncmp(a, "--render-thread=", 16) == 0)
			{
				// Accepts "--render-thread 0|1" and "--render-thread=0|1".
				const char *value = nullptr;
				if (a[15] == '=')
					value = a + 16;
				else if (i + 1 < argc)
					value = argv[++i];
				if (value == nullptr || (value[0] != '0' && value[0] != '1') || value[1] != '\0')
				{
					std::cerr << "fury: --render-thread requires 0 or 1\n";
					return false;
				}
				out_options.render_thread = value[0] - '0';
				continue;
			}
			out_filtered.push_back(a);
		}
		return true;
	}
}

int main(int argc, char *argv[])
{
#if defined(FURY_HEADLESS_CLI) && FURY_HEADLESS_CLI
	// `furye-cli render-mesh` needs a GL context, so set up the window + engine here
	// and delegate to Cli::RenderMesh. Headless-by-default, windowed on demand.
	if (argc >= 2 && std::strcmp(argv[1], "render-mesh") == 0) {
		sf::Window window;
		if (!CreateWindowNegotiated(window, sf::VideoMode({256, 256}),
				"Fury3d-render-mesh", sf::Style::None)) {
			std::cerr << "fury render-mesh: window creation failed\n";
			return 1;
		}
		window.setVerticalSyncEnabled(false);
		(void)window.setActive();
		int rc = 1;
		if (fury::Engine::Initialize(window, 2, fury::LogLevel::DBUG,
				fury::FileUtil::GetAbsPath("Log.txt").c_str())) {
			rc = fury::Cli::RenderMesh(argc, argv);
			fury::Engine::Shutdown();
		} else {
			std::cerr << "fury render-mesh: Engine::Initialize failed\n";
		}
		return rc;
	}
#endif


	// `fury render-thread-smoke [N]`: standalone render-thread context-handoff
	// check. Render thread clears+swaps N frames with a center-pixel readback
	// assert per frame; main thread pumps events. No engine init.
	if (argc >= 2 && std::strcmp(argv[1], "render-thread-smoke") == 0) {
		int frames = 120;
		if (argc >= 3) {
			frames = std::atoi(argv[2]);
			if (frames < 1) frames = 1;
			if (frames > 100000) frames = 100000;
		}
		sf::Window window;
		if (!CreateWindowNegotiated(window, sf::VideoMode({256, 256}),
				"Fury3d-rt-smoke", sf::Style::None)) {
			std::cerr << "fury render-thread-smoke: window creation failed\n";
			return 1;
		}
		window.setVerticalSyncEnabled(false);
		(void)window.setActive();
		if (gl::LoadGLFunctions() != 1) {
			std::cerr << "fury render-thread-smoke: GL function load failed\n";
			return 1;
		}
		return fury::RenderThread::RunSmokeTest(window, frames);
	}

	// Fast-path: if argv[1] looks like a CLI subcommand, take the offline
	// path. No window, no engine, no Lua. Exit code from Cli::Run propagates.
	if (argc >= 2 && fury::Cli::LooksLikeSubcommand(argv[1]))
		return fury::Cli::Run(argc, argv);

#if defined(FURY_HEADLESS_CLI) && FURY_HEADLESS_CLI
	// furye-cli never falls through to the Lua launcher: unknown input is a
	// user error, no-args prints help.
	if (argc < 2)
	{
		char arg0[] = "furye-cli", arg1[] = "--help";
		char* help_argv[] = { arg0, arg1 };
		return fury::Cli::Run(2, help_argv);
	}
	std::cerr << "furye-cli: unknown subcommand '" << argv[1] << "' (see --help)\n";
	return 1;
#endif

	// Pak flows (fury only; the editor works on loose files):
	//   fury game.pak                 -> mount, play the pak's boot scene
	//   fury scene.bin / fury Player.lua scene.bin with sibling scene.pak
	//                                -> mount, asset reads resolve from pak
	std::string pak_boot_key;
#if !defined(FURY_HEADLESS_CLI) && !WITH_EDITOR
	if (argc >= 2)
	{
		// Silent probes only: this block runs before Engine::Initialize, so
		// the Log singleton does not exist yet and FileUtil::FileExist would
		// crash on its error log for missing files.
		auto file_exists_quiet = [](const std::string& p) {
			std::ifstream f(p);
			return f.good();
		};
		auto ends_icase = [](const std::string& s, const char* suf) {
			const size_t n = std::strlen(suf);
			if (s.size() < n) return false;
			for (size_t i = 0; i < n; ++i)
				if (std::tolower((unsigned char)s[s.size() - n + i]) != suf[i]) return false;
			return true;
		};

		std::string arg1 = argv[1];
		if (ends_icase(arg1, ".pak") && file_exists_quiet(arg1))
		{
			std::string err;
			if (!fury::AssetBackend::MountPak(arg1, err))
			{
				std::cerr << "fury: failed to mount " << arg1 << ": " << err << "\n";
				return 1;
			}
			pak_boot_key = fury::AssetBackend::MountedBootEntry();
			if (pak_boot_key.empty())
			{
				std::cerr << "fury: " << arg1 << " has no boot entry\n";
				return 1;
			}
		}
		else
		{
			for (int i = 1; i < argc && i <= 2; ++i)
			{
				std::string a = argv[i];
				size_t stem = 0;
				if (ends_icase(a, ".json")) stem = 5;
				else if (ends_icase(a, ".bin")) stem = 4;
				else continue;
				if (!file_exists_quiet(a)) continue;
				std::string sibling = a.substr(0, a.size() - stem) + ".pak";
				if (!file_exists_quiet(sibling)) continue;
				std::string err;
				if (fury::AssetBackend::MountPak(sibling, err))
					std::cout << "fury: mounted sibling pak " << sibling << "\n";
				else
					std::cerr << "fury: sibling pak " << sibling << " failed to mount: " << err << "\n";
				break;
			}
		}
	}
#endif

	// Strip launcher-only flags (--screenshot, --screenshot-frame) before
	// the rest of argv flows on to the Lua `arg` table.
	fury::LuaBindings::LauncherEngineOptions launcher_options;
	std::vector<std::string> filtered_args;
	if (!ParseLauncherFlags(argc, argv, launcher_options, filtered_args))
		return 1;

	// Otherwise: existing Lua launcher behavior.
	sf::Vector2u window_size{1280, 720};
	sf::Vector2i window_pos{-1, -1};
	int pw = 0, ph = 0, px2 = 0, py2 = 0;
	if (fury::Editor::GetPersistedWindowSize(pw, ph, px2, py2))
	{
		window_size.x = static_cast<unsigned int>(pw);
		window_size.y = static_cast<unsigned int>(ph);
		window_pos.x = px2;
		window_pos.y = py2;
	}

	sf::Window window;
	if (!CreateWindowNegotiated(window, sf::VideoMode(window_size),
			"Fury3d", sf::Style::Titlebar | sf::Style::Resize | sf::Style::Close))
	{
		std::cerr << "fury: window creation failed" << std::endl;
		return 1;
	}
	if (window_pos.x >= 0 && window_pos.y >= 0)
		window.setPosition(window_pos);
	window.setKeyRepeatEnabled(true);
	window.setVerticalSyncEnabled(false);
	(void)window.setActive();

	if (!fury::Engine::Initialize(window, 2, fury::LogLevel::DBUG,
		fury::FileUtil::GetAbsPath("Log.txt").c_str()))
	{
		std::cerr << "Engine::Initialize failed" << std::endl;
		return 1;
	}

	// Play-mode gate: the plain runtime simulates physics; the editor never
	// does (it launches a fury child process for play sessions instead).
#if !WITH_EDITOR
	fury::PhysicsWorld::Instance()->SetSimulationEnabled(true);
#endif

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

		const std::string script_path_default = (argc > 1) ? argv[1] : "Editor.lua";
		// Pak boot: run Player.lua against the pak's boot scene. The script is
		// read through the asset backend below, so a packed Player.lua wins
		// over the loose one when the pak carries it.
		if (!pak_boot_key.empty())
		{
			filtered_args.clear();
			filtered_args.push_back(pak_boot_key);
		}
		const std::string script_path = pak_boot_key.empty() ? script_path_default : "Player.lua";

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

		// With a pak mounted the launcher script may live inside it; read
		// through the backend (falls back to disk transparently).
		std::string script_source;
		const bool read_via_backend = fury::AssetBackend::HasMountedPak();
		bool script_found = true;
		if (read_via_backend)
			script_found = fury::FileUtil::LoadString(script_path, script_source);

		if (!script_found)
		{
			FURYE << "Lua script not found (pak or disk): " << script_path;
			exit_code = 1;
		}
		else
		{
		sol::protected_function_result result = read_via_backend
			? lua.safe_script(script_source, &sol::script_pass_on_error)
			: lua.safe_script_file(script_path, &sol::script_pass_on_error);
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
		}

		fury::LuaBindings::SetLauncherOptions(nullptr);

		// Tear down the editor's Lua-callback statics (g_SceneIO,
		// g_CommandHandler, g_FrameSelectionHandler, g_TreeProvider,
		// g_CameraControls) BEFORE `sol::state` is destroyed. They hold
		// std::function closures that captured sol::protected_function
		// values by copy; those sol2 references own Lua registry slots
		// that must be released while the lua_State is still alive.
		// sol::protected_function's destructor calls
		// luaL_unref(L_, LUA_REGISTRYINDEX, ref) on its cached
		// lua_State*, and after `sol::state`'s dtor runs lua_close that
		// pointer is dangling (freed memory, not nullptr), so the
		// null-check in basic_reference's dtor is bypassed and the
		// unref reads freed global_State — segfault in luaH_getint.
		// Engine::Shutdown calls Editor::Shutdown again later, but by
		// then the fields are already empty so it's a cheap no-op.
		fury::Editor::Shutdown();
	}

	fury::Engine::Shutdown();
	return exit_code;
}
