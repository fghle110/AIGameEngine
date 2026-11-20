#include "Editor/EditorApp.h"

#include "Aegis/Core/Log.h"

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	aegis::Log::Init("logs");
	aegis::EditorApp app;
	app.Run();
	return 0;
}
