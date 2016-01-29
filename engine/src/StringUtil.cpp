#include "StringUtil.h"

namespace fury
{
	size_t StringUtil::GetHashCode(const std::string &str)
	{
		auto it = m_StringHashCodes.find(str);
		if(it != m_StringHashCodes.end())
			return it->second;

		size_t hashcode = m_HashFunc(str);
		m_StringHashCodes.emplace(str, hashcode);
		return hashcode;
	}
}