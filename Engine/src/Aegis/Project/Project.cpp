#include "Aegis/Project/Project.h"

#include "Aegis/Core/Json.h"
#include "Aegis/Core/Log.h"

namespace aegis
{
	namespace
	{
		constexpr i32 c_ProjectVersion = 1;
	}

	bool Project::Load(const FileSystem::Path& projectFile, Project& outProject, std::string* outError)
	{
		std::string text;
		if (!FileSystem::TryReadTextFile(projectFile, text))
		{
			if (outError != nullptr)
			{
				*outError = std::format("Could not read project file '{}'", FileSystem::ToUtf8(projectFile));
			}
			AE_CORE_ERROR("Could not read project file '{}'", FileSystem::ToUtf8(projectFile));
			return false;
		}

		const JsonValue document = JsonValue::ParseOrEmpty(text, "project");
		if (!document.IsObject())
		{
			if (outError != nullptr)
			{
				*outError = "The project manifest is not a JSON object";
			}
			return false;
		}

		const i64 version = document.GetInt("version", 0);
		if (version != c_ProjectVersion)
		{
			if (outError != nullptr)
			{
				*outError = std::format("Unsupported project version {} (expected {})", version, c_ProjectVersion);
			}
			return false;
		}

		outProject = Project{};
		outProject.Name = document.GetString("name", "Untitled");
		outProject.StartScene = document.GetString("startScene", "Scenes/Main.aescene");
		outProject.WindowTitle = document.GetString("windowTitle", outProject.Name);
		outProject.WindowWidth = static_cast<u32>(document.GetUInt("windowWidth", 1600));
		outProject.WindowHeight = static_cast<u32>(document.GetUInt("windowHeight", 900));
		outProject.VSync = document.GetBool("vsync", true);
		outProject.RootDirectory = projectFile.parent_path();

		if (outProject.WindowWidth == 0 || outProject.WindowHeight == 0)
		{
			if (outError != nullptr)
			{
				*outError = "The project window size must be positive";
			}
			return false;
		}
		return true;
	}

	bool Project::Save(const FileSystem::Path& projectFile, std::string* outError) const
	{
		JsonValue document = JsonValue::MakeObject();
		document.Set("version", c_ProjectVersion);
		document.Set("name", Name);
		document.Set("startScene", StartScene);
		document.Set("windowTitle", WindowTitle.empty() ? Name : WindowTitle);
		document.Set("windowWidth", WindowWidth);
		document.Set("windowHeight", WindowHeight);
		document.Set("vsync", VSync);

		if (!FileSystem::WriteTextFile(projectFile, document.Dump(4) + "\n"))
		{
			if (outError != nullptr)
			{
				*outError = std::format("Could not write project file '{}'", FileSystem::ToUtf8(projectFile));
			}
			return false;
		}
		return true;
	}

	FileSystem::Path Project::GetManifestPath() const
	{
		return RootDirectory / (Name + ".aeproj");
	}

	FileSystem::Path Project::Resolve(const std::string& relativePath) const
	{
		return RootDirectory / FileSystem::FromUtf8(relativePath);
	}

	FileSystem::Path Project::GetStartScenePath() const
	{
		return Resolve(StartScene);
	}
}
