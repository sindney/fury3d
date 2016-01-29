#ifndef _FURY_SIGNAL_H_
#define _FURY_SIGNAL_H_

#include <functional>
#include <map>
#include <unordered_map>
#include <memory>
#include <string>

#include "Macros.h"

#define BIND_FUNC(listener, func) std::bind(func, listener.get(), std::placeholders::_1)

namespace fury
{
	class FURY_API Signal
	{
	private:

		class Slot
		{
		public:

			typedef std::shared_ptr<Slot> Ptr;

			std::map<
				std::weak_ptr<void>,
				std::function<void(const std::shared_ptr<void>&)>,
				std::owner_less<std::weak_ptr<void>>
			> Listeners;
		};

		std::unordered_map<size_t, Slot::Ptr> m_Slots;

	public:

		typedef std::shared_ptr<Signal> Ptr;

		static Ptr Create();

		bool Connect(const std::string &slotName, const std::shared_ptr<void> &listener, const std::function<void(const std::shared_ptr<void>&)> &funcPtr);

		bool Connect(size_t slotName, const std::shared_ptr<void> &listener, const std::function<void(const std::shared_ptr<void>&)> &funcPtr);

		void Disconnect(const std::string &slotName, const std::shared_ptr<void> &listener);

		void Disconnect(size_t slotName, const std::shared_ptr<void> &listener);

		void Clear(const std::string &slotName);

		void Clear(size_t slotName);

		void Clear();

		void Emit(const std::string &slotName, const std::shared_ptr<void> &sender);

		void Emit(size_t slotName, const std::shared_ptr<void> &sender);
	};
}

#endif // _FURY_SIGNAL_H_