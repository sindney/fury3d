#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>

#include <Fury.h>

#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/FuncMessageFormatter.h>

#include "LoadFbxFile.h"

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
	sf::RenderWindow window(
		sf::VideoMode(1280, 720), 
		"Fury3d", 
		sf::Style::Titlebar | sf::Style::Close, 
		sf::ContextSettings(24, 8, 0, 3, 3)
	);
	window.setKeyRepeatEnabled(true);
	window.setFramerateLimit(60);

	// setup plog
	plog::init(plog::warning, FileUtil::Instance()->GetAbsPath("Log.txt").c_str());
	Engine::InitPlog(plog::warning, new plog::ConsoleAppender<plog::FuncMessageFormatter>());

	if (!Engine::InitGL())
		return false;

	// if (argc < 2) Pause();

	FrameWork::Ptr example = std::make_shared<LoadFbxFile>();
	example->Init(window);

	// Game Loop
	sf::Clock clock;
	sf::Event event;
	sf::Int32 next_game_tick = clock.getElapsedTime().asMilliseconds();

	while (window.isOpen() && example->running)
	{
		// Update game logic TICKS_PER_SECOND times per second.
		int numLoops = 0;
		while (clock.getElapsedTime().asMilliseconds() > next_game_tick && numLoops < MAX_FRAMESKIP && example->running)
		{
			example->PreFixedUpdate();

			while (window.pollEvent(event))
			{
				example->HandleEvent(event);
			}

			example->FixedUpdate();
			example->PostFixedUpdate();

			next_game_tick += SKIP_TICKS;
			numLoops++;
		}
		// display game object in maximum framerate.
		float dt = float(clock.getElapsedTime().asMilliseconds() + SKIP_TICKS - next_game_tick) / float(SKIP_TICKS);

		example->Update(dt);

		window.setActive();

		example->Draw(window);

		window.display();
		window.setActive(false);
	}

	return EXIT_SUCCESS;
}