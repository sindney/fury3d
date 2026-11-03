#ifndef _FURY_UI_TEST_HOST_H_
#define _FURY_UI_TEST_HOST_H_

// Editor UI automation host (furye-ui-tests only). Every entry point is a
// no-op unless the target defines FURY_UI_TEST_HOST, so production binaries
// compile and link zero test-engine code.

#if defined(FURY_UI_TEST_HOST) && WITH_EDITOR

#include <string>

namespace fury
{
	namespace UiTestHost
	{
		// Consumes --ui-test* launcher flags from argv (ParseLauncherFlags
		// calls this per unrecognized argument), advancing `i` past consumed
		// values. Returns false for unrelated arguments.
		bool ConsumeLauncherArg(int argc, char **argv, int &i);

		// True when --ui-test was passed: the process runs unattended.
		bool Active();

		// Redirected imgui.ini path (artifact dir) for the editor's early
		// manual readers. Valid once Active() is true.
		std::string IniOverridePath();

		// Fixed 1280x720 fixture window; applied after the persisted size.
		void ApplyWindowConstraints(unsigned int &w, unsigned int &h);

		// Forces gui/gui-font scale to 1 for deterministic layouts.
		float ForceGuiScale(float scale);

		// Lifecycle: after Gui::Initialize, before Editor::Initialize.
		void Initialize();

		// Per-frame pump on the game thread: runs after editor/Lua UI
		// submission, before the draw snapshot is cloned; creates the fixture
		// scene on first call, watches completion and timeout.
		void Pump();

		// Hooks around framebuffer presentation. PostSwap drives the test
		// engine's capture pipeline; both are no-ops when the host is off.
		void PreSwap();
		void PostSwap();

		// Stops the queue and exports results while the ImGui context is still
		// alive (the coroutine asserts on it). Called before Gui::Shutdown.
		void Shutdown();

		// Destroys the engine after ImGui::DestroyContext (documented
		// test-engine order, keeps its ini data saveable).
		void DestroyEngine();

		// Process exit code: 0 success, 1 failed test, 2 empty selection,
		// 3 timeout. Meaningful only after a run (Ran()).
		bool Ran();
		int ExitCode();

		// ----- test-only services -----------------------------------------
		// Artifact/output directory for this run (JUnit, captures, ini).
		const std::string &ArtifactDir();

		// Queued native-dialog results, consumed FIFO by the Editor.OpenDialog
		// / Editor.SaveDialog Lua bindings (no OS dialogs in unattended runs).
		void QueueOpenDialogResult(const std::string &path);
		void QueueSaveDialogResult(const std::string &path);
		bool ConsumeOpenDialogResult(std::string &out);
		bool ConsumeSaveDialogResult(std::string &out);
	}
}

#endif // FURY_UI_TEST_HOST && WITH_EDITOR

#endif // _FURY_UI_TEST_HOST_H_
