#ifndef _FURY_SINGLETON_H_
#define _FURY_SINGLETON_H_

#include <memory>

#include "Macros.h"

namespace fury
{
	template<class TargetType>
	class Singleton
	{
	protected:

		static std::shared_ptr<TargetType> m_Instance;

		//bool m_Initialized = false;

	public:

		// this returns reference to current instance.
		// so you can call Instance().reset() to destory static instance.
		static std::shared_ptr<TargetType> &Instance()
		{
			if (m_Instance == nullptr)
				m_Instance = std::make_shared<TargetType>();
			return m_Instance;
		}

		virtual ~Singleton()
		{
			m_Instance = nullptr;
		}
	};

	template<class TargetType>
	std::shared_ptr<TargetType> Singleton<TargetType>::m_Instance = nullptr;
}

#endif // _FURY_SINGLETON_H_