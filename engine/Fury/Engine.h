#ifndef _FURY_ENGINE_H_
#define _FURY_ENGINE_H_

#include <iostream>
#include <functional>
#include <string>

#include <SFML/Window/Window.hpp>
#include <SFML/Window/Event.hpp>

#include "Fury/Log.h"
#include "Fury/Signal.h"

namespace fury
{
	struct EngineCallbacks
	{
		std::function<void()> OnInit;
		std::function<void(float)> OnUpdate;
		std::function<void()> OnFixedUpdate;
		std::function<void()> OnShutdown;
	};

	// Runtime options for Engine::Run. Defaults are deliberately conservative
	// -- see openspec changes/fix-demo-fps-profiler-retina for rationale.
	struct EngineOptions
	{
		// Frame-rate cap applied via sf::Window::setFramerateLimit. 0 disables.
		int max_fps = 144;
		// ImGuiStyle::ScaleAllSizes multiplier. 0.0 = use system DPI.
		float gui_scale = 0.0f;
		// ImGuiStyle::FontScaleMain. 0.0 = follow the resolved gui_scale.
		float gui_font_scale = 0.0f;

		// When true, the system DPI is multiplied with `gui_scale`.
		bool dpi_aware_override = false;

		// Screenshot capture (debug). When `screenshot_path` is non-empty,
		// the engine reads the back-buffer after frame `screenshot_frame`,
		// writes a PNG to disk, and stops the main loop. `exit_code_out`
		// receives 0 on success, 1 on capture failure (left untouched if
		// no capture was requested). The launcher passes &local_int.
		std::string screenshot_path;
		int screenshot_frame = 2;
		// --screenshot-series: capture series_count frames every
		// series_interval frames (from screenshot_frame) into one atlas PNG
		std::string screenshot_series_path;
		int series_count = 0;
		int series_interval = 0;
		int *exit_code_out = nullptr;
	};

	class FURY_API Engine
	{
	public:

		static bool Initialize(sf::Window &window, int numThreads,
			LogLevel level = LogLevel::EROR, const char* logfile = nullptr,
			bool console = true, const LogFormatter &formatter = Formatter::Simple, bool append = false);

		static void HandleEvent(sf::Event &event, sf::Window &window);

		static Signal<float>::Ptr OnUpdate;

		static Signal<>::Ptr OnFixedUpdate;

		static void Update(float dt);

		static void FixedUpdate();

		static void Shutdown();

		static void Run(sf::Window &window, const EngineCallbacks &cb);

		static void Run(sf::Window &window, const EngineCallbacks &cb, const EngineOptions &opts);

		static std::pair<int, int> GetGLVersion();

		// System DPI as a multiplier (1.0 = 96 DPI, 2.0 = 192 DPI).
		static float GetSystemDPI();

		// Fixed-tick alpha for render interpolation: how far we are between
		// the last OnFixedUpdate and the next, in [0, 1). Set each frame in
		// Run() before OnUpdate fires.
		static float GetFixedTickAlpha();

		// Fixed step duration in seconds (1/25). The Animator's physics
		// path advances clip time by this on each OnFixedUpdate.
		static float GetFixedDt();

		// Accumulated wall-clock seconds (sum of Update dt). Drives the
		// WIND shader variant's u_time. SetTime is the headless/test hook
		// (fixed-frame renders need a deterministic clock).
		static float GetTime();

		static void SetTime(float seconds);

		// Tracy profiler runtime switch (no-op when Tracy is not compiled
		// in). false disarms all zones for the run; true re-arms. Also
		// driven at startup by FURY_TRACY=0 and the editor's Tracy=0|1
		// setting. Safe to toggle at any time (atomic flag, no Tracy
		// lifetime calls).
		static void SetTracyEnabled(bool value);
	};
}

#endif // _FURY_ENGINE_H_