#pragma once

#include "Aegis/Core/Application.h"

#include <string>

namespace aegis
{
	class RuntimeApp final : public Application
	{
	public:
		explicit RuntimeApp(std::string projectPath);
		~RuntimeApp() override;

	private:
		std::string m_ProjectPath;
	};
}
