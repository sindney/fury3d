#include "LoadFbxFile.h"

AnimationPlayer::Ptr m_AnimPlayer = nullptr;

void LoadFbxFile::Init(sf::Window &window)
{
	m_RootNode = SceneNode::Create("RootNode");

	// load scene
	FbxImportOptions importOptions;
	importOptions.ScaleFactor = 0.01f;
	importOptions.AnimCompressLevel = 0.25f;

	if (false)
	{
		FbxParser::Instance()->LoadScene(FileUtil::GetAbsPath("Resource/Scene/james.fbx"), m_RootNode, importOptions);

		EntityUtil::Instance()->ForEach<AnimationClip>([&](const AnimationClip::Ptr &clip) -> bool
		{
			std::cout << "Clip: " << clip->GetName() << " Duration: " << clip->GetDuration() << std::endl;
			return true;
		});

		auto animWalk = EntityUtil::Instance()->Get<AnimationClip>("James|Walk");
		auto animNode = m_RootNode->FindChildRecursively("JamesNode");
		m_AnimPlayer = AnimationPlayer::Create("AnimPlayer");
		m_AnimPlayer->AdvanceTime(animNode, animWalk, 0.0f);
	}
	else
	{
		FbxParser::Instance()->LoadScene(FileUtil::GetAbsPath("Resource/Scene/tank_shadow.fbx"), m_RootNode, importOptions);

		EntityUtil::Instance()->Get<Mesh>("Grid")->SetCastShadows(false);
	}

	if (auto sunLightNode = m_RootNode->FindChildRecursively("Sun"))
	{
		if (auto light = sunLightNode->GetComponent<Light>())
		{
			light->SetCastShadows(true);
		}
	}

	// setup camera
	m_CamSpeed = 1;

	auto camera = Camera::Create();
	camera->PerspectiveFov(0.7854f, 1.778f, 1, 100);
	camera->SetShadowFar(30);

	m_CamNode = SceneNode::Create("camNode");
	m_CamNode->SetLocalPosition(Vector4(0.0f, 10.0f, 25.0f, 1.0f));
	m_CamNode->SetLocalRoattion(Angle::EulerRadToQuat(0.0f, -Angle::DegToRad * 30.0f, 0.0f));
	m_CamNode->Recompose();
	m_CamNode->AddComponent(Transform::Create());
	m_CamNode->AddComponent(camera);
	m_CamNode->Recompose(true);
	EntityUtil::Instance()->Add(m_CamNode);

	m_OcTree = OcTree::Create(Vector4(-1000, -1000, -1000, 1), Vector4(1000, 1000, 1000, 1), 2);
	m_OcTree->AddSceneNodeRecursively(m_RootNode);

	// setup pipeline
	m_Pipeline = PrelightPipeline::Create("pipeline");
	FileUtil::LoadFromFile(m_Pipeline, FileUtil::GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"));
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