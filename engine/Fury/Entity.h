#ifndef _FURY_ENTITY_H_
#define _FURY_ENTITY_H_

#include <memory>
#include <string>

#include "Fury/TypeComparable.h"
#include "Fury/Serializable.h"

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

		// UUID -- persistent unique identifier. Used as the
		// EntityManager lookup key (via GetHashCode). Auto-generated
		// on construction; loaded from JSON if present.
		std::string GetUUID() const;

		// Sets the UUID and recomputes m_HashCode. Call this after
		// loading a UUID from JSON so the entity is keyed correctly.
		void SetUUID(const std::string &uuid);

		size_t GetHashCode() const;

		// SetName updates m_Name only -- it does NOT update m_HashCode
		// (which is now derived from m_UUID). Use SetUUID to change
		// the identity / hash.
		size_t SetName(const std::string &name);

	protected:

		std::type_index m_TypeIndex;

		std::string m_Name;

		std::string m_UUID;

		size_t m_HashCode;
	};
}

#endif // _FURY_ENTITY_H_