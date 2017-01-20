#include <SFML/Window.hpp>

#include <Fury/Fury.h>
#include <Fury/Gui.h>

#include "LoadFbxFile.h"
#include "LoadScene.h"

using namespace std;
using namespace fury;

#undef near
#undef far
#undef max

#define TICKS_PER_SECOND 25
#define SKIP_TICKS 1000 / TICKS_PER_SECOND
#define MAX_FRAMESKIP 5

void Pause()
{
	std::cout << "Press ENTER to continue...";
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

int main(int argc, char *argv[])
{
	// setup sfml
	sf::Window window(
		sf::VideoMode(1280, 720),
		"Fury3d",
		sf::Style::Titlebar | sf::Style::Close,
		//sf::Style::Fullscreen, 
		sf::ContextSettings(24, 8, 0, 3, 3)
		);
	window.setKeyRepeatEnabled(true);
	window.setVerticalSyncEnabled(false);
	window.setActive();
	//window.setFramerateLimit(60);

	//if (argc < 2) Pause();

	if (!Engine::Initialize(window, 2, LogLevel::DBUG, FileUtil::GetAbsPath("Log.txt").c_str()))
		return false;

	FrameWork::Ptr example = std::make_shared<LoadScene>();
	//FrameWork::Ptr example = std::make_shared<LoadFbxFile>();
	example->Init(window);

	// Game Loop
	sf::Clock clock;
	sf::Event event;
	sf::Int32 next_game_tick = clock.getElapsedTime().asMilliseconds();

	bool show_test_window = true;

	while (window.isOpen() && example->running)
	{
		RenderUtil::Instance()->BeginFrame();

		// Sync event
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
			{
				example->running = false;
				break;
			}

			Engine::HandleEvent(event);
			Gui::HandleEvent(event);
		}

		// Update game logic TICKS_PER_SECOND times per second.
		int numLoops = 0;
		while (clock.getElapsedTime().asMilliseconds() > next_game_tick && numLoops < MAX_FRAMESKIP && example->running)
		{
			Engine::FixedUpdate();
			example->FixedUpdate();
			next_game_tick += SKIP_TICKS;
			numLoops++;
		}
		// display game object in maximum framerate.
		sf::Int32 elapsed = clock.getElapsedTime().asMilliseconds();
		float dt = float(elapsed + SKIP_TICKS - next_game_tick) / float(SKIP_TICKS);
		next_game_tick -= elapsed;

		Gui::NewFrame(clock.restart().asSeconds());

		Engine::Update(dt);
		example->Update(dt);
		example->Draw(window);

		example->UpdateGUI(dt);
		Gui::Render();

		window.display();

		RenderUtil::Instance()->EndFrame();
	}

	example = nullptr;

	return EXIT_SUCCESS;
}