#ifndef _FURY_SIGNAL_H_
#define _FURY_SIGNAL_H_

#include <functional>
#include <memory>
#include <stack>
#include <vector>
#include <unordered_map>
#include <string>

#include "TypeComparable.h"

namespace fury
{
	template <class... Args>
	class FURY_API Signal : public TypeComparable
	{
	public:

		typedef std::shared_ptr<Signal<Args...>> Ptr;

		typedef std::function<void(Args...)> CallbackFunc;

		typedef std::pair<std::weak_ptr<void>, CallbackFunc> CallbackPair;

		static Ptr Create()
		{
			return std::make_shared<Signal<Args...>>();
		}

	private:

		std::unordered_map<size_t, CallbackPair> m_LinkMap;

		std::shared_ptr<void> m_DefaultOwner;

		std::stack<size_t> m_KeyStack;

		size_t m_CurrentKey;

		std::type_index m_TypeIndex;

	public:

		Signal() : m_TypeIndex(typeid(Signal<Args...>))
		{
			m_DefaultOwner = std::static_pointer_cast<void>(std::make_shared<bool>(true));
		}

		virtual std::type_index GetTypeIndex() const
		{
			return m_TypeIndex;
		}

		size_t GenKey()
		{
			if (m_KeyStack.empty())
			{
				return m_CurrentKey++;
			}
			else
			{
				auto key = m_KeyStack.top();
				m_KeyStack.pop();
				return key;
			}
		}

		size_t Connect(void(*f)(Args...))
		{
			auto key = GenKey();
			m_LinkMap.emplace(std::make_pair(key,
				std::make_pair<std::weak_ptr<void>, CallbackFunc>(m_DefaultOwner, f)));
			return key;
		}

		template <class Reciver>
		size_t Connect(const std::shared_ptr<Reciver> &reciver, void(Reciver::*f)(Args ...))
		{
			auto key = GenKey();
			auto rawPtr = reciver.get();
			CallbackFunc forward = [rawPtr, f](Args&&... args)
			{
				(rawPtr->*f)(std::forward<Args>(args)...);
			};

			m_LinkMap.emplace(std::make_pair(key,
				std::make_pair(std::static_pointer_cast<void>(reciver), forward)));

			return key;
		}

		bool Disconnect(size_t key)
		{
			auto it = m_LinkMap.find(key);
			if (it != m_LinkMap.end())
			{
				m_KeyStack.push(key);
				m_LinkMap.erase(it);
				return true;
			}
			else
			{
				return false;
			}
		}

		void Clear()
		{
			while (!m_KeyStack.empty())
				m_KeyStack.pop();

			m_CurrentKey = 0;
			m_LinkMap.clear();
		}

		void Emit(Args&&... args)
		{
			std::vector<typename std::unordered_map<size_t, CallbackPair>::iterator> tobeRemoved;

			for (auto it = m_LinkMap.begin(); it != m_LinkMap.end(); ++it)
			{
				if (it->second.first.expired())
					tobeRemoved.push_back(it);
				else
					it->second.second(std::forward<Args>(args)...);
			}

			for (auto it = tobeRemoved.begin(); it != tobeRemoved.end(); ++it)
				m_LinkMap.erase(*it);
		}
	};
}

#endif // _FURY_SIGNAL_H_
