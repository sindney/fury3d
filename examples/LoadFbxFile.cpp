#include "LoadFbxFile.h"

AnimationPlayer::Ptr m_AnimPlayer = nullptr;

LoadFbxFile::~LoadFbxFile()
{
	Scene::Active = nullptr;
	Pipeline::Active = nullptr;
}

void LoadFbxFile::Init(sf::Window &window)
{
	// load scene
	FbxImportOptions importOptions;
	importOptions.ScaleFactor = 1.0f;
	importOptions.AnimCompressLevel = 0.25f;

	m_OcTree = OcTree::Create(Vector4(-1000, -1000, -1000, 1), Vector4(1000, 1000, 1000, 1), 2);
	Scene::Active = m_Scene = Scene::Create("main", m_OcTree);

	if (false)
	{
		FbxParser::Instance()->LoadScene(FileUtil::GetAbsPath("Resource/Scene/james.fbx"), m_Scene->GetRootNode(), importOptions);

		Scene::Active->GetEntityManager()->ForEach<AnimationClip>([&](const AnimationClip::Ptr &clip) -> bool
		{
			std::cout << "Clip: " << clip->GetName() << " Duration: " << clip->GetDuration() << std::endl;
			return true;
		});

		auto animWalk = Scene::Active->GetEntityManager()->Get<AnimationClip>("James|Walk");
		auto animNode = m_Scene->GetRootNode()->FindChildRecursively("JamesNode");
		m_AnimPlayer = AnimationPlayer::Create("AnimPlayer");
		m_AnimPlayer->AdvanceTime(animNode, animWalk, 0.0f);
	}
	else
	{
		FbxParser::Instance()->LoadScene(FileUtil::GetAbsPath("Resource/Scene/tank.fbx"), m_Scene->GetRootNode(), importOptions);
		Scene::Active->GetEntityManager()->Get<Mesh>("Grid")->SetCastShadows(false);
	}

	m_OcTree->AddSceneNodeRecursively(m_Scene->GetRootNode());

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

void LoadFbxFile::FixedUpdate()
{
	BasicScene::FixedUpdate();
	if (m_AnimPlayer)
		m_AnimPlayer->AdvanceTime(0.04f);
}

void LoadFbxFile::Update(float dt)
{
	BasicScene::Update(dt);
	if (m_AnimPlayer)
		m_AnimPlayer->Display(dt);
}

void LoadFbxFile::Draw(sf::Window &window)
{
	m_Pipeline->Execute(m_OcTree);
}