#ifndef _FURY_SINGLETON_H_
#define _FURY_SINGLETON_H_

#include <memory>
#include <iostream>

#include "Macros.h"

namespace fury
{
	template<class TargetType, class... Args>
	class Singleton
	{
	protected:

		static std::shared_ptr<TargetType> m_Instance;

	public:

		// this returns reference to current instance.
		// so you can call Instance().reset() to destory static instance.
		inline static std::shared_ptr<TargetType> &Instance()
		{
			ASSERT_MSG(m_Instance, "Singleton instance doesn't exist!");
			return m_Instance;
		}

		// make sure your singleton initializes later than log singleton.
		inline static std::shared_ptr<TargetType> &Initialize(Args&&... args)
		{
			ASSERT_MSG(!m_Instance, "Singleton instance already exist!");
			m_Instance = std::make_shared<TargetType>(std::forward<Args>(args)...);
			return m_Instance;
		}

		// don't do logs in singleton's destructor
		// because when destructing, Log's instance might be destoried already.
		virtual ~Singleton()
		{
			m_Instance = nullptr;
		}
	};

	template<class TargetType, class... Args>
	std::shared_ptr<TargetType> Singleton<TargetType, Args...>::m_Instance = nullptr;
}

#endif // _FURY_SINGLETON_H_