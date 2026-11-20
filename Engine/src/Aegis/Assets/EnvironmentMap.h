#pragma once

#include "Aegis/Assets/Texture.h"
#include "Aegis/Core/FileSystem.h"

#include <glm/glm.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace aegis
{
	// CPU-precomputed image-based lighting for one environment: a diffuse
	// irradiance cube (already divided by PI, so diffuse = albedo * irradiance),
	// a GGX-prefiltered specular cube chain and the split-sum BRDF lookup table.
	//
	// Prefiltering runs once when the scene environment changes; the GPU only
	// samples the three textures. Everything is plain data so the math is unit
	// testable without a GPU.
	struct EnvironmentMap
	{
		using CubeFaces = std::array<std::vector<glm::vec3>, 6>;

		u32 IrradianceSize = 0;
		CubeFaces Irradiance;

		u32 SpecularSize = 0;
		u32 SpecularMipCount = 0;
		std::vector<CubeFaces> Specular; // index 0 = sharpest mip

		u32 BrdfSize = 0;
		std::vector<glm::vec2> Brdf;

		bool IsValid() const;

		// Roughness of one specular mip (0 = mirror, 1 = fully rough).
		f32 GetMipRoughness(u32 mip) const;

		static EnvironmentMap BuildProceduralSky(u32 specularSize = 48, u32 irradianceSize = 24,
			u32 sampleCount = 128, u32 brdfSize = 32);
		static EnvironmentMap BuildFromEquirect(const Image& equirect, u32 specularSize = 48,
			u32 irradianceSize = 24, u32 sampleCount = 128, u32 brdfSize = 32);

		// Loads any equirectangular .hdr (Poly Haven HDRIs work as-is) and precomputes.
		static std::optional<EnvironmentMap> LoadFromHdrFile(const FileSystem::Path& path, u32 specularSize = 48,
			u32 irradianceSize = 24, u32 sampleCount = 128, u32 brdfSize = 32);

		// Helpers shared with the sky pass and with tests.
		static glm::vec3 SkyRadiance(const glm::vec3& direction);
		static glm::vec3 SampleEquirect(const Image& equirect, const glm::vec3& direction);
		// Face order follows the Vulkan/OpenGL cube convention: +X, -X, +Y, -Y, +Z, -Z.
		static glm::vec3 CubeDirection(u32 face, f32 u, f32 v);
		static Image BuildProceduralSkyEquirect(u32 width = 256, u32 height = 128);
	};
}
