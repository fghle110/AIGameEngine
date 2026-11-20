#include "RuntimeApp.h"

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
	std::filesystem::path ResolveProjectPath(const std::string& argument)
	{
		auto path = aegis::FileSystem::FromUtf8(argument);
		if (aegis::FileSystem::IsDirectory(path))
		{
			for (const auto& entry : aegis::FileSystem::ListDirectory(path, false, false))
			{
				if (aegis::FileSystem::GetExtension(entry) == "aeproj")
				{
					return entry;
				}
			}
		}
		return path;
	}
}

int main(int argc, char** argv)
{
	aegis::Log::Init("logs");

	std::string projectArgument;
	aegis::RuntimeAutomationOptions automation;
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument == "--project" && i + 1 < argc)
		{
			projectArgument = argv[++i];
		}
		else if (argument == "--frames" && i + 1 < argc)
		{
			automation.Frames = std::atoi(argv[++i]);
		}
		else if (argument == "--screenshot" && i + 1 < argc)
		{
			automation.ScreenshotPath = argv[++i];
		}
		else if (argument == "--help" || argument == "-h")
		{
			std::printf("AegisRuntime <project.aeproj | project folder> [--frames N] [--screenshot out.png]\n");
			return 0;
		}
		else if (projectArgument.empty())
		{
			projectArgument = argument;
		}
	}

	if (projectArgument.empty())
	{
		std::fprintf(stderr, "AegisRuntime requires a project file (.aeproj) or a project folder\n");
		return 1;
	}

	const auto projectFile = ResolveProjectPath(projectArgument);
	if (!aegis::FileSystem::IsFile(projectFile))
	{
		std::fprintf(stderr, "Project file '%s' was not found\n", aegis::FileSystem::ToUtf8(projectFile).c_str());
		return 1;
	}

	aegis::RuntimeApp app(projectFile, automation);
	app.Run();
	return 0;
}
