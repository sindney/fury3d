#ifndef _FURY_ENTITY_UTIL_H_
#define _FURY_ENTITY_UTIL_H_

#include <unordered_map>

#include "Singleton.h"
#include "Entity.h"
#include "StringUtil.h"

namespace fury
{
	class FURY_API EntityUtil : public Singleton<EntityUtil> 
	{
	public:

		typedef std::shared_ptr<EntityUtil> Ptr;

		// returns false if there's a name conflict.
		bool AddEntity(const Entity::Ptr &entity);

		template<class EntityType>
		std::shared_ptr<EntityType> RemoveEntity(const std::string &name);

		template<class EntityType>
		std::shared_ptr<EntityType> FindEntity(const std::string &name);

		template<class EntityType>
		std::shared_ptr<EntityType> RemoveEntity(size_t hashcode);

		template<class EntityType>
		std::shared_ptr<EntityType> FindEntity(size_t hashcode);

		template<class EntityType>
		void RemoveEntities(std::type_index type);

		void RemoveAllEntities();

	protected:

		class FURY_API EntityMap
		{
		public:

			typedef std::shared_ptr<EntityMap> Ptr;

			static Ptr Create();

			// returns false if there's a name conflict.
			bool AddEntity(const Entity::Ptr &entity);

			Entity::Ptr RemoveEntity(size_t hashcode);

			Entity::Ptr FindEntity(size_t hashcode) const;

			void RemoveAllEntities();

		private:

			std::unordered_map<size_t, Entity::Ptr> m_Entities;

		};

		typedef std::unordered_map<std::type_index, EntityMap::Ptr> TypeMap;

		TypeMap m_EntityTypes;
	};

	template<class EntityType>
	std::shared_ptr<EntityType> EntityUtil::RemoveEntity(const std::string &name)
	{
		return RemoveEntity<EntityType>(StringUtil::Instance()->GetHashCode(name));
	}

	template<class EntityType>
	std::shared_ptr<EntityType> EntityUtil::FindEntity(const std::string &name)
	{
		return FindEntity<EntityType>(StringUtil::Instance()->GetHashCode(name));
	}

	template<class EntityType>
	std::shared_ptr<EntityType> EntityUtil::RemoveEntity(size_t hashcode)
	{
		std::type_index typeIndex = typeid(EntityType);
		auto it = m_EntityTypes.find(typeIndex);

		if(it != m_EntityTypes.end())
		{
			auto &entityMap = it->second;
			return std::static_pointer_cast<EntityType>(entityMap->RemoveEntity(hashcode));
		}

		return nullptr;
	}

	template<class EntityType>
	std::shared_ptr<EntityType> EntityUtil::FindEntity(size_t hashcode)
	{
		std::type_index typeIndex = typeid(EntityType);
		auto it = m_EntityTypes.find(typeIndex);

		if(it != m_EntityTypes.end())
			return std::static_pointer_cast<EntityType>(it->second->FindEntity(hashcode));

		return nullptr;
	}

	template<class EntityType>
	void EntityUtil::RemoveEntities(std::type_index type)
	{
		auto it = m_EntityTypes.find(type);
		if (it != m_EntityTypes.end())
			it->second->RemoveAllEntities();
	}
}

#endif // _FURY_ENTITY_UTIL_H_