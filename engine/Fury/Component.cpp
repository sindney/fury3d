#include "Fury/Component.h"
#include "Fury/SceneNode.h"

namespace fury
{
	Component::Component() : m_TypeIndex(typeid(Component)) {}

	Component::~Component()
	{
		if (m_Owner.expired()) return;

		auto ptr = m_Owner.lock();
		ptr->RemoveComponent(m_TypeIndex);
	}

	std::type_index Component::GetTypeIndex() const
	{
		return m_TypeIndex;
	}

	bool Component::HasOwner() const
	{
		return !m_Owner.expired();
	}

	std::shared_ptr<SceneNode> Component::GetOwner() const
	{
		return m_Owner.lock();
	}

	void Component::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		if (m_Owner.expired())
			m_Owner = node;
	}

	void Component::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		(void)node;
		m_Owner.reset();
	}

	void Component::OnOwnerDestructing(SceneNode &node)
	{
		(void)node;
		m_Owner.reset();
	}
}