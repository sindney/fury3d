#ifndef _FURY_TYPE_COMPARABLE_H_
#define _FURY_TYPE_COMPARABLE_H_

#include <typeindex>

#include "Macros.h"

namespace fury
{
	class FURY_API TypeComparable
	{
	public:

		virtual std::type_index GetTypeIndex() const = 0;
	};
}

#endif // _FURY_TYPE_COMPARABLE_H_