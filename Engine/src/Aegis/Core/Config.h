#pragma once

// Branding, versioning and build facts. Every user-visible name lives here so a
// rename touches one file (plus the CMake project() call).

#include "Aegis/Core/Base.h"

#define AE_ENGINE_NAME "Aegis"
#define AE_ENGINE_VERSION_MAJOR 0
#define AE_ENGINE_VERSION_MINOR 1
#define AE_ENGINE_VERSION_PATCH 0

#define AE_ENGINE_VERSION_STRING "0.1.0"

namespace aegis
{
	// Hazel style: c_ prefix for compile-time constants.
	inline constexpr std::string_view c_EngineName = AE_ENGINE_NAME;
	inline constexpr std::string_view c_EngineVersion = AE_ENGINE_VERSION_STRING;
	inline constexpr std::string_view c_EngineNamespace = "aegis";
	inline constexpr std::string_view c_DefaultProjectExtension = ".aeproj";
	inline constexpr std::string_view c_SceneExtension = ".aescene";
	inline constexpr std::string_view c_PrefabExtension = ".aeprefab";
	inline constexpr std::string_view c_MaterialExtension = ".aemat";
	inline constexpr std::string_view c_MeshExtension = ".aemesh";

	inline constexpr std::string_view c_EditorName = "Aegis Editor";
	inline constexpr std::string_view c_RuntimeName = "Aegis Runtime";

	inline constexpr u32 c_DefaultWindowWidth = 1600;
	inline constexpr u32 c_DefaultWindowHeight = 900;
	inline constexpr std::string_view c_DefaultWindowTitle = "Aegis Editor";
	inline constexpr u16 c_DefaultCommandPort = 7777;
}
