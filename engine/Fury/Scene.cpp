#include "Fury/Scene.h"

#include <functional>
#include <unordered_map>

#include "Fury/AnimationClip.h"
#include "Fury/EntityManager.h"
#include "Fury/Joint.h"
#include "Fury/ParticleSystem.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/OcTree.h"
#include "Fury/PostProcessRegistry.h"
#include "Fury/RenderSettings.h"
#include "Fury/SceneNode.h"
#include "Fury/Texture.h"

namespace fury {
Scene::Ptr Scene::Active = nullptr;

std::string Scene::Path(const std::string& path) {
	if (Scene::Active != nullptr)
		return Scene::Active->GetWorkingDir() + path;
	else
		return path;
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

bool Scene::Load(const void* wrapper, bool object) {
	Clear();

	if (object && !IsObject(wrapper)) {
		FURYE << "Json node is not an object!";
		return false;
	}

	if (!Entity::Load(wrapper, false))
		return false;

	// Format version gate. Absent = version 1 (pre-versioning files).
	// A newer file is rejected -- silently loading it would drop or
	// misread fields the author depends on.
	int version = 1;
	LoadMemberValue(wrapper, "version", version);
	if (version > kFormatVersion) {
		FURYE << "Scene format version " << version << " is newer than supported ("
			  << kFormatVersion << "); update the engine to load this scene";
		return false;
	}

	// load textures (top-level array, if present -- new format)
	if (auto texWrapper = FindMember(wrapper, "textures")) {
		LoadArray(texWrapper, [&](const void* node) -> bool {
			auto texture = Texture::Create("temp");
			if (!texture->Load(node))
				return false;
			m_EntityManager->Add(texture);
			return true;
		});
	}

	// load materials
	if (!LoadArray(wrapper, "materials", [&](const void* node) -> bool {
			auto material = Material::Create("temp");
			if (!material->Load(node))
				return false;

			m_EntityManager->Add(material);
			return true;
		})) {
		FURYE << "Error serializing materials!";
		return false;
	}

	// load meshes
	if (!LoadArray(wrapper, "meshes", [&](const void* node) -> bool {
			auto mesh = Mesh::Create("temp");
			if (!mesh->Load(node))
				return false;

			m_EntityManager->Add(mesh);
			return true;
		})) {
		FURYE << "Error serializing meshes!";
		return false;
	}

	// load animation clips (top-level array, if present)
	if (auto clipsWrapper = FindMember(wrapper, "animations")) {
		LoadArray(clipsWrapper, [&](const void* node) -> bool {
			auto clip = AnimationClip::Create("temp");
			if (!clip->Load(node))
				return false;
			m_EntityManager->Add(clip);
			return true;
		});
	}

	// load particle systems (top-level array, if present) -- assets
	// referenced by name from ParticleRenderer components. See the
	// particle-system spec.
	if (auto psWrapper = FindMember(wrapper, "particleSystems")) {
		LoadArray(psWrapper, [&](const void* node) -> bool {
			auto ps = ParticleSystem::Create("temp");
			if (!ps->Load(node))
				return false;
			m_EntityManager->Add(ps);
			return true;
		});
	}

	// load nodes
	if (auto rootNodeWrapper = FindMember(wrapper, "nodes")) {
		if (!m_RootNode->Load(rootNodeWrapper))
			return false;
	} else {
		FURYE << "root_node not found!";
		return false;
	}

	// Re-link skinned-mesh Joints to SceneNodes after load (Mesh::Load
	// rebuilds joints without refs). UUID is primary (unambiguous across
	// instances); name is the fallback for old scenes without the field.
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
				// Fallback for old scenes without persisted UUIDs.
				auto it = nodesByName.find(joint->GetName());
				if (it != nodesByName.end()) node = it->second;
			}
			if (node)
				joint->SetSceneNode(node);
		}
		return true;
	});

	// Registration pass: iterate every material's textures and
	// register them in EntityManager. This handles both old scenes
	// (no top-level "textures" array -- textures come from materials)
	// and new scenes (top-level array already loaded, this pass
	// catches any material-bound textures not in the array).
	// Add returns false if the texture is already registered (same
	// UUID) -- that's fine, just means it's a duplicate reference.
	m_EntityManager->ForEach<Material>([&](const Material::Ptr& mat) -> bool {
		for (const auto& kv : mat->GetTextures()) {
			if (kv.second)
				m_EntityManager->Add(kv.second);
		}
		return true;
	});

	// setup scene manager
	m_SceneManager->AddSceneNodeRecursively(m_RootNode);

	// renderSettings block is optional -- old scenes (pre-this change)
	// load with the LDR + CSM-on + empty chain defaults that
	// RenderSettings() constructs by default. When present, parse it
	// and overwrite.
	if (auto rsNode = FindMember(wrapper, "renderSettings"))
	{
		if (!m_RenderSettings)
			m_RenderSettings = std::make_shared<RenderSettings>();
		m_RenderSettings->Load(rsNode, false);
	}

	return true;
}

void Scene::Save(void* wrapper, bool object) {
	if (object)
		StartObject(wrapper);

	Entity::Save(wrapper, false);

	// Format version marker (kFormatVersion); pre-versioning files
	// are version 1.
	SaveKey(wrapper, "version");
	SaveValue(wrapper, kFormatVersion);

	// save textures (top-level array -- deduped by UUID)
	SaveKey(wrapper, "textures");
	StartArray(wrapper);
	m_EntityManager->ForEach<Texture>([&](const Texture::Ptr& ptr) -> bool {
		ptr->Save(wrapper);
		return true;
	});
	EndArray(wrapper);

	// save materials
	SaveKey(wrapper, "materials");
	StartArray(wrapper);
	m_EntityManager->ForEach<Material>([&](const Material::Ptr& ptr) -> bool {
		ptr->Save(wrapper);
		return true;
	});
	EndArray(wrapper);

	// save meshes -- the top-level array holds only LOD-0 (source)
	// meshes. The LOD chain lives inline on each source mesh's
	// `lod_meshes` sub-object and is emitted by Mesh::Save itself.
	// LOD meshes are never added to the EntityManager, so this loop
	// never sees them and no dedup is required.
	SaveKey(wrapper, "meshes");
	StartArray(wrapper);
	m_EntityManager->ForEach<Mesh>([&](const Mesh::Ptr& ptr) -> bool {
		ptr->Save(wrapper);
		return true;
	});
	EndArray(wrapper);

	// save animation clips
	SaveKey(wrapper, "animations");
	StartArray(wrapper);
	m_EntityManager->ForEach<AnimationClip>([&](const std::shared_ptr<AnimationClip>& ptr) -> bool {
		ptr->Save(wrapper);
		return true;
	});
	EndArray(wrapper);

	// save particle systems (assets referenced by name from
	// ParticleRenderer components).
	SaveKey(wrapper, "particleSystems");
	StartArray(wrapper);
	m_EntityManager->ForEach<ParticleSystem>([&](const ParticleSystem::Ptr& ptr) -> bool {
		ptr->Save(wrapper);
		return true;
	});
	EndArray(wrapper);

	// save nodes
	SaveKey(wrapper, "nodes");
	m_RootNode->Save(wrapper);

	// renderSettings block (always emitted by Save so reload is
	// stable; backwards compatibility is on the Load side).
	if (m_RenderSettings)
	{
		SaveKey(wrapper, "renderSettings");
		// Pass object=true so RenderSettings::Save wraps its
		// fields in { ... } -- Scene::Save has just emitted the
		// "renderSettings" key and the writer expects a value
		// next.
		m_RenderSettings->Save(wrapper, true);
	}

	if (object)
		EndObject(wrapper);
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
} // namespace fury