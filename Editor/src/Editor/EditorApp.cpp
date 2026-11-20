#include "Editor/EditorApp.h"

#include "Aegis/Core/Config.h"
#include "Aegis/Core/Log.h"

namespace aegis
{
	EditorApp::EditorApp()
		: Application([] {
			ApplicationSpecification specification;
			specification.Name = std::string(c_EditorName);
			specification.WindowWidth = c_DefaultWindowWidth;
			specification.WindowHeight = c_DefaultWindowHeight;
			specification.VSync = true;
			return specification;
		}())
	{
		AE_CORE_INFO("{} started", c_EditorName);
	}

	EditorApp::~EditorApp()
	{
		AE_CORE_INFO("{} stopped", c_EditorName);
	}

	void EditorApp::OnEvent(Event& event)
	{
		Application::OnEvent(event);
	}
}
