#include "Entity.h"
#include "StringUtil.h"

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
		m_HashCode = StringUtil::Instance()->GetHashCode(name);
		return m_HashCode;
	}
}