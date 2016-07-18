#include "Entity.h"
#include "Log.h"

namespace fury
{
	Entity::Ptr Entity::Create(const std::string &name)
	{
		return std::make_shared<Entity>(name);
	}

	Entity::Entity(const std::string &name) : m_TypeIndex(typeid(Entity))
	{
		SetName(name);
	}

	std::type_index Entity::GetTypeIndex() const
	{
		return m_TypeIndex;
	}

	bool Entity::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "name", m_Name))
		{
			FURYE << "Name not found!";
			return false;
		}
		SetName(m_Name);

		return true;
	}

	void Entity::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		SaveKey(wrapper, "name");
		SaveValue(wrapper, m_Name);

		if (object)
			EndObject(wrapper);
	}

	std::string Entity::GetName() const
	{
		return m_Name;
	}

	size_t Entity::GetHashCode() const
	{
		return m_HashCode;
	}

	size_t Entity::SetName(const std::string &name)
	{
		m_Name = name;
		m_HashCode = std::hash<std::string>()(name);
		return m_HashCode;
	}
}