#ifndef _FURY_OBJECTPOOL_H_
#define _FURY_OBJECTPOOL_H_

#include <unordered_map>
#include <stack>
#include <functional>
#include <memory>

#include "Macros.h"

namespace fury
{
	template<typename Key, typename Object, class... Args>
	class FURY_API ObjectPool
	{
	private:

		std::unordered_map<Key, std::stack<Object>> m_Pool;

		std::function<Key(Args&&... args)> m_KeyGen;

		std::function<Key(Object)> m_KeyGen2;

		std::function<Object(Args&&... args)> m_Creater;

		std::function<void(Object)> m_Collector;

	public:

		ObjectPool(std::function<Key(Args&&... args)> keyGen, std::function<Key(Object)> keyGen2,
			std::function<Object(Args&&... args)> creater, std::function<void(Object)> collector) :
			m_KeyGen(keyGen), m_KeyGen2(keyGen2), m_Creater(creater), m_Collector(collector)
		{

		}

		Object Get(Args&&... args)
		{
			Key key = m_KeyGen(std::forward<Args>(args)...);
			auto match = m_Pool.find(key);

			if (match == m_Pool.end() || match->second.size() < 1)
				return m_Creater(std::forward<Args>(args)...);

			auto top = match->second.top();
			match->second.pop();
			return top;
		}

		void Collect(Object obj)
		{
			m_Collector(obj);
			m_Pool[m_KeyGen2(obj)].push(obj);
		}
	};
}

#endif //_FURY_OBJECTPOOL_H_