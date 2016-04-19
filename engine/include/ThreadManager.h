#ifndef _FURY_THREAD_MANAGER_H_
#define _FURY_THREAD_MANAGER_H_

// Implimentation refers to: https://github.com/progschj/ThreadPool

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

#include "Singleton.h"

namespace fury
{
	class FURY_API ThreadManager : public Singleton<ThreadManager, size_t>
	{
	public:

		typedef std::shared_ptr<ThreadManager> Ptr;

	protected:

		static std::thread::id m_MainThreadId;

		std::vector<std::thread> m_Workers;

		std::queue<std::function<void()>> m_Tasks;

		std::mutex m_QueueMutex;

		std::condition_variable m_Condiction;

		bool m_Stop;

	public:

		ThreadManager(size_t numThreads);

		~ThreadManager();

		template<class F, class... Args>
		auto Enqueue(F&& f, Args&&... args)
			->std::future<typename std::result_of<F(Args...)>::type>
		{
			using return_type = typename std::result_of<F(Args...)>::type;

			auto task = std::make_shared<std::packaged_task<return_type()>>(
				std::bind(std::forward<F>(f), std::forward<Args>(args)...));

			std::future<return_type> res = task->get_future();
			{
				std::unique_lock<std::mutex> lock(m_QueueMutex);

				// don't allow enqueueing after stopping the pool
				if (m_Stop)
					throw std::runtime_error("Enqueue on stopped ThreadPool");

				m_Tasks.emplace([task](){ (*task)(); });
			}
			m_Condiction.notify_one();
			return res;
		}

		size_t GetWorkerCount();

		void SetMainThread();

		bool IsMainThread();
	};
}

#endif // _FURY_THREAD_MANAGER_H_