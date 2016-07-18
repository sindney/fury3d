#include "LoadScene.h"

LoadScene::~LoadScene()
{
	Scene::Active = nullptr;
	Pipeline::Active = nullptr;
}

void LoadScene::Init(sf::Window &window)
{
	// load scene
	FbxImportOptions importOptions;
	importOptions.ScaleFactor = 1.0f;
	importOptions.AnimCompressLevel = 0.25f;

	m_OcTree = OcTree::Create(Vector4(-1000, -1000, -1000, 1), Vector4(1000, 1000, 1000, 1), 2);
	Scene::Active = m_Scene = Scene::Create("main", m_OcTree);
	FileUtil::LoadFromFile(m_Scene, FileUtil::GetAbsPath("Resource/Scene/scene.json"));

	auto lights = { /*"Lamp.001", "Lamp.002", "Lamp.003", */"Lamp.004", "Sun", "Spot", "Fire" };
	for (auto lightName : lights)
	{
		if (auto lightNode = m_Scene->GetRootNode()->FindChildRecursively(lightName))
		{
			if (auto light = lightNode->GetComponent<Light>())
			{
				light->SetCastShadows(true);

			}
		}
	}

	// setup camera
	m_CamSpeed = 1;

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
	Pipeline::Active = m_Pipeline = PrelightPipeline::Create("pipeline");
	Pipeline::Active->GetEntityManager()->Add(m_CamNode);

	FileUtil::LoadFromFile(m_Pipeline, FileUtil::GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"));

	m_Pipeline->AddDebugCollidable(m_CamNode->GetComponent<Camera>()->GetFrustum());
	m_Pipeline->AddDebugCollidable(m_CamNode->GetComponent<Camera>()->GetShadowBounds());

	//FileUtil::SaveToFile(m_Scene, "Resource/scene.json");
	//FileUtil::SaveToFile(m_Pipeline, "Resource/pipeline.json");
}

void LoadScene::FixedUpdate()
{
	BasicScene::FixedUpdate();
}

void LoadScene::Update(float dt)
{
	BasicScene::Update(dt);
}

void LoadScene::Draw(sf::Window &window)
{
	m_Pipeline->Execute(m_OcTree);
}