#include "Fury/Scene.h"
#include "Fury/OcTree.h"
#include "Fury/EntityManager.h"
#include "Fury/SceneNode.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"

namespace fury
{
	Scene::Ptr Scene::Active = nullptr;

	std::string Scene::Path(const std::string &path)
	{
		if (Scene::Active != nullptr)
			return Scene::Active->GetWorkingDir() + path;
		else
			return path;
	}

	std::shared_ptr<EntityManager> Scene::Manager()
	{
		ASSERT_MSG(Scene::Active != nullptr, "Scene::Active == nullptr");
		return Scene::Active->GetEntityManager();
	}

	Scene::Ptr Scene::Create(const std::string &name, const std::string &workingDir, const std::shared_ptr<SceneManager> &sceneManager)
	{
		return std::make_shared<Scene>(name, workingDir, sceneManager);
	}

	Scene::Scene(const std::string &name, const std::string &workingDir, const std::shared_ptr<SceneManager> &sceneManager) :
		Entity(name), m_WorkingDir(workingDir)
	{
		m_TypeIndex = typeid(Scene);
		if (sceneManager == nullptr)
			m_SceneManager = OcTree::Create(Vector4(-1000), Vector4(1000), 2);
		else
			m_SceneManager = sceneManager;

		m_EntityManager = EntityManager::Create();
		m_RootNode = SceneNode::Create("RootNode");
	}

	Scene::~Scene()
	{
		FURYD << "Scene " << m_Name << " Destoried!";
		Clear();
	}

	void Scene::Clear()
	{
		m_EntityManager->RemoveAll();
		m_SceneManager->Clear();
		m_RootNode->RemoveAllChilds();
		m_RootNode->RemoveAllComponents();
	}

	bool Scene::Load(const void* wrapper, bool object)
	{
		Clear();

		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!Entity::Load(wrapper, false))
			return false;

		// load materials
		if (!LoadArray(wrapper, "materials", [&](const void* node) -> bool
		{
			auto material = Material::Create("temp");
			if (!material->Load(node))
				return false;

			m_EntityManager->Add(material);
			return true;
		}))
		{
			FURYE << "Error serializing materials!";
			return false;
		}

		// load meshes
		if (!LoadArray(wrapper, "meshes", [&](const void* node) -> bool
		{
			auto mesh = Mesh::Create("temp");
			if (!mesh->Load(node))
				return false;

			m_EntityManager->Add(mesh);
			return true;
		}))
		{
			FURYE << "Error serializing meshes!";
			return false;
		}

		// load nodes
		if (auto rootNodeWrapper = FindMember(wrapper, "nodes"))
		{
			if (!m_RootNode->Load(rootNodeWrapper))
				return false;
		}
		else
		{
			FURYE << "root_node not found!";
			return false;
		}

		// setup scene manager
		m_SceneManager->AddSceneNodeRecursively(m_RootNode);

		return true;
	}

	void Scene::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		// save materials
		SaveKey(wrapper, "materials");
		StartArray(wrapper);
		m_EntityManager->ForEach<Material>([&](const Material::Ptr &ptr) -> bool 
		{
			ptr->Save(wrapper);
			return true;
		});
		EndArray(wrapper);

		// save meshes
		SaveKey(wrapper, "meshes");
		StartArray(wrapper);
		m_EntityManager->ForEach<Mesh>([&](const Mesh::Ptr &ptr) -> bool
		{
			ptr->Save(wrapper);
			return true;
		});
		EndArray(wrapper);

		// save nodes
		SaveKey(wrapper, "nodes");
		m_RootNode->Save(wrapper);

		if (object)
			EndObject(wrapper);
	}

	std::shared_ptr<SceneNode> Scene::GetRootNode() const
	{
		return m_RootNode;
	}

	std::shared_ptr<SceneManager> Scene::GetSceneManager() const
	{
		return m_SceneManager;
	}

	std::shared_ptr<EntityManager> Scene::GetEntityManager() const
	{
		return m_EntityManager;
	}

	std::string Scene::GetWorkingDir() const
	{
		return m_WorkingDir;
	}

	void Scene::SetWorkingDir(const std::string &path)
	{
		m_WorkingDir = path;
	}
}