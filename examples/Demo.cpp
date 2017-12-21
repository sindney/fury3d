#include <SFML/Window.hpp>

#include <Fury/Fury.h>
#include <Fury/Gui.h>

using namespace std;
using namespace fury;

#undef near
#undef far
#undef max

#define TICKS_PER_SECOND 25
#define SKIP_TICKS 1000 / TICKS_PER_SECOND
#define MAX_FRAMESKIP 5

OcTree::Ptr m_OcTree;

SceneNode::Ptr m_CamNode;

void Pause();

void Initialize();

void Update(float dt);

void FixedUpdate();

void Shutdown();

int main(int argc, char *argv[])
{
	// setup sfml
	sf::Window window(
		sf::VideoMode(1920, 1080),
		"Fury3d",
		sf::Style::Titlebar | sf::Style::Close /*| sf::Style::Fullscreen*/, 
		sf::ContextSettings(24, 8, 0, 3, 3)
		);
	window.setKeyRepeatEnabled(true);
	window.setVerticalSyncEnabled(false);
	window.setActive();
	//window.setFramerateLimit(60);

	//if (argc < 2) Pause();

	if (!Engine::Initialize(window, 2, 2, LogLevel::DBUG, FileUtil::GetAbsPath("Log.txt").c_str()))
		return false;

	Initialize();

	// Game Loop
	sf::Clock clock;
	sf::Event event;
	sf::Int32 next_game_tick = clock.getElapsedTime().asMilliseconds();
	bool running = true;

	while (window.isOpen() && running)
	{
		RenderUtil::Instance()->BeginFrame();

		// Sync event
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
			{
				running = false;
				break;
			}
			Engine::HandleEvent(event);
		}

		// Update game logic TICKS_PER_SECOND times per second.
		int numLoops = 0;
		while (clock.getElapsedTime().asMilliseconds() > next_game_tick && numLoops < MAX_FRAMESKIP && running)
		{
			FixedUpdate();
			next_game_tick += SKIP_TICKS;
			numLoops++;
		}
		// display game object in maximum framerate.
		sf::Int32 elapsed = clock.getElapsedTime().asMilliseconds();
		float dt = float(elapsed + SKIP_TICKS - next_game_tick) / float(SKIP_TICKS);
		next_game_tick -= elapsed;

		Gui::NewFrame(clock.restart().asSeconds());
		Update(dt);

		window.display();

		RenderUtil::Instance()->EndFrame();
	}

	Shutdown();
	return EXIT_SUCCESS;
}

void Pause()
{
	std::cout << "Press ENTER to continue...";
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

void Initialize()
{
	m_OcTree = OcTree::Create(Vector4(-1000, -1000, -1000, 1), Vector4(1000, 1000, 1000, 1), 2);
	Scene::Active = Scene::Create("main", FileUtil::GetAbsPath(), m_OcTree);
	FileUtil::LoadCompressedFile(Scene::Active, FileUtil::GetAbsPath("Resource/Scene/scene.bin"));

	auto camera = Camera::Create();
	camera->PerspectiveFov(0.7854f, 1.778f, 1, 100);
	camera->SetShadowFar(30);
	camera->SetShadowBounds(Vector4(-5), Vector4(5));

	m_CamNode = SceneNode::Create("camNode");
	m_CamNode->SetLocalPosition(Vector4(0.0f, 10.0f, 25.0f, 1.0f));
	m_CamNode->SetLocalRoattion(MathUtil::EulerRadToQuat(0.0f, -MathUtil::DegToRad * 30.0f, 0.0f));
	m_CamNode->Recompose();
	m_CamNode->AddComponent(Transform::Create());
	m_CamNode->AddComponent(camera);
	m_CamNode->Recompose(true);

	// setup pipeline
	Pipeline::Active = PrelightPipeline::Create("pipeline");
	Pipeline::Active->SetCurrentCamera(m_CamNode);
	FileUtil::LoadFile(Pipeline::Active, FileUtil::GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"));
}

void Update(float dt)
{
	Engine::Update(dt);
	Gui::ShowDefault(dt);
	Gui::Render();
	Pipeline::Active->Execute(m_OcTree);
}

void FixedUpdate()
{
	Engine::FixedUpdate();
}

void Shutdown()
{
	m_OcTree = nullptr;
	m_CamNode = nullptr;
	Engine::Shutdown();
}