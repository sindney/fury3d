#ifndef _FURY_STRING_UTIL_H_
#define _FURY_STRING_UTIL_H_

#include <string>

#include "Singleton.h"

namespace fury
{
	class FURY_API StringUtil : public Singleton<StringUtil>
	{
	public:

		typedef std::shared_ptr<StringUtil> Ptr;

		inline size_t GetHashCode(const std::string &str)
		{
			return m_HashFunc(str);
		}

	private:

		std::hash<std::string> m_HashFunc;

	};
}

#endif // _FURY_STRING_UTIL_H_
