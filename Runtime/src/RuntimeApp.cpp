#include "RuntimeApp.h"

#include "Aegis/Core/Config.h"
#include "Aegis/Core/Log.h"

namespace aegis
{
	RuntimeApp::RuntimeApp(std::string projectPath)
		: Application([] {
			ApplicationSpecification specification;
			specification.Name = std::string(c_RuntimeName);
			specification.VSync = true;
			return specification;
		}())
		, m_ProjectPath(std::move(projectPath))
	{
		AE_CORE_INFO("{} starting with project '{}'", c_RuntimeName, m_ProjectPath);
	}

	RuntimeApp::~RuntimeApp() = default;
}
