#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/FileSystem.h"

#include <string>

namespace aegis
{
	// A project is a folder with a .aeproj manifest plus Scenes/, Scripts/, Audio/
	// and Textures/ subfolders. Paths inside the manifest are project-relative.
	struct Project
	{
		std::string Name = "Untitled";
		std::string StartScene = "Scenes/Main.aescene";
		std::string WindowTitle;
		u32 WindowWidth = 1600;
		u32 WindowHeight = 900;
		bool VSync = true;

		FileSystem::Path RootDirectory;

		static bool Load(const FileSystem::Path& projectFile, Project& outProject, std::string* outError = nullptr);
		bool Save(const FileSystem::Path& projectFile, std::string* outError = nullptr) const;

		FileSystem::Path GetManifestPath() const;
		FileSystem::Path Resolve(const std::string& relativePath) const;
		FileSystem::Path GetStartScenePath() const;
	};
}
