#include "RuntimeApp.h"

#include "Aegis/Core/Log.h"

#include <string>

int main(int argc, char** argv)
{
	aegis::Log::Init("logs");

	std::string projectPath;
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument == "--project" && i + 1 < argc)
		{
			projectPath = argv[++i];
		}
		else if (projectPath.empty())
		{
			projectPath = argument;
		}
	}

	aegis::RuntimeApp app(projectPath);
	app.Run();
	return 0;
}
