#include "Aegis/Core/Log.h"

#include "Aegis/Core/Config.h"
#include "Aegis/Core/FileSystem.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <utility>

#if AE_PLATFORM_WINDOWS
	#include <windows.h>
	#include <io.h>
#else
	#include <unistd.h>
#endif

namespace aegis
{
	std::string_view LogLevelToString(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Trace: return "TRACE";
			case LogLevel::Debug: return "DEBUG";
			case LogLevel::Info: return "INFO";
			case LogLevel::Warn: return "WARN";
			case LogLevel::Error: return "ERROR";
			case LogLevel::Critical: return "CRITICAL";
			case LogLevel::Off: break;
		}
		return "UNKNOWN";
	}

	std::string_view LogLevelToAnsiColor(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Trace: return "\x1b[90m";   // bright black
			case LogLevel::Debug: return "\x1b[36m";   // cyan
			case LogLevel::Info: return "\x1b[37m";    // white
			case LogLevel::Warn: return "\x1b[33m";    // yellow
			case LogLevel::Error: return "\x1b[31m";   // red
			case LogLevel::Critical: return "\x1b[1;31m"; // bold red
			case LogLevel::Off: break;
		}
		return "";
	}

	namespace
	{
		constexpr usize c_MaxRecentMessages = 4096;
		constexpr std::string_view c_ResetColor = "\x1b[0m";

		struct LogState
		{
			std::mutex Mutex;
			std::vector<Ref<LogSink>> Sinks;
			std::deque<LogMessage> Recent;
			std::array<u64, static_cast<usize>(LogLevel::Off)> Counters{};
#if defined(AE_DEBUG)
			std::atomic<LogLevel> Level = LogLevel::Trace;
#else
			std::atomic<LogLevel> Level = LogLevel::Info;
#endif
			bool Initialized = false;
		};

		LogState& State()
		{
			static LogState s_State;
			return s_State;
		}

		std::string MakeTimestamp(bool shortForm, std::chrono::system_clock::time_point time)
		{
			const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
			std::tm tm{};
#if AE_PLATFORM_WINDOWS
			localtime_s(&tm, &seconds);
#else
			localtime_r(&seconds, &tm);
#endif
			const auto millis =
				std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;

			char buffer[40]{};
			if (shortForm)
			{
				std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
					static_cast<i32>(millis));
			}
			else
			{
				std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900,
					tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<i32>(millis));
			}
			return buffer;
		}

		class ConsoleSink final : public LogSink
		{
		public:
			ConsoleSink()
			{
#if AE_PLATFORM_WINDOWS
				// Enable ANSI escape sequences on the attached console (Windows 10+).
				for (const DWORD stdHandleId : { STD_OUTPUT_HANDLE, STD_ERROR_HANDLE })
				{
					const HANDLE handle = ::GetStdHandle(stdHandleId);
					if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
					{
						continue;
					}
					DWORD mode = 0;
					if (::GetConsoleMode(handle, &mode) != 0)
					{
						::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
					}
				}
				m_UseColor = ::_isatty(_fileno(stdout)) != 0;
#else
				m_UseColor = ::isatty(STDOUT_FILENO) != 0;
#endif
			}

			void Write(const LogMessage& message) override
			{
				FILE* stream = message.Level >= LogLevel::Error ? stderr : stdout;
				if (m_UseColor)
				{
					std::fprintf(stream, "%s%s %-8s \x1b[90m[%s]\x1b[0m %s%s\x1b[0m\n",
						LogLevelToAnsiColor(message.Level).data(), message.Timestamp.c_str(),
						LogLevelToString(message.Level).data(), message.Logger.c_str(), message.Message.c_str(),
						c_ResetColor.data());
				}
				else
				{
					std::fprintf(stream, "%s %-8s [%s] %s\n", message.Timestamp.c_str(),
						LogLevelToString(message.Level).data(), message.Logger.c_str(), message.Message.c_str());
				}
				if (message.Level >= LogLevel::Warn)
				{
					std::fflush(stream);
				}
			}

			void Flush() override
			{
				std::fflush(stdout);
				std::fflush(stderr);
			}

		private:
			bool m_UseColor = false;
		};

		class FileSink final : public LogSink
		{
		public:
			explicit FileSink(const std::filesystem::path& path)
			{
				FileSystem::EnsureParentDirectory(path);
				m_Stream.open(path, std::ios::out | std::ios::trunc);
				if (!m_Stream.is_open())
				{
					std::fprintf(stderr, "Aegis: could not open log file '%s'\n", path.string().c_str());
				}
			}

			void Write(const LogMessage& message) override
			{
				if (!m_Stream.is_open())
				{
					return;
				}
				m_Stream << message.Timestamp << " " << LogLevelToString(message.Level) << " [" << message.Logger << "] "
						 << message.Message << '\n';
				if (message.Level >= LogLevel::Warn)
				{
					m_Stream.flush();
				}
			}

			void Flush() override
			{
				if (m_Stream.is_open())
				{
					m_Stream.flush();
				}
			}

		private:
			std::ofstream m_Stream;
		};
	}

	void Log::Init(std::string_view logDirectory)
	{
		// Preserve the current level so a re-init (e.g. project switch) keeps it.
		LogLevel previousLevel;
		{
			auto& state = State();
			std::lock_guard lock(state.Mutex);
			previousLevel = state.Level.load(std::memory_order_relaxed);
		}

		ResetLocked();

		AddSink(CreateRef<ConsoleSink>());

		if (!logDirectory.empty())
		{
			const auto directory = FileSystem::FromUtf8(logDirectory);
			const auto now = std::chrono::system_clock::now();
			const std::string stamp = MakeTimestamp(false, now);
			std::string fileStamp = stamp;
			for (char& c : fileStamp)
			{
				if (c == ':' || c == ' ')
				{
					c = '-';
				}
				else if (c == '.')
				{
					c = '_';
				}
			}
			const auto logPath = directory / ("Aegis-" + fileStamp + ".log");
			if (FileSystem::CreateDirectories(directory))
			{
				AddSink(CreateRef<FileSink>(logPath));
			}
		}

		{
			auto& state = State();
			std::lock_guard lock(state.Mutex);
			state.Level.store(previousLevel, std::memory_order_relaxed);
			state.Initialized = true;
		}

#if defined(AE_DIST)
		constexpr std::string_view c_BuildType = "dist";
#elif defined(AE_DEBUG)
		constexpr std::string_view c_BuildType = "debug";
#else
		constexpr std::string_view c_BuildType = "release";
#endif
		AE_CORE_INFO("{} {} initialized ({} build)", c_EngineName, c_EngineVersion, c_BuildType);
	}

	void Log::Shutdown()
	{
		std::lock_guard lock(State().Mutex);
		for (const auto& sink : State().Sinks)
		{
			sink->Flush();
		}
		State().Sinks.clear();
		State().Recent.clear();
		State().Initialized = false;
	}

	bool Log::IsEnabled(LogLevel level)
	{
		return level != LogLevel::Off && level >= State().Level.load(std::memory_order_relaxed);
	}

	LogLevel Log::GetLevel()
	{
		return State().Level.load(std::memory_order_relaxed);
	}

	void Log::SetLevel(LogLevel level)
	{
		State().Level.store(level, std::memory_order_relaxed);
	}

	void Log::AddSink(Ref<LogSink> sink)
	{
		if (!sink)
		{
			return;
		}
		auto& state = State();
		std::lock_guard lock(state.Mutex);
		state.Sinks.push_back(std::move(sink));
	}

	void Log::RemoveAllSinks()
	{
		auto& state = State();
		std::lock_guard lock(state.Mutex);
		state.Sinks.clear();
	}

	void Log::PushRecentLocked(const LogMessage& message)
	{
		auto& state = State();
		state.Recent.push_back(message);
		while (state.Recent.size() > c_MaxRecentMessages)
		{
			state.Recent.pop_front();
		}
	}

	void Log::Write(LogLevel level, std::string_view logger, std::string_view message)
	{
		if (!IsEnabled(level))
		{
			return;
		}

		LogMessage entry;
		entry.Level = level;
		entry.Logger = std::string(logger);
		entry.Message = std::string(message);
		entry.Time = std::chrono::system_clock::now();
		entry.Timestamp = MakeTimestamp(true, entry.Time);

		auto& state = State();
		std::lock_guard lock(state.Mutex);
		state.Counters[static_cast<usize>(level)]++;
		PushRecentLocked(entry);
		for (const auto& sink : state.Sinks)
		{
			sink->Write(entry);
		}
	}

	std::vector<LogMessage> Log::GetRecentMessages()
	{
		auto& state = State();
		std::lock_guard lock(state.Mutex);
		return { state.Recent.begin(), state.Recent.end() };
	}

	void Log::ClearRecentMessages()
	{
		auto& state = State();
		std::lock_guard lock(state.Mutex);
		state.Recent.clear();
	}

	u64 Log::GetMessageCount(LogLevel level)
	{
		if (level == LogLevel::Off)
		{
			return 0;
		}
		auto& state = State();
		std::lock_guard lock(state.Mutex);
		return state.Counters[static_cast<usize>(level)];
	}

	void Log::ResetLocked()
	{
		auto& state = State();
		std::lock_guard lock(state.Mutex);
		for (const auto& sink : state.Sinks)
		{
			sink->Flush();
		}
		state.Sinks.clear();
	}

	namespace detail
	{
		void AssertFailed(const char* expression, std::string_view message, const char* file, int line)
		{
			const std::string text = !message.empty()
										 ? std::format("Assertion failed: {} at {}:{} ({})", expression, file, line,
											 message)
										 : std::format("Assertion failed: {} at {}:{}", expression, file, line);
			Log::Write(LogLevel::Critical, "Assert", text);
			AE_DEBUGBREAK();
			std::abort();
		}
	}
}
