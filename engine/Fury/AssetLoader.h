#ifndef _FURY_ASSET_LOADER_H_
#define _FURY_ASSET_LOADER_H_

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Macros.h"

namespace fury
{
	// Async asset byte reads. Worker threads perform backend reads (pak or
	// disk + LZ4 decompress); completions are delivered on the main thread
	// via Pump(). Prefetch submissions fill a take-out cache that
	// AssetBackend::ReadAssetBytes consults first, so a scene load can
	// queue every texture up front, Flush, and have each subsequent sync
	// load consume warm bytes - parallel IO without changing load
	// semantics. Workers never touch GL.
	class FURY_API AssetLoader final
	{
	public:

		using DoneCallback = std::function<void(const std::string& path, bool ok)>;

		static AssetLoader& Get();

		// Defensive join at static teardown: Lua os.exit() runs static
		// destructors without unwinding Cli::Run, so a still-joinable
		// worker would otherwise std::terminate.
		~AssetLoader() { Shutdown(); }

		void Initialize(int workers = 1);
		void Shutdown();
		bool IsUp() const;

		// Async read; cb(path, ok) runs on the main thread during Pump.
		// Bytes land in the take-cache regardless (same as Prefetch).
		void Submit(const std::string& resolvedPath, int priority, DoneCallback cb);

		// Queue a background read; bytes land in the take-cache.
		void Prefetch(const std::string& resolvedPath, int priority = 0);

		// Move out prefetched bytes; false when not (yet) completed.
		bool TakePrefetched(const std::string& resolvedPath, std::vector<unsigned char>& out);

		// Run due completion callbacks. Main thread, once per frame.
		void Pump();

		// Block the calling thread until queue + inflight drain, then Pump.
		void Flush();

		size_t PendingCount();

	private:

		struct Request
		{
			std::string path;
			int priority = 0;
			DoneCallback cb; // empty for pure prefetch
		};

		void WorkerMain();

		std::mutex m_QueueMutex;
		std::condition_variable m_QueueCv;
		std::deque<Request> m_Queue;

		std::mutex m_DoneMutex;
		std::vector<std::pair<Request, bool>> m_Done;

		std::mutex m_CacheMutex;
		std::unordered_map<std::string, std::vector<unsigned char>> m_Cache;

		std::mutex m_InflightMutex;
		std::condition_variable m_InflightCv;
		size_t m_Inflight = 0;

		std::vector<std::thread> m_Workers;
		bool m_Stop = false;
	};
}

#endif // _FURY_ASSET_LOADER_H_
