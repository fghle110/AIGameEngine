#include "Aegis/Assets/AssetHandle.h"

#include "Aegis/Core/StringUtils.h"

namespace aegis
{
	std::string_view AssetTypeToString(AssetType type)
	{
		switch (type)
		{
			case AssetType::None: return "none";
			case AssetType::Mesh: return "mesh";
			case AssetType::Texture: return "texture";
			case AssetType::Material: return "material";
			case AssetType::Shader: return "shader";
			case AssetType::Scene: return "scene";
			case AssetType::Prefab: return "prefab";
			case AssetType::Script: return "script";
			case AssetType::Audio: return "audio";
			case AssetType::Environment: return "environment";
			case AssetType::Font: return "font";
			case AssetType::Animation: return "animation";
		}
		return "none";
	}

	AssetType AssetTypeFromString(std::string_view text)
	{
		const std::string lowered = StringUtils::ToLower(text);
		if (lowered == "mesh") return AssetType::Mesh;
		if (lowered == "texture") return AssetType::Texture;
		if (lowered == "material") return AssetType::Material;
		if (lowered == "shader") return AssetType::Shader;
		if (lowered == "scene") return AssetType::Scene;
		if (lowered == "prefab") return AssetType::Prefab;
		if (lowered == "script") return AssetType::Script;
		if (lowered == "audio") return AssetType::Audio;
		if (lowered == "environment") return AssetType::Environment;
		if (lowered == "font") return AssetType::Font;
		if (lowered == "animation") return AssetType::Animation;
		return AssetType::None;
	}
}
