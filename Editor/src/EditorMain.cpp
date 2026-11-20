#include "Editor/EditorApp.h"

#include "Aegis/Core/Log.h"

#include <cstring>
#include <string>

namespace
{
	void PrintUsage()
	{
		std::printf("Aegis Editor\n");
		std::printf("  --frames <n>          render n frames and quit (self-test)\n");
		std::printf("  --screenshot <path>   capture the final frame to a PNG\n");
	}
}

int main(int argc, char** argv)
{
	aegis::Log::Init("logs");

	aegis::EditorAutomationOptions automation;
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument == "--frames" && i + 1 < argc)
		{
			automation.Frames = std::atoi(argv[++i]);
		}
		else if (argument == "--screenshot" && i + 1 < argc)
		{
			automation.ScreenshotPath = argv[++i];
		}
		else if (argument == "--help" || argument == "-h")
		{
			PrintUsage();
			return 0;
		}
	}

	aegis::EditorApp app(automation);
	app.Run();
	return 0;
}
