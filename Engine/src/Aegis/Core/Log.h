#pragma once

// Thread-safe logger with console, file and in-memory (editor console) sinks.
// Formatting uses std::format and only happens when the level is enabled.

#include "Aegis/Core/Base.h"

#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace aegis
{
	enum class LogLevel : u8
	{
		Trace = 0,
		Debug,
		Info,
		Warn,
		Error,
		Critical,
		Off
	};

	std::string_view LogLevelToString(LogLevel level);
	std::string_view LogLevelToAnsiColor(LogLevel level);

	struct LogMessage
	{
		LogLevel Level = LogLevel::Info;
		std::string Logger;
		std::string Message;
		std::string Timestamp;
		std::chrono::system_clock::time_point Time;
	};

	class LogSink
	{
	public:
		virtual ~LogSink() = default;
		virtual void Write(const LogMessage& message) = 0;
		virtual void Flush() {}
	};

	class Log
	{
	public:
		// Installs the console sink and a timestamped file in logDirectory (when
		// the directory can be created). Safe to call more than once.
		static void Init(std::string_view logDirectory = "logs");
		static void Shutdown();

		static bool IsEnabled(LogLevel level);
		static LogLevel GetLevel();
		static void SetLevel(LogLevel level);

		static void Write(LogLevel level, std::string_view logger, std::string_view message);

		static void AddSink(Ref<LogSink> sink);
		static void RemoveAllSinks();

		// Most recent messages, oldest first. Used by the editor console panel.
		static std::vector<LogMessage> GetRecentMessages();
		static void ClearRecentMessages();

		static u64 GetMessageCount(LogLevel level);

	private:
		static void ResetLocked();
		static void PushRecentLocked(const LogMessage& message);
	};

	namespace detail
	{
		inline std::string FormatLogString()
		{
			return {};
		}

		template<typename... Args>
		std::string FormatLogString(std::format_string<Args...> format, Args&&... args)
		{
			return std::format(format, std::forward<Args>(args)...);
		}
	}
}

// Engine internals.
#define AE_CORE_TRACE(...)                                                                                             \
	do                                                                                                                 \
	{                                                                                                                  \
		if (::aegis::Log::IsEnabled(::aegis::LogLevel::Trace))                                                         \
		{                                                                                                              \
			::aegis::Log::Write(::aegis::LogLevel::Trace, "Aegis", ::aegis::detail::FormatLogString(__VA_ARGS__));     \
		}                                                                                                              \
	} while (false)

#define AE_CORE_DEBUG(...)                                                                                             \
	do                                                                                                                 \
	{                                                                                                                  \
		if (::aegis::Log::IsEnabled(::aegis::LogLevel::Debug))                                                         \
		{                                                                                                              \
			::aegis::Log::Write(::aegis::LogLevel::Debug, "Aegis", ::aegis::detail::FormatLogString(__VA_ARGS__));     \
		}                                                                                                              \
	} while (false)

#define AE_CORE_INFO(...)                                                                                              \
	do                                                                                                                 \
	{                                                                                                                  \
		if (::aegis::Log::IsEnabled(::aegis::LogLevel::Info))                                                          \
		{                                                                                                              \
			::aegis::Log::Write(::aegis::LogLevel::Info, "Aegis", ::aegis::detail::FormatLogString(__VA_ARGS__));      \
		}                                                                                                              \
	} while (false)

#define AE_CORE_WARN(...)                                                                                              \
	do                                                                                                                 \
	{                                                                                                                  \
		if (::aegis::Log::IsEnabled(::aegis::LogLevel::Warn))                                                          \
		{                                                                                                              \
			::aegis::Log::Write(::aegis::LogLevel::Warn, "Aegis", ::aegis::detail::FormatLogString(__VA_ARGS__));      \
		}                                                                                                              \
	} while (false)

#define AE_CORE_ERROR(...)                                                                                             \
	do                                                                                                                 \
	{                                                                                                                  \
		if (::aegis::Log::IsEnabled(::aegis::LogLevel::Error))                                                         \
		{                                                                                                              \
			::aegis::Log::Write(::aegis::LogLevel::Error, "Aegis", ::aegis::detail::FormatLogString(__VA_ARGS__));     \
		}                                                                                                              \
	} while (false)

#define AE_CORE_CRITICAL(...)                                                                                          \
	do                                                                                                                 \
	{                                                                                                                  \
		if (::aegis::Log::IsEnabled(::aegis::LogLevel::Critical))                                                      \
		{                                                                                                              \
			::aegis::Log::Write(::aegis::LogLevel::Critical, "Aegis", ::aegis::detail::FormatLogString(__VA_ARGS__));  \
		}                                                                                                              \
	} while (false)

// Application / game code. Prefixed with AE_LOG_ so the log macros never collide
// with build configuration macros such as AE_DEBUG.
#define AE_LOG_TRACE(...)    AE_CORE_TRACE(__VA_ARGS__)
#define AE_LOG_DEBUG(...)    AE_CORE_DEBUG(__VA_ARGS__)
#define AE_LOG_INFO(...)     AE_CORE_INFO(__VA_ARGS__)
#define AE_LOG_WARN(...)     AE_CORE_WARN(__VA_ARGS__)
#define AE_LOG_ERROR(...)    AE_CORE_ERROR(__VA_ARGS__)
#define AE_LOG_CRITICAL(...) AE_CORE_CRITICAL(__VA_ARGS__)
