#include "Aegis/Core/Config.h"
#include "Aegis/Core/Log.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	void PrintUsage()
	{
		std::printf("%s command line tool %s\n",
			std::string(aegis::c_EngineName).c_str(), std::string(aegis::c_EngineVersion).c_str());
		std::printf("\nUsage:\n");
		std::printf("  AegisCli <command> [arguments]\n\n");
		std::printf("Commands:\n");
		std::printf("  version                     Print the engine version\n");
		std::printf("  help                        Print this help\n");
		std::printf("  commands [url]              List editor AI commands from a running editor\n");
		std::printf("  call <command> [json] [url] Invoke an editor AI command\n");
		std::printf("  scene <file>                Validate a scene file\n");
	}
}

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
		std::printf("%s %s\n", std::string(aegis::c_EngineName).c_str(),
			std::string(aegis::c_EngineVersion).c_str());
		return 0;
	}

	std::fprintf(stderr, "Unknown command '%s'. Run 'AegisCli help' for the command list.\n", command.c_str());
	return 1;
}
