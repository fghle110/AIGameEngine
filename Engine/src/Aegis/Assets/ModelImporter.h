#pragma once

#include "Aegis/Assets/Material.h"
#include "Aegis/Assets/Mesh.h"
#include "Aegis/Core/FileSystem.h"

#include <optional>
#include <string>
#include <vector>

namespace aegis
{
	// One decoded image from a glTF file, together with the sampler the file
	// declares for it and the colour space its slot requires.
	struct ImportedTexture
	{
		Image Source;
		SamplerSettings Sampler;
		TextureColorSpace ColorSpace = TextureColorSpace::Srgb;
		std::string Name;
	};

	// The material of one primitive. Texture slots index ImportedModel::Textures;
	// -1 means the map is absent.
	struct ImportedMaterial
	{
		std::string Name;
		MaterialFactors Factors;
		i32 AlbedoTexture = -1;
		i32 NormalTexture = -1;
		i32 MetallicRoughnessTexture = -1;
		i32 OcclusionTexture = -1;
		i32 EmissiveTexture = -1;
	};

	struct ImportedModel
	{
		std::string Name;
		Mesh Geometry;
		std::vector<ImportedMaterial> Materials;
		std::vector<ImportedTexture> Textures;
		// Primitives dropped because their mode is not TRIANGLES (points, strips).
		u32 SkippedPrimitiveCount = 0;
	};

	// glTF 2.0 / GLB importer built on cgltf. Node transforms are baked into the
	// vertex data, each primitive becomes one submesh with its own material, missing
	// normals and tangents are generated, and skins/animations are ignored with a
	// single log line.
	class ModelImporter
	{
	public:
		static std::optional<ImportedModel> Import(const FileSystem::Path& path);
	};
}
