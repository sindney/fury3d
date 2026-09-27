#ifndef _FURY_GAME_UI_H_
#define _FURY_GAME_UI_H_

#include <memory>
#include <string>

#include <SFML/Window/Event.hpp>

#include "Fury/Macros.h"

namespace sf { class Window; }
namespace Rml { class Context; class ElementDocument; }

namespace fury
{
	// Game UI (RmlUi) integration: one "main" context, documents loaded
	// through the engine VFS, input forwarded from Engine::HandleEvent,
	// draw data snapshotted into the frame packet (the Gui.cpp pattern).
	// Active in every binary; headless use (fury exec / furye-cli) lazily
	// creates a windowless context so layout and tree queries still work.
	namespace GameUI
	{
		// Windowed init from Engine::Run: RmlUi core + "main" context
		// sized to the window. dpRatio follows the GUI scale source.
		bool FURY_API Initialize(sf::Window *window, float dpRatio);

		// Lazy path for headless use (no window, no GL). Safe to call
		// from Lua bindings and CLI handlers.
		bool FURY_API EnsureInitialized();

		void FURY_API Shutdown();

		// Frame update on the game thread (layout, animations).
		void FURY_API Update(float dt);

		// SFML event forwarding (mouse/keys/text + window resize).
		void FURY_API HandleEvent(sf::Event &event);

		Rml::Context *FURY_API GetContext();

		// Load a document by content path; the RmlUi FileInterface
		// resolves it through Scene::ResolveAsset + AssetBackend.
		// nullptr on failure (logged, never fatal).
		Rml::ElementDocument *FURY_API LoadDocument(const std::string &path);

		// Path-addressed document lifecycle (the load path is the id).
		// All return false when no document with that path is loaded.
		Rml::ElementDocument *FURY_API GetDocument(const std::string &path);
		bool FURY_API ShowDocument(const std::string &path);
		bool FURY_API HideDocument(const std::string &path);
		bool FURY_API ToggleDocument(const std::string &path);
		bool FURY_API CloseDocument(const std::string &path);

		bool FURY_API WantCaptureMouse();

		bool FURY_API WantCaptureKeyboard();

		// RmlUi visual debugger (element inspector). First true call
		// initialises the debugger on the main context. Enabled at startup
		// via the FURY_UI_DEBUG env hook.
		void FURY_API SetDebuggerVisible(bool visible);

		// Render-thread split (Gui snapshot pattern): build records
		// compiled geometry + batches on the game thread, replay submits
		// them to GL inside the frame executor.
		std::shared_ptr<void> FURY_API BuildDrawSnapshot();
		void FURY_API RenderSnapshot(const std::shared_ptr<void> &frame);
	}
}

#endif // _FURY_GAME_UI_H_
