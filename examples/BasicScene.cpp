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
	auto &inputMgr = InputManager::Instance();

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
	ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiSetCond_FirstUseEver);
	ImGui::Begin("Profiler", &m_ShowProfilerWindow, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | 
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_ShowBorders | ImGuiWindowFlags_NoResize);

	ImGui::Text("FrameTime: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
	ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

	ImGui::End();

	/*ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiSetCond_FirstUseEver);
	ImGui::ShowTestWindow(&m_ShowProfilerWindow);*/
}

void BasicScene::Draw(sf::Window &window)
{

}