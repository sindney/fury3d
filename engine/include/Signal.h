#ifndef _FURY_SIGNAL_H_
#define _FURY_SIGNAL_H_

#include <functional>
#include <memory>
#include <stack>
#include <vector>
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

		std::vector<CallbackPair> m_Links;

		std::shared_ptr<void> m_DefaultOwner;

		std::stack<size_t> m_KeyStack;

		size_t m_CurrentKey;

		std::type_index m_TypeIndex;

	public:

		Signal() : m_TypeIndex(typeid(Signal<Args...>))
		{
			m_DefaultOwner = std::static_pointer_cast<void>(std::make_shared<bool>(true));
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

		virtual std::type_index GetTypeIndex() const
		{
			return m_TypeIndex;
		}

		size_t Connect(void(*f)(Args...))
		{
			m_Links.emplace_back(std::make_pair<std::weak_ptr<void>, CallbackFunc>(m_DefaultOwner, f));
			return GenKey();
		}

		template <class Reciver>
		size_t Connect(const std::shared_ptr<Reciver> &reciver, void(Reciver::*f)(Args ...))
		{
			auto rawPtr = reciver.get();
			CallbackFunc forward = [rawPtr, f](Args&&... args)
			{
				(rawPtr->*f)(std::forward<Args>(args)...);
			};

			m_Links.emplace_back(std::make_pair(std::static_pointer_cast<void>(reciver), forward));

			return GenKey();
		}

		bool Disconnect(size_t key)
		{
			if (key < m_Links.size())
			{
				m_KeyStack.push(key);
				m_Links.erase(m_Links.begin() + key);
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
			m_Links.clear();
		}

		void Emit(Args&&... args)
		{
			for (size_t i = 0; i < m_Links.size(); i++)
			{
				auto pair = m_Links[i];
				if (pair.first.expired())
				{
					m_Links.erase(m_Links.begin() + i);
					i--;
					continue;
				}
				pair.second(std::forward<Args>(args)...);
			}
		}
	};
}

#endif // _FURY_SIGNAL_H_