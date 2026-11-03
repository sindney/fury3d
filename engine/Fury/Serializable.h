#ifndef _FURY_SERIALIZEABLE_H_
#define _FURY_SERIALIZEABLE_H_

#include <memory>

#include "Fury/Macros.h"

namespace fury
{
	class Archive;

	class FURY_API Serializable
	{
	public:
		typedef std::shared_ptr<Serializable> Ptr;

		virtual ~Serializable() = default;

		virtual void Serialize(Archive& ar) = 0;
	};
}

#endif // _FURY_SERIALIZEABLE_H_
