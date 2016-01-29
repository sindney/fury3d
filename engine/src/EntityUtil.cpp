#include "Debug.h"
#include "EntityUtil.h"

namespace fury
{
	// EntityMap

	EntityUtil::EntityMap::Ptr EntityUtil::EntityMap::Create()
	{
		return std::make_shared<EntityMap>();
	}

	bool EntityUtil::EntityMap::AddEntity(const Entity::Ptr &entity)
	{
		return m_Entities.emplace(entity->GetHashCode(), entity).second;
	}

	Entity::Ptr EntityUtil::EntityMap::RemoveEntity(size_t hashcode)
	{
		auto it = m_Entities.find(hashcode);
		Entity::Ptr result = nullptr;

		if (it != m_Entities.end())
		{
			result = it->second;
			m_Entities.erase(it);
		}

		return result;
	}

	Entity::Ptr EntityUtil::EntityMap::FindEntity(size_t hashcode) const
	{
		auto it = m_Entities.find(hashcode);

		if(it != m_Entities.end())
			return it->second;
		else 
			return nullptr;
	}

	void EntityUtil::EntityMap::RemoveAllEntities()
	{
		m_Entities.clear();
	}

	// EntityUtil
	
	bool EntityUtil::AddEntity(const Entity::Ptr &entity)
	{
		auto it = m_EntityTypes.find(entity->GetTypeIndex());
		EntityMap::Ptr entityMap = nullptr;

		if (it != m_EntityTypes.end())
		{
			entityMap = it->second;
		}
		if (entityMap == nullptr)
		{
			entityMap = EntityMap::Create();
			m_EntityTypes.emplace(entity->GetTypeIndex(), entityMap);
		}

		return entityMap->AddEntity(entity);
	}

	void EntityUtil::RemoveAllEntities()
	{
		m_EntityTypes.clear();
	}
}