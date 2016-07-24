#ifndef _FURY_COMPONENT_H_
#define _FURY_COMPONENT_H_

#include <typeindex>
#include <memory>

#include "Fury/TypeComparable.h"
#include "Fury/Serializable.h"

namespace fury
{
	class SceneNode;

	class FURY_API Component : public TypeComparable, public Serializable
	{
		friend class SceneNode;

	public:

		typedef std::shared_ptr<Component> Ptr;

		Component();

		virtual ~Component();

		virtual std::type_index GetTypeIndex() const;

		virtual Ptr Clone() const = 0;

		virtual bool Load(const void* wrapper, bool object = true) 
		{
			return true;
		}

		virtual void Save(void* wrapper, bool object = true) {}

		bool HasOwner() const;

	protected:

		std::type_index m_TypeIndex;

		std::weak_ptr<SceneNode> m_Owner;

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node);

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node);
		
		// this is called form owner's destructor.
		// so don't do nasty thing in it.
		virtual void OnOwnerDestructing(SceneNode &node);
	};
}

#endif // _FURY_COMPONENT_H_