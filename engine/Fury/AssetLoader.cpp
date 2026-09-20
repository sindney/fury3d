#include "AssetLoader.h"

#include "Fury/AssetBackend.h"
#include "Fury/Profiler.h"

namespace fury
{
	AssetLoader& AssetLoader::Get()
	{
		static AssetLoader instance;
		return instance;
	}

	void AssetLoader::Initialize(int workers)
	{
		if (!m_Workers.empty())
			return;
		if (workers < 1)
			workers = 1;
		m_Stop = false;
		for (int i = 0; i < workers; i++)
			m_Workers.emplace_back([this]() { WorkerMain(); });
	}

	void AssetLoader::Shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(m_QueueMutex);
			m_Stop = true;
		}
		m_QueueCv.notify_all();
		for (auto& t : m_Workers)
			if (t.joinable())
				t.join();
		m_Workers.clear();

		std::lock_guard<std::mutex> lock(m_CacheMutex);
		m_Cache.clear();
	}

	bool AssetLoader::IsUp() const
	{
		return !m_Workers.empty();
	}

	void AssetLoader::Submit(const std::string& resolvedPath, int priority, DoneCallback cb)
	{
		{
			std::lock_guard<std::mutex> lock(m_QueueMutex);
			m_Queue.push_back({ resolvedPath, priority, std::move(cb) });
		}
		m_QueueCv.notify_one();
	}

	void AssetLoader::Prefetch(const std::string& resolvedPath, int priority)
	{
		Submit(resolvedPath, priority, {});
	}

	bool AssetLoader::TakePrefetched(const std::string& resolvedPath, std::vector<unsigned char>& out)
	{
		std::lock_guard<std::mutex> lock(m_CacheMutex);
		auto it = m_Cache.find(resolvedPath);
		if (it == m_Cache.end())
			return false;
		out = std::move(it->second);
		m_Cache.erase(it);
		return true;
	}

	void AssetLoader::Pump()
	{
		std::vector<std::pair<Request, bool>> done;
		{
			std::lock_guard<std::mutex> lock(m_DoneMutex);
			done.swap(m_Done);
		}
		for (auto& [req, ok] : done)
			if (req.cb)
				req.cb(req.path, ok);
	}

	void AssetLoader::Flush()
	{
		FURY_ZONE;
		for (;;)
		{
			size_t pending;
			{
				std::lock_guard<std::mutex> lock(m_QueueMutex);
				pending = m_Queue.size();
			}
			std::unique_lock<std::mutex> inflight(m_InflightMutex);
			if (pending == 0 && m_Inflight == 0)
				break;
			m_InflightCv.wait(inflight, [&] {
				size_t q;
				{
					std::lock_guard<std::mutex> lock(m_QueueMutex);
					q = m_Queue.size();
				}
				return q == 0 && m_Inflight == 0;
			});
		}
		Pump();
	}

	size_t AssetLoader::PendingCount()
	{
		std::lock_guard<std::mutex> qlock(m_QueueMutex);
		std::lock_guard<std::mutex> ilock(m_InflightMutex);
		return m_Queue.size() + m_Inflight;
	}

	void AssetLoader::WorkerMain()
	{
		for (;;)
		{
			Request req;
			{
				std::unique_lock<std::mutex> lock(m_QueueMutex);
				m_QueueCv.wait(lock, [&] { return m_Stop || !m_Queue.empty(); });
				if (m_Stop && m_Queue.empty())
					return;
				req = std::move(m_Queue.front());
				m_Queue.pop_front();
			}

			{
				std::lock_guard<std::mutex> lock(m_InflightMutex);
				++m_Inflight;
			}

			bool ok = false;
			{
				FURY_ZONE_NAMED("AssetLoader::Read");
				std::vector<unsigned char> bytes;
				ok = AssetBackend::ReadAssetBytes(req.path, bytes);
				if (ok)
				{
					std::lock_guard<std::mutex> lock(m_CacheMutex);
					m_Cache[req.path] = std::move(bytes);
				}
			}

			{
				std::lock_guard<std::mutex> lock(m_DoneMutex);
				m_Done.emplace_back(std::move(req), ok);
			}
			{
				std::lock_guard<std::mutex> lock(m_InflightMutex);
				--m_Inflight;
			}
			m_InflightCv.notify_all();
		}
	}
}
