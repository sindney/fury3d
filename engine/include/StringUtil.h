#ifndef _FURY_STRING_UTIL_H_
#define _FURY_STRING_UTIL_H_

#include <functional>
#include <unordered_map>
#include <string>

#include "Singleton.h"

namespace fury
{
	class FURY_API StringUtil : public Singleton<StringUtil>
	{
	public:

		typedef std::shared_ptr<StringUtil> Ptr;

		size_t GetHashCode(const std::string &str);

	private:

		std::unordered_map<std::string, size_t> m_StringHashCodes;

		std::hash<std::string> m_HashFunc;

	};
}

#endif // _FURY_STRING_UTIL_H_