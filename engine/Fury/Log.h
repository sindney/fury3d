#ifndef _FURY_LOG_H_
#define _FURY_LOG_H_

// Inspired by plog: 
// https://github.com/SergiusTheBest/plog
// Implementation referenced to: 
// http://stackoverflow.com/questions/8337300/c11-how-do-i-implement-convenient-logging-without-a-singleton 

#include <iostream>
#include <ostream>
#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <chrono>

#include "Fury/ThreadUtil.h"
#include "Fury/Singleton.h"

namespace fury
{
	enum class FURY_API LogLevel : int
	{
		EROR = 0,
		WARN = 1,
		INFO = 2,
		DBUG = 3
	};

	// C4251: STL members (std::string, std::stringstream) in an exported class --
// see the matching note in ThreadUtil.h.
#if PLATFORM_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4251)
#endif
	class FURY_API Record
	{
	public:

		std::string level;

		std::string func;

		std::string file;

		size_t line = 0;

		std::stringstream stream;

		Record(LogLevel level, const char* function, const char* file, int line)
			: func(function), file(file), line(line)
		{
			switch (level)
			{
			case LogLevel::DBUG:
				this->level = "DBUG";
				break;
			case LogLevel::INFO:
				this->level = "INFO";
				break;
			case LogLevel::WARN:
				this->level = "WARN";
				break;
			case LogLevel::EROR:
				this->level = "EROR";
				break;
			}

			auto start = func.find(' ') + 1;
			auto end = func.find('(');
			func = func.substr(start, end - start);

#ifdef _MSC_VER
			start = this->file.find_last_of('\\');
#else
			start = this->file.find_last_of('/');
#endif
			this->file = this->file.substr(start + 1);
		}

		template<typename T>
		Record& operator << (const T& data)
		{
			stream << data;
			stream.flush();
			return *this;
		}
	};
#if PLATFORM_WINDOWS
#pragma warning(pop)
#endif

	struct FURY_API Formatter
	{
		static void Simple(std::ostream& stream, const Record &record)
		{
			stream << "[" << record.level << "]";

			if (!ThreadUtil::Instance()->IsMainThread())
				stream << "[" << std::this_thread::get_id() << "]";

			stream << "[" << record.func << "][" << record.line << "]: ";
		};

		static void Default(std::ostream& stream, const Record &record)
		{
			stream << "[" << record.level << "]";

			if (!ThreadUtil::Instance()->IsMainThread())
				stream << "[" << std::this_thread::get_id() << "]";

			stream << "[" << record.file << "][" << record.func << "][" << record.line << "]: ";
		};
	};

	typedef std::function<void(std::ostream&, const Record&)> LogFormatter;

	// Optional sink invoked alongside the file/console outputs. Used by
	// the editor's Console window to mirror engine log lines into a
	// ring buffer. Single-callback; setting it again replaces. Empty by
	// default (no extra fan-out).
	using LogSink = std::function<void(const Record&)>;

	// thread safe
	template<int instance>
	// C4251: STL members (std::mutex, std::ostream ref captured as pointer, ...)
	// in an exported template class -- see note on Record above.
#if PLATFORM_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4251)
#endif
	class FURY_API Log : public Singleton<Log<instance>, LogLevel, const char*, bool, const LogFormatter&, bool>
	{
	private:

		std::mutex m_StreamMutex;

		std::ostream& m_ConsoleStream = std::cout;

		std::ofstream m_FileStream;

		bool m_FileOutput = false;

		bool m_ConsoleOutput = false;

		// level, file, function, line
		LogFormatter m_Formatter;

		LogLevel m_LogLevel = LogLevel::DBUG;

		LogSink m_ExtraSink;

	public:

		Log(LogLevel level, const char* logfile, bool console, const LogFormatter &formatter, bool append)
			: m_FileOutput(logfile != nullptr), m_ConsoleOutput(console), m_Formatter(formatter), m_LogLevel(level)
		{
			if (m_FileOutput)
			{
				m_FileStream.open(logfile, std::ofstream::out | (append ? std::ostream::app : std::ofstream::trunc));
				ASSERT_MSG(m_FileStream.good(), "Log file not found!");
			}
		}

		virtual ~Log()
		{
			if (m_FileOutput)
				m_FileStream.close();
		}

		void SetLevel(LogLevel level)
		{
			m_LogLevel = level;
		}

		LogLevel GetLevel()
		{
			return m_LogLevel;
		}

		// Install a side-channel sink that's called for every accepted
		// record. Pass an empty std::function to clear. Replaces any
		// previously-installed sink. Thread-safe vs. operator+= because
		// they share the stream mutex.
		void SetExtraSink(LogSink sink)
		{
			std::lock_guard<std::mutex> lock(m_StreamMutex);
			m_ExtraSink = std::move(sink);
		}

		void operator += (const Record& record)
		{
			std::lock_guard<std::mutex> lock(m_StreamMutex);

			if (m_ConsoleOutput)
			{
				m_Formatter(m_ConsoleStream, record);
				m_ConsoleStream << record.stream.str() << "\n";
			}

			if (m_FileOutput)
			{
				m_Formatter(m_FileStream, record);
				m_FileStream << record.stream.str() << "\n";
				m_FileStream.flush();
			}

			if (m_ExtraSink)
			{
				m_ExtraSink(record);
			}
		}
	};
#if PLATFORM_WINDOWS
#pragma warning(pop)
#endif
}

#ifdef _MSC_VER
#define FURY_FUNC_NAME __FUNCTION__
#else
#define FURY_FUNC_NAME __PRETTY_FUNCTION__
#endif

#define FURY_LOG_IF(instance, level)	if (fury::Log<instance>::Instance() && level <= fury::Log<instance>::Instance()->GetLevel())
#define FURY_LOG(instance, level) 		FURY_LOG_IF(instance, level) *fury::Log<instance>::Instance() += fury::Record(level, FURY_FUNC_NAME, __FILE__, __LINE__)

#define FURY_DEBUG(instance) 			FURY_LOG(instance, fury::LogLevel::DBUG)
#define FURY_INFO(instance) 			FURY_LOG(instance, fury::LogLevel::INFO)
#define FURY_WARN(instance) 			FURY_LOG(instance, fury::LogLevel::WARN)
#define FURY_ERROR(instance) 			FURY_LOG(instance, fury::LogLevel::EROR)

#define FURYD_(instance) 				FURY_DEBUG(instance)
#define FURYI_(instance) 				FURY_INFO(instance)
#define FURYW_(instance) 				FURY_WARN(instance)
#define FURYE_(instance) 				FURY_ERROR(instance)

#define FURYD 							FURYD_(0)
#define FURYI							FURYI_(0)
#define FURYW							FURYW_(0)
#define FURYE 							FURYE_(0)

#endif // _FURY_LOG_H_