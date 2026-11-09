#include "Fury/Scene.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

#include "Fury/AnimationClip.h"
#include "Fury/Archive.h"
#include "Fury/AssetLoader.h"
#include "Fury/FileUtil.h"
#include "Fury/EntityManager.h"
#include "Fury/Heightmap.h"
#include "Fury/Joint.h"
#include "Fury/ParticleSystem.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/OcTree.h"
#include "Fury/PhysicsWorld.h"
#include "Fury/PostProcessRegistry.h"
#include "Fury/RenderSettings.h"
#include "Fury/SceneNode.h"
#include "Fury/Texture.h"

namespace fury {
Scene::Ptr Scene::Active = nullptr;

template<typename T>
void SerializeSceneAssets(Archive& ar, const char* name, ArchiveFieldPolicy policy,
	const std::shared_ptr<EntityManager>& manager)
{
	if (ar.IsLoading())
	{
		if (!ar.EnterArray(name, policy)) return;
		std::set<std::string> warned;
		for (size_t i = 0; i < ar.GetArraySize(); ++i)
		{
			ar.SetArrayIndex(i);
			if (!ar.EnterObject()) continue;
			auto ptr = T::Create("temp");
			ptr->Serialize(ar);
			if (!manager->Add(ptr) && !ptr->GetPath().empty() && warned.insert(ptr->GetPath()).second)
				FURYW << "Scene::Load: duplicate asset path '" << ptr->GetPath()
					  << "' -- first registered entry wins";
			ar.LeaveObject();
		}
		ar.LeaveArray();
		return;
	}
	if (!ar.EnterArray(name, policy)) return;
	// The EntityManager keeps assets in hash-ordered maps; sort them so the
	// saved document does not depend on registration order.
	std::vector<std::shared_ptr<T>> assets;
	manager->ForEach<T>([&](const std::shared_ptr<T>& ptr) -> bool
	{
		assets.push_back(ptr);
		return true;
	});
	std::sort(assets.begin(), assets.end(),
		[](const std::shared_ptr<T>& a, const std::shared_ptr<T>& b)
		{
			const int pathCmp = a->GetPath().compare(b->GetPath());
			if (pathCmp != 0) return pathCmp < 0;
			const int nameCmp = a->GetName().compare(b->GetName());
			if (nameCmp != 0) return nameCmp < 0;
			return a->GetUUID() < b->GetUUID();
		});
	for (const auto& ptr : assets)
	{
		if (!ar.IsValid()) break;
		if (ar.EnterObject())
		{
			ptr->Serialize(ar);
			ar.LeaveObject();
		}
	}
	ar.LeaveArray();
}

void PrefetchSceneTextures(Archive& ar)
{
	if (!ar.EnterArray("textures", ArchiveFieldPolicy::Optional)) return;
	AssetLoader& loader = AssetLoader::Get();
	if (loader.IsUp())
	{
		for (size_t i = 0; i < ar.GetArraySize(); ++i)
		{
			ar.SetArrayIndex(i);
			if (!ar.EnterObject()) continue;
			std::string path, name;
			ar << Optional("path", path);
			ar << Optional("name", name);
			if (path.empty()) path = name;
			if (!path.empty()) loader.Prefetch(Scene::ResolveAsset(path));
			ar.LeaveObject();
		}
		loader.Flush();
	}
	ar.LeaveArray();
}

std::string Scene::Path(const std::string& path) {
	if (Scene::Active != nullptr)
		return Scene::Active->GetWorkingDir() + path;
	else
		return path;
}

namespace { constexpr const char* kEnginePrefix = "Engine/"; }
namespace { constexpr const char* kEngineAssetRoot = "Resource/"; }

std::string Scene::ResolveAsset(const std::string& path) {
	if (path.empty()) return path;

	// Engine asset: strip "Engine/" and resolve to the engine resource
	// root (cwd + "Resource/"). The engine resource root lives next
	// to fury/furye on a typical launch (cwd = examples/).
	if (path.compare(0, std::strlen(kEnginePrefix), kEnginePrefix) == 0) {
		const std::string rest = path.substr(std::strlen(kEnginePrefix));
		return FileUtil::GetAbsPath() + kEngineAssetRoot + rest;
	}

	// Project asset: scene's working_dir + path (the Scene::Path semantics).
	return Scene::Path(path);
}

std::shared_ptr<EntityManager> Scene::Manager() {
	ASSERT_MSG(Scene::Active != nullptr, "Scene::Active == nullptr");
	return Scene::Active->GetEntityManager();
}

Scene::Ptr Scene::Create(const std::string& name, const std::string& workingDir, const std::shared_ptr<SceneManager>& sceneManager) {
	return std::make_shared<Scene>(name, workingDir, sceneManager);
}

Scene::Scene(const std::string& name, const std::string& workingDir, const std::shared_ptr<SceneManager>& sceneManager)
	: Entity(name), m_WorkingDir(workingDir) {
	m_TypeIndex = typeid(Scene);
	if (sceneManager == nullptr)
		m_SceneManager = OcTree::Create();
	else
		m_SceneManager = sceneManager;

	m_EntityManager = EntityManager::Create();
	m_RootNode = SceneNode::Create("RootNode");

	// Defaults: LDR, CSM on, empty chain (legacy behavior).
	m_RenderSettings = std::make_shared<RenderSettings>();
}

Scene::~Scene() {
	FURYD << "Scene " << m_Name << " Destoried!";
	Clear();
}

void Scene::Clear() {
	m_EntityManager->RemoveAll();
	m_SceneManager->Clear();
	m_RootNode->RemoveAllChilds();
	m_RootNode->RemoveAllComponents();
	// Reset render settings to defaults; Load replaces this if the
	// scene carries the block.
	m_RenderSettings = std::make_shared<RenderSettings>();
}

void Scene::Serialize(Archive& ar) {
	int version = ar.IsLoading() ? 1 : kFormatVersion;
	ar << Optional("version", version);
	if (ar.IsLoading()) {
		if (version > kFormatVersion) {
			ar.SetError("scene format version " + std::to_string(version) +
				" is newer than supported " + std::to_string(kFormatVersion));
			return;
		}
		ar.SetVersion(version);
		Clear();
		m_HasPhysicsSettings = false;
		m_PhysicsGravity = Vector4(0.0f, -981.0f, 0.0f, 0.0f);
	}
	Entity::SerializeFields(ar);

	if (ar.IsLoading()) PrefetchSceneTextures(ar);
	SerializeSceneAssets<Texture>(ar, "textures", ArchiveFieldPolicy::Optional, m_EntityManager);
	SerializeSceneAssets<Material>(ar, "materials", ArchiveFieldPolicy::Required, m_EntityManager);
	SerializeSceneAssets<Mesh>(ar, "meshes", ArchiveFieldPolicy::Required, m_EntityManager);
	SerializeSceneAssets<AnimationClip>(ar, "animations", ArchiveFieldPolicy::Optional, m_EntityManager);
	SerializeSceneAssets<ParticleSystem>(ar, "particleSystems", ArchiveFieldPolicy::Optional, m_EntityManager);
	SerializeSceneAssets<Heightmap>(ar, "heightmaps", ArchiveFieldPolicy::Optional, m_EntityManager);
	if (!ar.IsValid()) return;

	if (ar.EnterObject("nodes")) {
		m_RootNode->Serialize(ar);
		ar.LeaveObject();
	}
	if (!ar.IsValid()) return;

	if (ar.EnterObject("renderSettings", ArchiveFieldPolicy::Optional)) {
		m_RenderSettings->Serialize(ar);
		ar.LeaveObject();
	}
	if (ar.EnterObject("physics", ar.IsLoading() ? ArchiveFieldPolicy::Optional : ArchiveFieldPolicy::Conditional, m_HasPhysicsSettings)) {
		if (ar.IsLoading()) m_HasPhysicsSettings = true;
		ar << Required("gravity", m_PhysicsGravity);
		ar.LeaveObject();
	}
	if (!ar.IsLoading()) return;

	std::unordered_map<std::string, std::shared_ptr<SceneNode>> nodesByUUID, nodesByName;
	std::function<void(const std::shared_ptr<SceneNode>&)> collectNodes =
		[&](const std::shared_ptr<SceneNode>& n) {
		if (!n) return;
		nodesByUUID.emplace(n->GetUUID(), n);
		nodesByName.emplace(n->GetName(), n);
		for (unsigned int i = 0; i < n->GetChildCount(); ++i)
			collectNodes(n->GetChildAt(i));
	};
	collectNodes(m_RootNode);

	m_EntityManager->ForEach<Mesh>([&](const Mesh::Ptr& mesh) -> bool {
		unsigned int jcount = mesh->GetJointCount();
		for (unsigned int i = 0; i < jcount; ++i) {
			auto joint = mesh->GetJointAt(i);
			if (!joint || joint->GetSceneNode()) continue;
			const auto &uuid = joint->GetSceneNodeUUID();
			std::shared_ptr<SceneNode> node;
			if (!uuid.empty()) {
				auto it = nodesByUUID.find(uuid);
				if (it != nodesByUUID.end()) node = it->second;
			}
			if (!node) {
				auto it = nodesByName.find(joint->GetName());
				if (it != nodesByName.end()) node = it->second;
			}
			if (node) joint->SetSceneNode(node);
		}
		return true;
	});

	m_EntityManager->ForEach<Material>([&](const Material::Ptr& mat) -> bool {
		for (const auto& kv : mat->GetTextures())
			if (kv.second) m_EntityManager->Add(kv.second);
		return true;
	});
	m_SceneManager->AddSceneNodeRecursively(m_RootNode);

	if (PhysicsWorld::Exists())
		PhysicsWorld::Instance()->SetGravity(m_PhysicsGravity);
}



std::shared_ptr<SceneNode> Scene::GetRootNode() const {
	return m_RootNode;
}

std::shared_ptr<SceneManager> Scene::GetSceneManager() const {
	return m_SceneManager;
}

std::shared_ptr<EntityManager> Scene::GetEntityManager() const {
	return m_EntityManager;
}

std::string Scene::GetWorkingDir() const {
	return m_WorkingDir;
}

void Scene::SetWorkingDir(const std::string& path) {
	m_WorkingDir = path;
}

std::shared_ptr<RenderSettings> Scene::GetRenderSettings() const {
	return m_RenderSettings;
}

void Scene::SetPhysicsGravity(const Vector4 &gravity) {
	m_PhysicsGravity = gravity;
	m_HasPhysicsSettings = true;
	if (PhysicsWorld::Exists())
		PhysicsWorld::Instance()->SetGravity(gravity);
}
} // namespace fury