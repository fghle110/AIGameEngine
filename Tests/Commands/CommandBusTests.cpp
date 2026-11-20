#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Commands/CommandBus.h"
#include "Aegis/Core/Json.h"

#include <string>
#include <utility>

using namespace aegis;

namespace
{
class ScopedCommand
{
public:
	ScopedCommand(CommandInfo commandInfo, CommandBus::Handler handler) : m_Name(commandInfo.Name)
	{
		CommandBus& bus = CommandBus::Get();
		bus.Unregister(m_Name);
		m_Registered = bus.Register(std::move(commandInfo), std::move(handler));
	}

	~ScopedCommand()
	{
		if (m_Registered)
		{
			CommandBus::Get().Unregister(m_Name);
		}
	}

	ScopedCommand(const ScopedCommand&) = delete;
	ScopedCommand& operator=(const ScopedCommand&) = delete;

	bool IsRegistered() const
	{
		return m_Registered;
	}

private:
	std::string m_Name;
	bool m_Registered = false;
};

CommandInfo makeEchoCommand(std::string name, std::string description)
{
	CommandInfo info;
	info.Name = std::move(name);
	info.Category = "test";
	info.Description = std::move(description);
	info.Parameters.push_back({ "value", "number", true, "Value to echo" });
	return info;
}

JsonValue makeEchoParams(f64 value)
{
	JsonValue params = JsonValue::MakeObject();
	params.Set("value", value);
	return params;
}

CommandBus::Handler echoHandler()
{
	return [](const JsonValue& params)
	{
		JsonValue result = JsonValue::MakeObject();
		result.Set("value", params.GetNumber("value"));
		return CommandResult::Ok(result);
	};
}
} // namespace

TEST_CASE("CommandBus: registration exposes metadata and parameters")
{
	CommandBus& bus = CommandBus::Get();
	ScopedCommand command(makeEchoCommand("test.bus.metadata", "Echoes a number"), echoHandler());
	REQUIRE(command.IsRegistered());
	CHECK(bus.HasCommand("test.bus.metadata"));

	const JsonValue described = bus.DescribeCommands();
	bool found = false;
	for (usize index = 0; index < described.GetSize(); ++index)
	{
		const JsonValue& entry = described[index];
		if (entry.GetString("name") != "test.bus.metadata")
		{
			continue;
		}
		found = true;
		CHECK(entry.GetString("category") == "test");
		CHECK(entry.GetString("description") == "Echoes a number");
		CHECK_FALSE(entry.GetBool("undoable"));

		const JsonValue& parameters = entry["parameters"];
		REQUIRE(parameters.IsArray());
		REQUIRE(parameters.GetSize() == 1);
		CHECK(parameters[0].GetString("name") == "value");
		CHECK(parameters[0].GetString("type") == "number");
		CHECK(parameters[0].GetBool("required"));
		CHECK(parameters[0].GetString("description") == "Value to echo");
	}
	CHECK(found);
}

TEST_CASE("CommandBus: executes registered commands with parameters")
{
	ScopedCommand command(makeEchoCommand("test.bus.echo", "Echoes a number"), echoHandler());
	const CommandResult result = CommandBus::Get().Execute("test.bus.echo", makeEchoParams(7.5));
	REQUIRE(result.Success);
	CHECK(result.Value.GetNumber("value") == doctest::Approx(7.5));
}

TEST_CASE("CommandBus: unknown commands fail with a helpful error")
{
	const CommandResult direct = CommandBus::Get().Execute("test.bus.missing", JsonValue());
	CHECK_FALSE(direct.Success);
	CHECK(direct.Error.find("test.bus.missing") != std::string::npos);

	JsonValue request = JsonValue::MakeObject();
	request.Set("command", "test.bus.missing");
	const JsonValue response = CommandBus::Get().ExecuteJson(request);
	CHECK_FALSE(response.GetBool("ok", true));
	CHECK(response.GetString("error").find("test.bus.missing") != std::string::npos);

	const JsonValue missingField = CommandBus::Get().ExecuteJson(JsonValue::MakeObject());
	CHECK_FALSE(missingField.GetBool("ok", true));
	CHECK(missingField.GetString("error").find("command") != std::string::npos);
}

TEST_CASE("CommandBus: JSON requests return ok, command and result")
{
	ScopedCommand command(makeEchoCommand("test.bus.json", "Echoes a number"), echoHandler());

	JsonValue request = JsonValue::MakeObject();
	request.Set("command", "test.bus.json");
	request.Set("params", makeEchoParams(3.25));
	const JsonValue response = CommandBus::Get().ExecuteJson(request);

	CHECK(response.GetBool("ok"));
	CHECK(response.GetString("command") == "test.bus.json");
	CHECK(response["result"].GetNumber("value") == doctest::Approx(3.25));
}

TEST_CASE("CommandBus: duplicate registration is rejected")
{
	ScopedCommand first(makeEchoCommand("test.bus.duplicate", "First"), echoHandler());
	REQUIRE(first.IsRegistered());

	const bool secondRegistered =
	    CommandBus::Get().Register(makeEchoCommand("test.bus.duplicate", "Second"), echoHandler());
	CHECK_FALSE(secondRegistered);

	const JsonValue described = CommandBus::Get().DescribeCommands();
	for (usize index = 0; index < described.GetSize(); ++index)
	{
		const JsonValue& entry = described[index];
		if (entry.GetString("name") == "test.bus.duplicate")
		{
			CHECK(entry.GetString("description") == "First");
		}
	}
}

TEST_CASE("CommandBus: unregister removes a command")
{
	ScopedCommand command(makeEchoCommand("test.bus.unregister", "Echoes a number"), echoHandler());
	REQUIRE(command.IsRegistered());
	CHECK(CommandBus::Get().HasCommand("test.bus.unregister"));
	CHECK(CommandBus::Get().Unregister("test.bus.unregister"));
	CHECK_FALSE(CommandBus::Get().HasCommand("test.bus.unregister"));
	CHECK_FALSE(CommandBus::Get().Unregister("test.bus.unregister"));
}

TEST_CASE("CommandBus: history records successful and failed requests")
{
	CommandBus& bus = CommandBus::Get();
	bus.ClearHistory();
	{
		ScopedCommand command(makeEchoCommand("test.bus.history", "Echoes a number"), echoHandler());
		REQUIRE(command.IsRegistered());
		REQUIRE(bus.Execute("test.bus.history", makeEchoParams(1.0)).Success);
	}
	CHECK_FALSE(bus.Execute("test.bus.history.missing", JsonValue()).Success);

	const JsonValue history = bus.GetHistory();
	REQUIRE(history.IsArray());
	// Unknown command names are rejected before a handler exists, so they are not
	// part of the execution history: only commands that actually ran are logged.
	REQUIRE(history.GetSize() == 1);
	CHECK(history[0].GetString("command") == "test.bus.history");
	CHECK(history[0].GetBool("ok"));
	CHECK(history[0].Contains("result"));

	bus.ClearHistory();
	CHECK(bus.GetHistory().GetSize() == 0);
}
