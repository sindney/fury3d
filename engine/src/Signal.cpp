#include "Debug.h"
#include "Signal.h"
#include "StringUtil.h"

namespace fury
{
	Signal::Ptr Signal::Create()
	{
		return std::make_shared<Signal>();
	}

	bool Signal::Connect(const std::string &slotName, const std::shared_ptr<void> &listener, const std::function<void(const std::shared_ptr<void>&)> &funcPtr)
	{
		return Connect(StringUtil::Instance()->GetHashCode(slotName), listener, funcPtr);
	}

	bool Signal::Connect(size_t slotName, const std::shared_ptr<void> &listener, const std::function<void(const std::shared_ptr<void>&)> &funcPtr)
	{
		auto it = m_Slots.find(slotName);
		if (it != m_Slots.end())
		{
			auto result = it->second->Listeners.emplace(listener, funcPtr);
			return result.second;
		}

		return false;
	}

	void Signal::Disconnect(const std::string &slotName, const std::shared_ptr<void> &listener)
	{
		Disconnect(StringUtil::Instance()->GetHashCode(slotName), listener);
	}

	void Signal::Disconnect(size_t slotName, const std::shared_ptr<void> &listener)
	{
		auto it = m_Slots.find(slotName);
		if (it != m_Slots.end())
		{
			it->second->Listeners.erase(listener);
		}
	}

	void Signal::Clear(const std::string &slotName)
	{
		Clear(StringUtil::Instance()->GetHashCode(slotName));
	}

	void Signal::Clear(size_t slotName)
	{
		auto it = m_Slots.find(slotName);
		if (it != m_Slots.end())
		{
			it->second->Listeners.clear();
		}
	}

	void Signal::Clear()
	{
		for (auto slot : m_Slots)
		{
			slot.second->Listeners.clear();
		}
	}

	void Signal::Emit(const std::string &slotName, const std::shared_ptr<void> &sender)
	{
		Emit(StringUtil::Instance()->GetHashCode(slotName), sender);
	}

	void Signal::Emit(size_t slotName, const std::shared_ptr<void> &sender)
	{
		auto it = m_Slots.find(slotName);
		if (it != m_Slots.end())
		{
			auto slot = it->second;
			for (auto it = slot->Listeners.begin(); it != slot->Listeners.end();)
			{
				if (!it->first.expired())
				{
					// listener alive, continue .. 
					it->second(sender);
					++it;
				}
				else
				{
					// listener dead, erase .. 
					it = slot->Listeners.erase(it);
				}
			}
		}
		else
		{
			// if no slot yet, insert one.
			m_Slots.emplace(slotName, std::make_shared<Slot>());
		}
	}
}