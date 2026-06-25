#include "Fury/Editor/EditorLog.h"

#include <cstring>

namespace fury
{
	namespace Editor
	{
		namespace
		{
			std::mutex g_Mutex;
			std::deque<LogEntry> g_Entries;
		}

		void LogBuffer::Push(LogLevel lvl, std::string text)
		{
			std::lock_guard<std::mutex> g(g_Mutex);
			if (g_Entries.size() >= Capacity)
				g_Entries.pop_front();
			g_Entries.push_back({lvl, std::move(text)});
		}

		void LogBuffer::Clear()
		{
			std::lock_guard<std::mutex> g(g_Mutex);
			g_Entries.clear();
		}

		std::deque<LogEntry> LogBuffer::Snapshot() const
		{
			std::lock_guard<std::mutex> g(g_Mutex);
			return g_Entries;
		}

		std::size_t LogBuffer::Size() const
		{
			std::lock_guard<std::mutex> g(g_Mutex);
			return g_Entries.size();
		}

		LogBuffer& GlobalLogBuffer()
		{
			static LogBuffer s;
			return s;
		}

		LogLevel LevelFromString(const char* s)
		{
			if (s == nullptr) return LogLevel::Info;
			if (std::strcmp(s, "warn") == 0)     return LogLevel::Warn;
			if (std::strcmp(s, "error") == 0)    return LogLevel::Error;
			if (std::strcmp(s, "debug") == 0)    return LogLevel::Debug;
			if (std::strcmp(s, "critical") == 0) return LogLevel::Critical;
			return LogLevel::Info;
		}
	}
}
