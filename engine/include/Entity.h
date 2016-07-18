#ifndef _FURY_ENTITY_H_
#define _FURY_ENTITY_H_

#include <memory>
#include <string>

#include "TypeComparable.h"
#include "Serializable.h"

namespace fury
{
	class FURY_API Entity : public TypeComparable, public Serializable
	{
	public:

		typedef std::shared_ptr<Entity> Ptr;

		static Ptr Create(const std::string &name);

		Entity(const std::string &name);

		virtual ~Entity() {}

		virtual std::type_index GetTypeIndex() const;

		virtual bool Load(const void* wrapper, bool object = true);

		virtual void Save(void* wrapper, bool object = true);

		std::string GetName() const;

		size_t GetHashCode() const;

		size_t SetName(const std::string &name);

	protected:

		std::type_index m_TypeIndex;

		std::string m_Name;

		size_t m_HashCode;
	};
}

#endif // _FURY_ENTITY_H_