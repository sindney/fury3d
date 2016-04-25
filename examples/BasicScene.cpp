#include <Imgui/imgui.h>

#include "BasicScene.h"

BasicScene::BasicScene()
{

}

BasicScene::~BasicScene()
{
	m_OcTree->Clear();
	m_RootNode->RemoveAllChilds();
}

void BasicScene::Init(sf::Window &window)
{

}

void BasicScene::PreFixedUpdate()
{
	m_CamNode->GetComponent<Transform>()->SyncTransforms();
}

void BasicScene::FixedUpdate()
{
	auto &inputMgr = InputUtil::Instance();

	if (inputMgr->GetKeyDown(sf::Keyboard::Escape))
		running = false;

	if (inputMgr->GetKeyDown(sf::Keyboard::A))
		m_CamPos.x = -m_CamSpeed;
	else if (inputMgr->GetKeyDown(sf::Keyboard::D))
		m_CamPos.x = m_CamSpeed;
	else
		m_CamPos.x = 0;

	if (inputMgr->GetKeyDown(sf::Keyboard::W))
		m_CamPos.z = -m_CamSpeed;
	else if (inputMgr->GetKeyDown(sf::Keyboard::S))
		m_CamPos.z = m_CamSpeed;
	else
		m_CamPos.z = 0;

	auto mousePos = inputMgr->GetMousePosition();
	if (inputMgr->GetMouseDown(sf::Mouse::Middle) || inputMgr->GetMouseDown(sf::Mouse::Right))
	{
		float tx = (m_OldMouseX - mousePos.first) * m_MouseSensitivity;
		float ty = (m_OldMouseY - mousePos.second) * m_MouseSensitivity;

		Transform::Ptr camTrans = m_CamNode->GetComponent<Transform>();
		Vector4 euler = Angle::QuatToEulerRad(camTrans->GetPostRotation());
		euler.x += tx;
		euler.y += ty;

		static const float DEGREE_90 = 3.1416f / 2;

		if (euler.y >= DEGREE_90)
			euler.y = DEGREE_90;
		else if (euler.y <= -DEGREE_90)
			euler.y = -DEGREE_90;

		camTrans->SetPostRotation(Angle::EulerRadToQuat(euler));
	}
	m_OldMouseX = mousePos.first;
	m_OldMouseY = mousePos.second;
}

void BasicScene::PostFixedUpdate()
{
	m_CamNode->GetComponent<Transform>()->SetPostPosition(m_CamNode->GetWorldMatrix().Multiply(m_CamPos));
}

void BasicScene::Update(float dt)
{
	m_CamNode->GetComponent<Transform>()->SetDeltaTime(dt);
}

void BasicScene::UpdateGUI(float dt)
{
	static bool showProfilerWindow = true, showBufferWindow = true;

	ImGui::Begin("Profiler", &showProfilerWindow, ImVec2(240, 150), 1.0f, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_ShowBorders | ImGuiWindowFlags_NoResize);

	ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

	// fps graph
	{
		static std::vector<float> values(30);
		static int values_offset = 0;
		static float refresh_time = ImGui::GetTime();
		static float upper_bound = 100.0f;

		float curFps = ImGui::GetIO().Framerate;
		while (curFps > upper_bound) upper_bound += 50;
		while (upper_bound - 50 > curFps) upper_bound -= 50;

		// Create dummy data at fixed 30 hz rate
		for (; ImGui::GetTime() > refresh_time + 1.0f / 30.0f; refresh_time += 1.0f / 30.0f)
		{
			values[values_offset] = curFps;
			values_offset = (values_offset + 1) % values.size();
		}

		std::string hint = "0 - " + std::to_string((int)upper_bound);
		ImGui::PlotLines("", &values[0], values.size(), values_offset, hint.c_str(), 1, upper_bound, ImVec2(235, 40));
	}

	ImGui::Separator();

	ImGui::Text("DrawCall: %i", RenderUtil::Instance()->GetDrawCall());
	ImGui::Text("Triangles: %i", RenderUtil::Instance()->GetTriangleCount());
	ImGui::Text("Mesh: %i", RenderUtil::Instance()->GetMeshCount());
	ImGui::Text("SkinnedMesh: %i", RenderUtil::Instance()->GetSkinnedMeshCount());
	ImGui::Text("Light: %i", RenderUtil::Instance()->GetLightCount());

	// switches
	{
		static bool draw_light_bounds = false, draw_mesh_bounds = false;

		ImGui::Separator();

		ImGui::Checkbox("Draw Light Bounds", &draw_light_bounds);
		ImGui::Checkbox("Draw Mesh Bounds", &draw_mesh_bounds);
		m_Pipeline->SetDebugParams(draw_mesh_bounds, draw_light_bounds);

		ImGui::Checkbox("Show Buffer Window", &showBufferWindow);
	}

	ImGui::End();

	if (showBufferWindow)
	{
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 300, 0), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("Buffers", &showBufferWindow, ImVec2(300, 300), 1.0f,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_ShowBorders);

		ImVec2 imgSize(ImGui::GetIO().DisplaySize.x / 4, ImGui::GetIO().DisplaySize.y / 4);

		ImGui::Text("Normal buffer: ");
		if (auto ptr = m_Pipeline->GetTextureByName("gbuffer_normal"))
			ImGui::Image((ImTextureID)ptr->GetID(), imgSize, ImVec2(0, 1), ImVec2(1, 0));

		ImGui::Text("Light buffer: ");
		if (auto ptr = m_Pipeline->GetTextureByName("gbuffer_light"))
			ImGui::Image((ImTextureID)ptr->GetID(), imgSize, ImVec2(0, 1), ImVec2(1, 0));

		ImGui::End();
	}

	/*static bool m_ShowProfilerWindow = true;
	ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiSetCond_FirstUseEver);
	ImGui::ShowTestWindow(&m_ShowProfilerWindow);*/
}

void BasicScene::Draw(sf::Window &window)
{

}
