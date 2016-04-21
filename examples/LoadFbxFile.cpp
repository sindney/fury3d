#include "LoadFbxFile.h"

void LoadFbxFile::Init(sf::Window &window)
{
	m_RootNode = SceneNode::Create("RootNode");

	// load scene
	FbxImportOptions importOptions;
	importOptions.ScaleFactor = 0.01f;
	importOptions.AnimCompressLevel = 0.25f;

	FbxParser::Instance()->LoadScene(FileUtil::GetAbsPath("Resource/Scene/james.fbx"), m_RootNode, importOptions);

	EntityManager::Instance()->ForEach<AnimationClip>([&](const AnimationClip::Ptr &clip) -> bool
	{
		std::cout << "Clip: " << clip->GetName() << " Duration: " << clip->GetDuration() << std::endl;
		return true;
	});

	auto animNode = m_RootNode->FindChildRecursively("JamesNode");
	auto animWalk = EntityManager::Instance()->Get<AnimationClip>("James|Walk");
	m_AnimPlayer = AnimationPlayer::Create("AnimPlayer");
	m_AnimPlayer->AdvanceTime(animNode, animWalk, 0.0f);

	// setup camera
	m_CamSpeed = 1;

	m_CamNode = SceneNode::Create("camNode");
	m_CamNode->SetLocalPosition(Vector4(0.0f, 10.0f, 25.0f, 1.0f));
	m_CamNode->SetLocalRoattion(Angle::EulerRadToQuat(0.0f, -Angle::DegToRad * 30.0f, 0.0f));
	m_CamNode->Recompose();
	m_CamNode->AddComponent(Transform::Create());
	m_CamNode->AddComponent(Camera::Create());
	m_CamNode->GetComponent<Camera>()->PerspectiveFov(0.7854f, 1.778f, 1, 500000);
	m_CamNode->Recompose(true);
	EntityManager::Instance()->Add(m_CamNode);

	m_OcTree = OcTreeManager::Create(Vector4(-10000, -10000, -10000, 1), Vector4(10000, 10000, 10000, 1), 2);
	m_OcTree->AddSceneNodeRecursively(m_RootNode);

	// setup pipeline
	m_Pipeline = PrelightPipeline::Create("pipeline");
	FileUtil::LoadFromFile(m_Pipeline, FileUtil::GetAbsPath("Resource/Pipeline/DefferedLightingLambert.json"));
}

void LoadFbxFile::FixedUpdate()
{
	BasicScene::FixedUpdate();
	m_AnimPlayer->AdvanceTime(0.04f);
}

void LoadFbxFile::Update(float dt)
{
	BasicScene::Update(dt);
	m_AnimPlayer->Display(dt);
}

void LoadFbxFile::Draw(sf::Window &window)
{
	m_Pipeline->Execute(m_OcTree);
}