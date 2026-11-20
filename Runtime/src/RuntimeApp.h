#pragma once

#include "Aegis/Core/Application.h"
#include "RuntimeLayer.h"

#include <filesystem>

namespace aegis
{
	class RuntimeApp final : public Application
	{
	public:
		explicit RuntimeApp(std::filesystem::path projectFile, RuntimeAutomationOptions automation = {});
		~RuntimeApp() override;

	private:
		RuntimeLayer* m_RuntimeLayer = nullptr;
		std::filesystem::path m_ProjectFile;
	};
}
