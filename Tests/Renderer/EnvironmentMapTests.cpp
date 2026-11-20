#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Assets/EnvironmentMap.h"
#include "Aegis/Assets/Texture.h"
#include "Aegis/Core/FileSystem.h"

#include <stb_image_write.h>

#include <cmath>
#include <vector>

using namespace aegis;

namespace
{
	Image MakeDirectionEquirect(u32 width, u32 height)
	{
		std::vector<f32> pixels(static_cast<usize>(width) * height * 4);
		for (u32 y = 0; y < height; ++y)
		{
			const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(height);
			const f32 theta = (0.5f - v) * 3.14159265358979f;
			for (u32 x = 0; x < width; ++x)
			{
				const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(width);
				const f32 phi = (u - 0.5f) * 2.0f * 3.14159265358979f;
				const glm::vec3 direction(std::cos(theta) * std::cos(phi), std::sin(theta),
					std::cos(theta) * std::sin(phi));
				const glm::vec3 encoded = direction * 0.5f + 0.5f;
				const usize index = (static_cast<usize>(y) * width + x) * 4;
				pixels[index] = encoded.x;
				pixels[index + 1] = encoded.y;
				pixels[index + 2] = encoded.z;
				pixels[index + 3] = 1.0f;
			}
		}
		return Image::FromLinearRgba(width, height, std::move(pixels));
	}

	Image MakeUniformEquirect(f32 value, u32 width = 16, u32 height = 8)
	{
		std::vector<f32> pixels(static_cast<usize>(width) * height * 4, value);
		for (usize i = 3; i < pixels.size(); i += 4)
		{
			pixels[i] = 1.0f;
		}
		return Image::FromLinearRgba(width, height, std::move(pixels));
	}
}

TEST_CASE("IBL: procedural sky matches the shader function")
{
	const glm::vec3 zenith = EnvironmentMap::SkyRadiance(glm::vec3(0.0f, 1.0f, 0.0f));
	CHECK(zenith.x == doctest::Approx(0.14f).epsilon(0.001));
	CHECK(zenith.z == doctest::Approx(0.42f).epsilon(0.001));

	const glm::vec3 horizon = EnvironmentMap::SkyRadiance(glm::vec3(1.0f, 0.0f, 0.0f));
	CHECK(horizon.x == doctest::Approx(0.58f).epsilon(0.001));

	const glm::vec3 below = EnvironmentMap::SkyRadiance(glm::vec3(0.0f, -1.0f, 0.0f));
	CHECK(below.x == doctest::Approx(0.10f).epsilon(0.001));
}

TEST_CASE("IBL: cube face directions follow the Vulkan convention")
{
	const glm::vec3 plusX = EnvironmentMap::CubeDirection(0, 0.0f, 0.0f);
	CHECK(plusX.x == doctest::Approx(1.0f));
	const glm::vec3 minusY = EnvironmentMap::CubeDirection(3, 0.0f, 0.0f);
	CHECK(minusY.y == doctest::Approx(-1.0f));
	const glm::vec3 plusZ = EnvironmentMap::CubeDirection(4, 0.0f, 0.0f);
	CHECK(plusZ.z == doctest::Approx(1.0f));
	CHECK(EnvironmentMap::CubeDirection(2, 0.0f, 0.0f).y == doctest::Approx(1.0f));
}

TEST_CASE("IBL: equirect sampling round-trips a direction encoding")
{
	const Image equirect = MakeDirectionEquirect(256, 128);
	REQUIRE(equirect.IsValid());

	const float directions[4][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.5f, 0.6f, 0.3f },
		{ -0.7f, -0.2f, 0.5f } };
	for (const auto& direction : directions)
	{
		const glm::vec3 normalized = glm::normalize(glm::vec3(direction[0], direction[1], direction[2]));
		const glm::vec3 sampled = EnvironmentMap::SampleEquirect(equirect, normalized);
		const glm::vec3 expected = normalized * 0.5f + 0.5f;
		CHECK(sampled.x == doctest::Approx(expected.x).epsilon(0.02));
		CHECK(sampled.y == doctest::Approx(expected.y).epsilon(0.02));
		CHECK(sampled.z == doctest::Approx(expected.z).epsilon(0.02));
	}
}

TEST_CASE("IBL: irradiance of a uniform environment is the environment colour")
{
	const EnvironmentMap environment = EnvironmentMap::BuildFromEquirect(MakeUniformEquirect(0.5f), 8, 8, 32, 8);
	REQUIRE(environment.IsValid());
	REQUIRE(environment.Irradiance[0].size() == 64);
	for (const glm::vec3& texel : environment.Irradiance[0])
	{
		CHECK(texel.x == doctest::Approx(0.5f).epsilon(0.02));
	}
}

TEST_CASE("IBL: prefiltering keeps the bright spot sharp at mip 0 and smooths it later")
{
	std::vector<f32> pixels(64 * 32 * 4, 0.0f);
	for (usize i = 3; i < pixels.size(); i += 4)
	{
		pixels[i] = 1.0f;
	}
	// A single bright texel: +X direction maps to the middle of the equator.
	const usize bright = (static_cast<usize>(16) * 64 + 32) * 4;
	for (int channel = 0; channel < 3; ++channel)
	{
		pixels[bright + channel] = 8.0f;
	}
	const Image equirect = Image::FromLinearRgba(64, 32, std::move(pixels));

	const EnvironmentMap environment = EnvironmentMap::BuildFromEquirect(equirect, 16, 8, 64, 8);
	REQUIRE(environment.IsValid());
	REQUIRE(environment.Specular.size() >= 2);

	// Find the brightest texel of each mip on the +X face.
	const auto brightest = [](const std::vector<glm::vec3>& face)
	{
		f32 value = 0.0f;
		for (const glm::vec3& texel : face)
		{
			value = std::max(value, texel.x);
		}
		return value;
	};

	const f32 sharp = brightest(environment.Specular.front()[0]);
	const f32 rough = brightest(environment.Specular.back()[0]);
	CHECK(sharp > rough);
	CHECK(sharp > 0.0f);
}

TEST_CASE("IBL: BRDF LUT stays in the energy-preserving range")
{
	const EnvironmentMap environment = EnvironmentMap::BuildProceduralSky(8, 4, 32, 16);
	REQUIRE(environment.IsValid());
	REQUIRE(environment.Brdf.size() == 16 * 16);
	for (const glm::vec2& texel : environment.Brdf)
	{
		CHECK(texel.x >= 0.0f);
		CHECK(texel.y >= 0.0f);
		CHECK(texel.x + texel.y <= 1.05f);
	}
	// Near NdotV = 1 and roughness = 1 the environment reflection is fully diffuse.
	const glm::vec2 rough = environment.Brdf[15 * 16 + 15];
	CHECK(rough.x + rough.y > 0.2f);
}

TEST_CASE("IBL: precomputation is deterministic")
{
	const EnvironmentMap first = EnvironmentMap::BuildProceduralSky(8, 4, 32, 8);
	const EnvironmentMap second = EnvironmentMap::BuildProceduralSky(8, 4, 32, 8);
	REQUIRE(first.IsValid());
	REQUIRE(second.IsValid());
	REQUIRE(first.Specular.size() == second.Specular.size());
	for (usize mip = 0; mip < first.Specular.size(); ++mip)
	{
		for (usize face = 0; face < 6; ++face)
		{
			REQUIRE(first.Specular[mip][face].size() == second.Specular[mip][face].size());
			for (usize texel = 0; texel < first.Specular[mip][face].size(); ++texel)
			{
				CHECK(first.Specular[mip][face][texel] == second.Specular[mip][face][texel]);
			}
		}
	}
}

TEST_CASE("IBL: loads an equirectangular HDR file and precomputes")
{
	const FileSystem::Path directory = FileSystem::FromUtf8("TestAssets/IBL");
	FileSystem::CreateDirectories(directory);
	const FileSystem::Path path = directory / "uniform.hdr";

	constexpr int width = 16;
	constexpr int height = 8;
	std::vector<f32> hdr(static_cast<usize>(width) * height * 3, 0.25f);
	REQUIRE(stbi_write_hdr(FileSystem::ToUtf8(path).c_str(), width, height, 3, hdr.data()) != 0);

	const std::optional<EnvironmentMap> environment = EnvironmentMap::LoadFromHdrFile(path, 8, 8, 32, 8);
	REQUIRE(environment.has_value());
	CHECK(environment->IsValid());
	CHECK(environment->IrradianceSize == 8);
	CHECK(environment->SpecularSize == 8);
	CHECK(environment->BrdfSize == 8);

	// The uniform HDR becomes a uniform irradiance (within the HDR reader tolerance).
	CHECK(environment->Irradiance[2][0].x == doctest::Approx(0.25f).epsilon(0.05));
}
