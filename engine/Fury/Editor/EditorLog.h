#ifndef _FURY_EDITOR_LOG_H_
#define _FURY_EDITOR_LOG_H_

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

#include "Fury/Macros.h"

namespace fury
{
	namespace Editor
	{
		enum class LogLevel : int
		{
			Debug = 0,
			Info,
			Warn,
			Error,
			Critical
		};

		struct LogEntry
		{
			LogLevel level;
			std::string text;
		};

		// Fixed-capacity ring (4096) thread-safe log sink. Hooked into the
		// engine's FURYI/W/E/D macros via Log.h's LogFormatter so engine-
		// emitted lines surface in the editor Console window.
		class LogBuffer
		{
		public:
			static constexpr std::size_t Capacity = 4096;

			void Push(LogLevel lvl, std::string text);
			void Clear();

			// Snapshot copy under the mutex so the renderer doesn't race
			// against concurrent Push() calls. Cheap enough at our scale
			// that a per-frame copy keeps the renderer code lock-free.
			std::deque<LogEntry> Snapshot() const;

			std::size_t Size() const;
		};

		LogBuffer& GlobalLogBuffer();

		// Translate "info" / "warn" / "error" / "debug" / "critical" into
		// the corresponding LogLevel. Falls back to Info for unknown labels.
		LogLevel LevelFromString(const char* s);
	}
}

#endif // _FURY_EDITOR_LOG_H_
