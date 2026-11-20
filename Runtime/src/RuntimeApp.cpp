#include "RuntimeApp.h"

#include "Aegis/Core/Config.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Project/Project.h"

#include <utility>

namespace aegis
{
	RuntimeApp::RuntimeApp(std::filesystem::path projectFile, RuntimeAutomationOptions automation)
		: Application([&projectFile] {
			ApplicationSpecification specification;
			specification.Name = std::string(c_RuntimeName);
			specification.VSync = true;

			Project project;
			std::string error;
			if (Project::Load(projectFile, project, &error))
			{
				specification.Name = project.WindowTitle.empty() ? project.Name : project.WindowTitle;
				specification.WindowWidth = project.WindowWidth;
				specification.WindowHeight = project.WindowHeight;
				specification.VSync = project.VSync;
			}
			return specification;
		}()),
		  m_ProjectFile(std::move(projectFile))
	{
		m_RuntimeLayer = new RuntimeLayer(m_ProjectFile, std::move(automation));
		PushLayer(m_RuntimeLayer);
	}

	RuntimeApp::~RuntimeApp()
	{
		m_RuntimeLayer = nullptr;
	}
}
