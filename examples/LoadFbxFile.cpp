#include "LoadFbxFile.h"
#include "FileUtil.h"

void LoadFbxFile::Init(sf::RenderWindow &window)
{
	m_RootNode = SceneNode::Create("RootNode");

	// load scene
	auto options = Options::UV | Options::NORMAL | Options::DIFFUSE_MAP | Options::OPTIMIZE_MESH | Options::DELETE_MESHDATA;
	FbxUtil::Instance()->LoadScene(FileUtil::Instance()->GetAbsPath("Resource/Scene/tank.fbx"), m_RootNode, 0.01f, options);

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
	EntityUtil::Instance()->AddEntity(m_CamNode);

	m_OcTree = OcTree::Create(Vector4(-10000, -10000, -10000, 1), Vector4(10000, 10000, 10000, 1), 2);
	m_OcTree->AddSceneNodeRecursively(m_RootNode);

	// setup pipeline
	m_Pipeline = PrelightPipeline::Create("pipeline");
	FileUtil::Instance()->LoadFromFile(m_Pipeline, FileUtil::Instance()->GetAbsPath("Resource/Pipeline/DefferedLighting.json"));
	//FileUtil::Instance()->SaveToFile(m_Pipeline, "Resource/Pipeline/DefferedLighting.json");
}

void LoadFbxFile::Update(float dt)
{
	BasicScene::Update(dt);
}

void LoadFbxFile::Draw(sf::RenderWindow &window)
{
	m_Pipeline->Execute(m_OcTree);
}