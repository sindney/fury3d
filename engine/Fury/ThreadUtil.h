#ifndef _FURY_THREAD_UTIL_H_
#define _FURY_THREAD_UTIL_H_

// Implimentation refers to: https://github.com/progschj/ThreadPool

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>

#include "Fury/Singleton.h"

namespace fury
{
	class FURY_API ThreadUtil : public Singleton<ThreadUtil, size_t>
	{
	public:

		typedef std::shared_ptr<ThreadUtil> Ptr;

	protected:

		class TaskState
		{
		public:

			size_t id = 0;

			int progress = 0;

			bool finished = false;

			std::shared_ptr<void> data;

			std::function<void()> callback;

			std::function<void(int)> progressChanged;

			TaskState(size_t id, std::shared_ptr<void> data)
				: id(id), data(data) {}
		};

		static std::thread::id m_MainThreadId;

		static size_t m_TaskKey;

		std::unordered_map<size_t, std::shared_ptr<TaskState>> m_TaskStates;

		std::unordered_map<size_t, int> m_TaskProgresses;

		std::queue<std::function<void()>> m_Tasks;

		std::vector<std::thread> m_Workers;

		std::mutex m_QueueMutex;

		std::condition_variable m_Condiction;

		bool m_Stop;

	public:

		ThreadUtil(unsigned int numThreads);

		~ThreadUtil();
		
		size_t Enqueue(std::function<void(int&)> task, std::function<void()> callback, std::function<void(int)> progressChanged = nullptr);

		template<class ReturnType>
		size_t Enqueue(std::function<std::shared_ptr<ReturnType>(int&)> task, std::function<void(std::shared_ptr<ReturnType>)> callback, 
			std::function<void(int)> progressChanged = nullptr)
		{
			std::unique_lock<std::mutex> lock(m_QueueMutex);

			// don't allow enqueueing after stopping the pool
			if (m_Stop)
				throw std::runtime_error("Enqueue on stopped ThreadPool");

			size_t key = m_TaskKey++;
			auto state = std::make_shared<TaskState>(key, nullptr);
			state->callback = [callback, state]
			{
				callback(std::static_pointer_cast<ReturnType>(state->data));
			};
			state->progressChanged = progressChanged;

			m_Tasks.emplace([task, state]()
			{
				state->data = task(state->progress);
				state->finished = true;
			});

			m_TaskStates.emplace(key, state);
			m_Condiction.notify_one();
			return key;
		}

		void Update();

		size_t GetWorkerCount();

		void SetMainThread();

		bool IsMainThread();
	};
}

#endif // _FURY_THREAD_UTIL_H_