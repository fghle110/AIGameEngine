#include "Aegis/Commands/CommandBus.h"

#include "Aegis/Core/Log.h"

namespace aegis
{
	namespace
	{
		JsonValue ParametersToJson(const CommandInfo& info)
		{
			JsonValue parameters = JsonValue::MakeArray();
			for (const CommandParameter& parameter : info.Parameters)
			{
				JsonValue entry = JsonValue::MakeObject();
				entry.Set("name", parameter.Name);
				entry.Set("type", parameter.Type);
				entry.Set("required", parameter.Required);
				entry.Set("description", parameter.Description);
				parameters.Push(std::move(entry));
			}
			return parameters;
		}
	}

	CommandBus& CommandBus::Get()
	{
		static CommandBus s_Bus;
		return s_Bus;
	}

	bool CommandBus::Register(CommandInfo info, Handler handler)
	{
		if (info.Name.empty() || !handler)
		{
			AE_CORE_ERROR("Cannot register an unnamed command or a command without a handler");
			return false;
		}

		std::lock_guard lock(m_Mutex);
		Entry entry;
		entry.Info = std::move(info);
		entry.HandlerFn = std::move(handler);
		const auto [it, inserted] = m_Commands.emplace(entry.Info.Name, std::move(entry));
		if (!inserted)
		{
			AE_CORE_ERROR("Command '{}' is already registered", it->second.Info.Name);
		}
		return inserted;
	}

	bool CommandBus::HasCommand(std::string_view name) const
	{
		std::lock_guard lock(m_Mutex);
		return m_Commands.find(name) != m_Commands.end();
	}

	CommandResult CommandBus::Execute(std::string_view name, const JsonValue& parameters)
	{
		Handler handler;
		{
			std::lock_guard lock(m_Mutex);
			const auto it = m_Commands.find(name);
			if (it == m_Commands.end())
			{
				return CommandResult::Fail(std::format("Unknown command '{}'", name));
			}
			handler = it->second.HandlerFn;
		}

		// Run outside the registry lock: handlers may register or query commands.
		CommandResult result = handler(parameters);
		if (!result.Success && result.Error.empty())
		{
			result.Error = "Command failed without an error message";
		}

		JsonValue historyEntry = JsonValue::MakeObject();
		historyEntry.Set("command", std::string(name));
		historyEntry.Set("params", parameters);
		historyEntry.Set("ok", result.Success);
		if (result.Success)
		{
			historyEntry.Set("result", result.Value);
		}
		else
		{
			historyEntry.Set("error", result.Error);
		}

		{
			std::lock_guard lock(m_Mutex);
			m_History.push_back(std::move(historyEntry));
			constexpr usize c_MaxHistory = 256;
			if (m_History.size() > c_MaxHistory)
			{
				m_History.erase(m_History.begin(), m_History.begin() + static_cast<isize>(m_History.size() - c_MaxHistory));
			}
		}

		if (!result.Success)
		{
			AE_CORE_WARN("Command '{}' failed: {}", name, result.Error);
		}
		return result;
	}

	JsonValue CommandBus::ExecuteJson(const JsonValue& request)
	{
		const std::string command = request.GetString("command");
		if (command.empty())
		{
			JsonValue response = JsonValue::MakeObject();
			response.Set("ok", false);
			response.Set("error", "Missing 'command' field");
			return response;
		}

		const JsonValue* parameters = request.Find("params");
		const CommandResult result = Execute(command, parameters != nullptr ? *parameters : JsonValue());

		JsonValue response = JsonValue::MakeObject();
		response.Set("ok", result.Success);
		response.Set("command", command);
		if (result.Success)
		{
			response.Set("result", result.Value);
		}
		else
		{
			response.Set("error", result.Error);
		}
		return response;
	}

	std::vector<CommandInfo> CommandBus::GetCommands() const
	{
		std::lock_guard lock(m_Mutex);
		std::vector<CommandInfo> commands;
		commands.reserve(m_Commands.size());
		for (const auto& [name, entry] : m_Commands)
		{
			commands.push_back(entry.Info);
		}
		return commands;
	}

	JsonValue CommandBus::DescribeCommands() const
	{
		std::lock_guard lock(m_Mutex);
		JsonValue commands = JsonValue::MakeArray();
		for (const auto& [name, entry] : m_Commands)
		{
			JsonValue command = JsonValue::MakeObject();
			command.Set("name", entry.Info.Name);
			command.Set("category", entry.Info.Category);
			command.Set("description", entry.Info.Description);
			command.Set("undoable", entry.Info.Undoable);
			command.Set("parameters", ParametersToJson(entry.Info));
			commands.Push(std::move(command));
		}
		return commands;
	}

	JsonValue CommandBus::GetHistory() const
	{
		std::lock_guard lock(m_Mutex);
		JsonValue history = JsonValue::MakeArray();
		for (const JsonValue& entry : m_History)
		{
			history.Push(entry);
		}
		return history;
	}

	void CommandBus::ClearHistory()
	{
		std::lock_guard lock(m_Mutex);
		m_History.clear();
	}
}
