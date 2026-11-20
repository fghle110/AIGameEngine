#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/Json.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace aegis
{
	struct CommandResult
	{
		bool Success = false;
		JsonValue Value;
		std::string Error;

		static CommandResult Ok(JsonValue value = JsonValue())
		{
			CommandResult result;
			result.Success = true;
			result.Value = std::move(value);
			return result;
		}

		static CommandResult Fail(std::string error)
		{
			CommandResult result;
			result.Success = false;
			result.Error = std::move(error);
			return result;
		}
	};

	struct CommandParameter
	{
		std::string Name;
		std::string Type;
		bool Required = false;
		std::string Description;
	};

	struct CommandInfo
	{
		std::string Name;
		std::string Category;
		std::string Description;
		bool Undoable = false;
		std::vector<CommandParameter> Parameters;
	};

	// Single source of truth for editor actions. The editor UI and the AI HTTP API
	// both go through Execute(), so automation has exactly the same capabilities as
	// a human. Commands are registered during editor startup; handlers must only be
	// called from the main thread (the HTTP server marshals requests there).
	class CommandBus
	{
	public:
		using Handler = std::function<CommandResult(const JsonValue& parameters)>;

		static CommandBus& Get();

		bool Register(CommandInfo info, Handler handler);
		// Removes a command. Re-registering an editor command set uses this so the
		// handlers always reference the current editor context.
		bool Unregister(std::string_view name);
		bool HasCommand(std::string_view name) const;

		CommandResult Execute(std::string_view name, const JsonValue& parameters = JsonValue());
		JsonValue ExecuteJson(const JsonValue& request);

		std::vector<CommandInfo> GetCommands() const;
		JsonValue DescribeCommands() const;

		// Recent request/response log, newest last (used by the console panel).
		JsonValue GetHistory() const;
		void ClearHistory();

	private:
		struct Entry
		{
			CommandInfo Info;
			Handler HandlerFn;
		};

		mutable std::mutex m_Mutex;
		std::map<std::string, Entry, std::less<>> m_Commands;
		std::vector<JsonValue> m_History;
	};
}
