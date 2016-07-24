#include <stack>

#include "Fury/Log.h"
#include "Fury/ThreadUtil.h"

namespace fury
{
	std::thread::id ThreadUtil::m_MainThreadId;

	ThreadUtil::ThreadUtil(size_t numThreads)
		: m_Stop(false)
	{
		size_t maxThreads = std::thread::hardware_concurrency();
		if (numThreads > maxThreads)
		{
			numThreads = maxThreads / 2;
			FURYW << "Hardware supports " << maxThreads << " threads at most!";
		}

		for (size_t i = 0; i < numThreads; ++i)
		{
			m_Workers.emplace_back([this]
			{
				while (true)
				{
					std::function<void()> task;

					{
						std::unique_lock<std::mutex> lock(this->m_QueueMutex);
						this->m_Condiction.wait(lock, [this]
						{ 
							return this->m_Stop || !this->m_Tasks.empty(); 
						});

						if (this->m_Stop && this->m_Tasks.empty())
							return;

						task = std::move(this->m_Tasks.front());
						this->m_Tasks.pop();
					}

					task();
				}
			});
		}
	}

	ThreadUtil::~ThreadUtil()
	{
		{
			std::unique_lock<std::mutex> lock(m_QueueMutex);
			m_Stop = true;
		}

		m_Condiction.notify_all();
		for (std::thread &worker : m_Workers)
			worker.join();
	}

	size_t ThreadUtil::GetWorkerCount()
	{
		return m_Workers.size();
	}

	void ThreadUtil::SetMainThread()
	{
		m_MainThreadId = std::this_thread::get_id();
	}

	bool ThreadUtil::IsMainThread()
	{
		return std::this_thread::get_id() == m_MainThreadId;
	}
}