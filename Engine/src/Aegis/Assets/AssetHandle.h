#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/Uuid.h"

namespace aegis
{
	enum class AssetType : u8
	{
		None = 0,
		Mesh,
		Texture,
		Material,
		Shader,
		Scene,
		Prefab,
		Script,
		Audio,
		Environment,
		Font,
		Animation
	};

	std::string_view AssetTypeToString(AssetType type);
	AssetType AssetTypeFromString(std::string_view text);

	// Stable reference to an asset in the project. Components store handles so
	// scenes stay serialisable and assets can be moved or reimported.
	struct AssetHandle
	{
		Uuid Id = Uuid::Null();
		AssetType Type = AssetType::None;

		bool IsValid() const { return !Id.IsNull(); }
		explicit operator bool() const { return IsValid(); }

		bool operator==(const AssetHandle& other) const { return Id == other.Id && Type == other.Type; }
		bool operator!=(const AssetHandle& other) const { return !(*this == other); }
	};

	struct AssetHandleHash
	{
		usize operator()(const AssetHandle& handle) const noexcept { return UuidHash{}(handle.Id); }
	};
}
