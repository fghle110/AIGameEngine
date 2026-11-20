#include "Aegis/Core/Config.h"
#include "Aegis/Core/Json.h"

#include "DemoScene.h"
#include "HttpClient.h"

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Project/Project.h"
#include "Aegis/Scene/Scene.h"
#include "Aegis/Serialization/SceneSerializer.h"

#include <cstddef>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
std::string DefaultUrl()
{
	return std::format("http://127.0.0.1:{}", aegis::c_DefaultCommandPort);
}

void PrintUsage()
{
	std::printf("%s command line tool %s\n",
	            std::string(aegis::c_EngineName).c_str(),
	            std::string(aegis::c_EngineVersion).c_str());
	std::printf("\nUsage:\n");
	std::printf("  AegisCli <command> [arguments]\n\n");
	std::printf("Commands:\n");
	std::printf("  version                     Print the engine version\n");
	std::printf("  help                        Print this help\n");
	std::printf("  health [--url <url>]        Check a running editor's AI command server\n");
	std::printf("  commands [--url <url>]      List editor AI commands from a running editor\n");
	std::printf("  call <command> [json] [--url <url>]\n");
	std::printf("                              Invoke an editor AI command; json is the params object\n");
	std::printf("  scene <file>                Validate a scene file\n");
	std::printf("  demo <directory>            Write the FeatureTest project (scene + scripts)\n");
	std::printf("  export <project.aeproj> <dir> Package the runtime and project for distribution\n");
	std::printf("\nURL:\n");
	std::printf("  The default editor URL is %s.\n", DefaultUrl().c_str());
	std::printf("  Pass --url <url> to target another editor, or -h for this help.\n");
	std::printf("\nExamples:\n");
	std::printf("  AegisCli health\n");
	std::printf("  AegisCli commands\n");
	std::printf("  AegisCli call project.info\n");
	std::printf("  AegisCli call scene.createEntity \"{\\\"name\\\":\\\"Player\\\"}\"\n");
}

bool IsUrl(std::string_view value)
{
	return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

std::string JoinUrl(std::string base, std::string_view path)
{
	while (!base.empty() && base.back() == '/')
	{
		base.pop_back();
	}
	return base + std::string(path);
}

struct HttpCliOptions
{
	std::string Url = DefaultUrl();
	std::vector<std::string> Positional;
	std::string Error;
};

bool ParseOptions(const std::vector<std::string>& arguments, HttpCliOptions& outOptions)
{
	outOptions.Url = DefaultUrl();
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		const std::string& argument = arguments[index];
		if (argument == "--url")
		{
			if (index + 1 >= arguments.size())
			{
				outOptions.Error = "--url requires a value";
				return false;
			}
			outOptions.Url = arguments[++index];
		}
		else if (argument.rfind("--url=", 0) == 0)
		{
			outOptions.Url = argument.substr(6);
		}
		else if (!argument.empty() && argument.front() == '-')
		{
			outOptions.Error = std::format("unknown option '{}' (try --url <url>)", argument);
			return false;
		}
		else
		{
			outOptions.Positional.push_back(argument);
		}
	}
	if (outOptions.Url.empty())
	{
		outOptions.Error = "--url must not be empty";
		return false;
	}
	return true;
}

int ReportTransportError(std::string_view action, const aegis::HttpResult& result)
{
	if (!result.Error.empty())
	{
		std::fprintf(stderr, "%s failed: %s\n", std::string(action).c_str(), result.Error.c_str());
		return 1;
	}
	if (result.StatusCode < 200 || result.StatusCode >= 300)
	{
		std::fprintf(stderr, "%s returned HTTP %d\n", std::string(action).c_str(), result.StatusCode);
		if (!result.Body.empty())
		{
			std::fprintf(stderr, "%s\n", result.Body.c_str());
		}
		return 1;
	}
	return 0;
}

void PrintJson(std::string_view text)
{
	aegis::JsonValue::ParseError parseError;
	const std::optional<aegis::JsonValue> document = aegis::JsonValue::Parse(text, &parseError);
	if (!document.has_value())
	{
		std::fputs(std::string(text).c_str(), stdout);
		if (text.empty() || text.back() != '\n')
		{
			std::fputc('\n', stdout);
		}
		return;
	}
	std::printf("%s\n", document->Dump(4).c_str());
}

int RunHealth(const std::vector<std::string>& arguments)
{
	HttpCliOptions options;
	if (!ParseOptions(arguments, options))
	{
		std::fprintf(stderr, "%s\n", options.Error.c_str());
		return 1;
	}
	if (options.Positional.size() == 1 && IsUrl(options.Positional.front()))
	{
		options.Url = options.Positional.front();
	}
	else if (!options.Positional.empty())
	{
		std::fprintf(stderr, "health takes no positional arguments (got '%s')\n", options.Positional.front().c_str());
		return 1;
	}

	const aegis::HttpResult result = aegis::HttpClient::Get(JoinUrl(options.Url, "/health"));
	if (const int status = ReportTransportError("health", result); status != 0)
	{
		return status;
	}
	PrintJson(result.Body);

	const std::optional<aegis::JsonValue> document = aegis::JsonValue::Parse(result.Body);
	return document.has_value() && document->GetBool("ok", false) ? 0 : 2;
}

int RunCommands(const std::vector<std::string>& arguments)
{
	HttpCliOptions options;
	if (!ParseOptions(arguments, options))
	{
		std::fprintf(stderr, "%s\n", options.Error.c_str());
		return 1;
	}
	if (options.Positional.size() == 1 && IsUrl(options.Positional.front()))
	{
		options.Url = options.Positional.front();
	}
	else if (!options.Positional.empty())
	{
		std::fprintf(stderr, "commands takes no positional arguments (got '%s')\n", options.Positional.front().c_str());
		return 1;
	}

	const aegis::HttpResult result = aegis::HttpClient::Get(JoinUrl(options.Url, "/commands"));
	if (const int status = ReportTransportError("commands", result); status != 0)
	{
		return status;
	}
	PrintJson(result.Body);
	return 0;
}

int RunCall(const std::vector<std::string>& arguments)
{
	HttpCliOptions options;
	if (!ParseOptions(arguments, options))
	{
		std::fprintf(stderr, "%s\n", options.Error.c_str());
		return 1;
	}
	if (options.Positional.empty())
	{
		std::fprintf(stderr, "call requires a command name\n");
		return 1;
	}

	const std::string command = options.Positional.front();
	std::string paramsText = "{}";
	bool hasParams = false;
	for (std::size_t index = 1; index < options.Positional.size(); ++index)
	{
		const std::string& argument = options.Positional[index];
		if (IsUrl(argument))
		{
			options.Url = argument;
		}
		else if (!hasParams)
		{
			paramsText = argument;
			hasParams = true;
		}
		else
		{
			std::fprintf(stderr,
			             "unexpected argument '%s' (usage: call <command> [json] [--url <url>])\n",
			             argument.c_str());
			return 1;
		}
	}

	aegis::JsonValue::ParseError parseError;
	const std::optional<aegis::JsonValue> params = aegis::JsonValue::Parse(paramsText, &parseError);
	if (!params.has_value())
	{
		std::fprintf(stderr,
		             "invalid params JSON at %zu:%zu: %s\n",
		             parseError.Line,
		             parseError.Column,
		             parseError.Message.c_str());
		return 1;
	}
	if (!params->IsObject())
	{
		std::fprintf(stderr, "params must be a JSON object, for example {\"name\":\"Player\"}\n");
		return 1;
	}

	aegis::JsonValue request = aegis::JsonValue::MakeObject();
	request.Set("command", command);
	request.Set("params", *params);

	const aegis::HttpResult result = aegis::HttpClient::Post(JoinUrl(options.Url, "/command"), request.Dump());
	if (const int status = ReportTransportError("call", result); status != 0)
	{
		return status;
	}
	PrintJson(result.Body);

	const std::optional<aegis::JsonValue> response = aegis::JsonValue::Parse(result.Body);
	if (!response.has_value())
	{
		return 0;
	}
	return response->GetBool("ok", false) ? 0 : 2;
}
bool CopyDirectoryRecursive(const aegis::FileSystem::Path& source, const aegis::FileSystem::Path& destination)
{
	bool success = aegis::FileSystem::CreateDirectories(destination);
	for (const auto& entry : aegis::FileSystem::ListDirectory(source, true, true))
	{
		const auto relative = entry.lexically_relative(source);
		const auto target = destination / relative;
		if (aegis::FileSystem::IsDirectory(entry))
		{
			success = aegis::FileSystem::CreateDirectories(target) && success;
		}
		else if (aegis::FileSystem::IsFile(entry))
		{
			success = aegis::FileSystem::CopyFileTo(entry, target) && success;
		}
	}
	return success;
}

int RunDemo(const std::vector<std::string>& arguments)
{
	aegis::Log::Init("");
	const std::string directory = arguments.empty() ? "Assets/Projects/FeatureTest" : arguments.front();
	std::string error;
	if (!aegis::demo::WriteFeatureProject(aegis::FileSystem::FromUtf8(directory), &error))
	{
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	return 0;
}

int ValidateSceneFile(const std::vector<std::string>& arguments)
{
	if (arguments.empty())
	{
		std::fprintf(stderr, "usage: AegisCli scene <file.aescene>\n");
		return 1;
	}
	aegis::Log::Init("");
	aegis::Scene scene;
	std::string error;
	if (!aegis::SceneSerializer::Load(scene, aegis::FileSystem::FromUtf8(arguments.front()), &error))
	{
		std::fprintf(stderr, "Invalid scene: %s\n", error.c_str());
		return 1;
	}
	std::printf("Scene '%s' is valid: %zu entities\n", scene.GetName().c_str(), scene.GetEntityCount());
	return 0;
}

int RunExport(const std::vector<std::string>& arguments)
{
	if (arguments.size() < 2)
	{
		std::fprintf(stderr, "usage: AegisCli export <project.aeproj> <output-directory>\n");
		return 1;
	}
	aegis::Log::Init("");

	aegis::Project project;
	std::string error;
	if (!aegis::Project::Load(aegis::FileSystem::FromUtf8(arguments[0]), project, &error))
	{
		std::fprintf(stderr, "Export failed: %s\n", error.c_str());
		return 1;
	}

	const auto output = aegis::FileSystem::FromUtf8(arguments[1]);
	if (!aegis::FileSystem::CreateDirectories(output))
	{
		std::fprintf(stderr, "Could not create '%s'\n", arguments[1].c_str());
		return 1;
	}

	const auto executableDirectory = aegis::FileSystem::GetExecutableDirectory();
	#if AE_PLATFORM_WINDOWS
	const std::string runtimeName = "AegisRuntime.exe";
	#else
	const std::string runtimeName = "AegisRuntime";
	#endif
	if (!aegis::FileSystem::CopyFileTo(executableDirectory / runtimeName, output / runtimeName))
	{
		std::fprintf(stderr, "Could not copy %s from '%s'\n", runtimeName.c_str(),
			aegis::FileSystem::ToUtf8(executableDirectory).c_str());
		return 1;
	}
	if (!CopyDirectoryRecursive(executableDirectory / "shaders", output / "shaders"))
	{
		std::fprintf(stderr, "Could not copy the compiled shaders\n");
		return 1;
	}
	if (!CopyDirectoryRecursive(project.RootDirectory, output / "Project"))
	{
		std::fprintf(stderr, "Could not copy the project assets\n");
		return 1;
	}

	const std::string readme = "Aegis export\n\nRun the game with:\n    " + runtimeName + " Project/" +
							   project.Name + ".aeproj\n";
	aegis::FileSystem::WriteTextFile(output / "README.txt", readme);
	std::printf("Exported '%s' to '%s'\n", project.Name.c_str(), arguments[1].c_str());
	return 0;
}
} // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		PrintUsage();
		return 1;
	}

	const std::string command = argv[1];
	if (command == "help" || command == "--help" || command == "-h")
	{
		PrintUsage();
		return 0;
	}
	if (command == "version" || command == "--version")
	{
		std::printf("%s %s\n", std::string(aegis::c_EngineName).c_str(), std::string(aegis::c_EngineVersion).c_str());
		return 0;
	}

	const std::vector<std::string> arguments(argv + 2, argv + argc);
	for (const std::string& argument : arguments)
	{
		if (argument == "--help" || argument == "-h")
		{
			PrintUsage();
			return 0;
		}
	}

	if (command == "health")
	{
		return RunHealth(arguments);
	}
	if (command == "commands")
	{
		return RunCommands(arguments);
	}
	if (command == "call")
	{
		return RunCall(arguments);
	}
	if (command == "demo")
	{
		return RunDemo(arguments);
	}
	if (command == "scene")
	{
		return ValidateSceneFile(arguments);
	}
	if (command == "export")
	{
		return RunExport(arguments);
	}

	std::fprintf(stderr, "Unknown command '%s'. Run 'AegisCli help' for the command list.\n", command.c_str());
	return 1;
}
