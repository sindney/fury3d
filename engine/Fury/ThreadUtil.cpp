#include <stack>
#include <list>

#include "Fury/Log.h"
#include "Fury/ThreadUtil.h"

namespace fury
{
	std::thread::id ThreadUtil::m_MainThreadId;

	size_t ThreadUtil::m_TaskKey = 0;

	ThreadUtil::ThreadUtil(unsigned int numThreads)
		: m_Stop(false)
	{
		unsigned int maxThreads = std::thread::hardware_concurrency();
		if (numThreads > maxThreads)
		{
			numThreads = maxThreads / 2;
			FURYW << "Hardware supports " << maxThreads << " threads at most!";
		}

		for (unsigned int i = 0; i < numThreads; i++)
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
			m_TaskStates.clear();
		}

		m_Condiction.notify_all();
		for (std::thread &worker : m_Workers)
			worker.join();
	}

	size_t ThreadUtil::Enqueue(std::function<void(int&)> task, std::function<void()> callback, std::function<void(int)> progressChanged)
	{
		std::unique_lock<std::mutex> lock(m_QueueMutex);

		// don't allow enqueueing after stopping the pool
		if (m_Stop)
			throw std::runtime_error("Enqueue on stopped ThreadPool");

		size_t key = m_TaskKey++;
		auto state = std::make_shared<TaskState>(key, nullptr);
		state->callback = callback;
		state->progressChanged = progressChanged;

		m_Tasks.emplace([task, state]()
		{
			task(state->progress);
			state->finished = true;
		});

		m_TaskStates.emplace(key, state);
		m_Condiction.notify_one();
		return key;
	}

	void ThreadUtil::Update()
	{
		std::unique_lock<std::mutex> lock(m_QueueMutex);

		std::list<size_t> finishedTasks;
		for (auto &pair : m_TaskStates)
		{
			auto id = pair.first;
			auto &state = pair.second;

			if (state->progressChanged)
			{
				auto it = m_TaskProgresses.find(id);
				if (it == m_TaskProgresses.end())
				{
					m_TaskProgresses.emplace(id, state->progress);
					state->progressChanged(state->progress);
				}
				else if (it->second != state->progress)
				{
					m_TaskProgresses[id] = state->progress;
					state->progressChanged(state->progress);
				}
			}

			if (state->finished)
				finishedTasks.emplace_back(id);
		}

		for (auto &id : finishedTasks)
		{
			auto state = m_TaskStates[id];
			m_TaskStates.erase(id);
			m_TaskProgresses.erase(id);
			if (state->callback)
				state->callback();
		}
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