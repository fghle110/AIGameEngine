#pragma once

#include <filesystem>
#include <string>

namespace aegis::demo
{
	// Writes the FeatureTest project (manifest, scene, scripts) that exercises every
	// component type, the scene hierarchy and the scripting API.
	bool WriteFeatureProject(const std::filesystem::path& projectDirectory, std::string* outError = nullptr);
}
