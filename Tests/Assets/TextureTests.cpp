#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Assets/Texture.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

using namespace aegis;

namespace
{
	FileSystem::Path TestAssetPath(const char* fileName)
	{
		return FileSystem::Path("TestAssets") / FileSystem::Path(fileName);
	}

	std::vector<u8> MakeQuadPixels(u8 red, u8 green, u8 blue, u8 alpha)
	{
		std::vector<u8> pixels(2 * 2 * 4);
		for (usize i = 0; i < pixels.size(); i += 4)
		{
			pixels[i + 0] = red;
			pixels[i + 1] = green;
			pixels[i + 2] = blue;
			pixels[i + 3] = alpha;
		}
		return pixels;
	}
}

TEST_CASE("Texture: a PNG round trip keeps the source colours")
{
	const FileSystem::Path path = TestAssetPath("ae_texture_roundtrip.png");
	const std::vector<u8> source = MakeQuadPixels(128, 64, 32, 255);
	REQUIRE(stbi_write_png(FileSystem::ToUtf8(path).c_str(), 2, 2, 4, source.data(), 2 * 4) != 0);

	const std::optional<Image> image = Image::LoadFromFile(path, TextureColorSpace::Srgb);
	REQUIRE(image.has_value());
	CHECK(image->GetWidth() == 2);
	CHECK(image->GetHeight() == 2);
	// 2x2 -> 1x1 is the only extra level.
	CHECK(image->GetMipCount() == 2);

	const std::vector<f32>& pixels = image->GetMipPixels(0);
	REQUIRE(pixels.size() == 16);
	CHECK(pixels[0] == doctest::Approx(Image::SrgbToLinear(128.0f / 255.0f)).epsilon(0.002));
	CHECK(pixels[1] == doctest::Approx(Image::SrgbToLinear(64.0f / 255.0f)).epsilon(0.002));
	CHECK(pixels[2] == doctest::Approx(Image::SrgbToLinear(32.0f / 255.0f)).epsilon(0.002));
	// Alpha is coverage, so it is never gamma decoded.
	CHECK(pixels[3] == doctest::Approx(1.0f));

	CHECK(FileSystem::RemoveFile(path));
}

TEST_CASE("Texture: the linear colour space leaves the samples untouched")
{
	const FileSystem::Path path = TestAssetPath("ae_texture_linear.png");
	const std::vector<u8> source = MakeQuadPixels(128, 64, 32, 255);
	REQUIRE(stbi_write_png(FileSystem::ToUtf8(path).c_str(), 2, 2, 4, source.data(), 2 * 4) != 0);

	const std::optional<Image> image = Image::LoadFromFile(path, TextureColorSpace::Linear);
	REQUIRE(image.has_value());
	const std::vector<f32>& pixels = image->GetMipPixels(0);
	CHECK(pixels[0] == doctest::Approx(128.0f / 255.0f).epsilon(0.002));
	CHECK(pixels[1] == doctest::Approx(64.0f / 255.0f).epsilon(0.002));
	CHECK(pixels[2] == doctest::Approx(32.0f / 255.0f).epsilon(0.002));

	CHECK(FileSystem::RemoveFile(path));
}

TEST_CASE("Texture: mip levels are box filtered down to 1x1")
{
	// 2x2 checkerboard of black and white texels: the 1x1 mip is the average.
	const std::vector<u8> bytes = {
		0, 0, 0, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 0, 0, 0, 255,
	};
	const Image image = Image::FromRgba8(2, 2, bytes.data(), TextureColorSpace::Linear);
	REQUIRE(image.IsValid());
	REQUIRE(image.GetMipCount() == 2);
	CHECK(image.GetMipWidth(1) == 1);
	CHECK(image.GetMipHeight(1) == 1);

	const std::vector<f32>& smallest = image.GetMipPixels(1);
	REQUIRE(smallest.size() == 4);
	CHECK(smallest[0] == doctest::Approx(0.5f));
	CHECK(smallest[1] == doctest::Approx(0.5f));
	CHECK(smallest[2] == doctest::Approx(0.5f));
	CHECK(smallest[3] == doctest::Approx(1.0f));
}

TEST_CASE("Texture: non power of two images clamp the odd mip taps")
{
	const Image image = Image::CreateSolid(3, 5, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
	REQUIRE(image.IsValid());
	// 3x5 -> 1x2 -> 1x1
	REQUIRE(image.GetMipCount() == 3);
	CHECK(image.GetMipWidth(1) == 1);
	CHECK(image.GetMipHeight(1) == 2);
	CHECK(image.GetMipWidth(2) == 1);
	CHECK(image.GetMipHeight(2) == 1);

	// A solid colour must survive every filtering step unchanged.
	for (u32 mip = 0; mip < image.GetMipCount(); ++mip)
	{
		const std::vector<f32>& pixels = image.GetMipPixels(mip);
		for (usize i = 0; i < pixels.size(); i += 4)
		{
			CHECK(pixels[i + 0] == doctest::Approx(0.25f).epsilon(0.001));
			CHECK(pixels[i + 1] == doctest::Approx(0.5f).epsilon(0.001));
			CHECK(pixels[i + 2] == doctest::Approx(0.75f).epsilon(0.001));
			CHECK(pixels[i + 3] == doctest::Approx(1.0f).epsilon(0.001));
		}
	}
}

TEST_CASE("Texture: HDR files decode as floating point radiance")
{
	const FileSystem::Path path = TestAssetPath("ae_texture_hdr.hdr");
	const std::vector<f32> radiance = {
		4.0f, 0.5f, 0.125f,
		0.25f, 2.0f, 1.0f,
	};
	REQUIRE(stbi_write_hdr(FileSystem::ToUtf8(path).c_str(), 2, 1, 3, radiance.data()) != 0);

	const std::optional<Image> image = Image::LoadFromFile(path, TextureColorSpace::Auto);
	REQUIRE(image.has_value());
	CHECK(image->GetWidth() == 2);
	CHECK(image->GetHeight() == 1);

	const std::vector<f32>& pixels = image->GetMipPixels(0);
	REQUIRE(pixels.size() == 8);
	// stb's HDR writer stores RGBE with an 8-bit mantissa, so a channel that is much
	// dimmer than the pixel maximum is quantised: allow a relative error of 15%.
	CHECK(pixels[0] == doctest::Approx(4.0f).epsilon(0.15));
	CHECK(pixels[1] == doctest::Approx(0.5f).epsilon(0.15));
	CHECK(pixels[2] == doctest::Approx(0.125f).epsilon(0.15));
	CHECK(pixels[4] == doctest::Approx(0.25f).epsilon(0.15));
	CHECK(pixels[5] == doctest::Approx(2.0f).epsilon(0.15));

	CHECK(FileSystem::RemoveFile(path));
}

TEST_CASE("Texture: a missing or corrupt file is rejected")
{
	const std::optional<Image> missing = Image::LoadFromFile(TestAssetPath("ae_texture_missing.png"));
	CHECK_FALSE(missing.has_value());

	const std::vector<u8> garbage = { 1, 2, 3, 4, 5, 6, 7, 8 };
	const std::optional<Image> corrupt = Image::DecodeFromMemory(garbage.data(), garbage.size(),
		TextureColorSpace::Srgb, "garbage");
	CHECK_FALSE(corrupt.has_value());

	const std::optional<Image> empty = Image::DecodeFromMemory(nullptr, 0, TextureColorSpace::Srgb, "empty");
	CHECK_FALSE(empty.has_value());
}

TEST_CASE("Texture: sRGB and linear encodings are inverse to each other")
{
	CHECK(Image::SrgbToLinear(0.0f) == doctest::Approx(0.0f));
	CHECK(Image::SrgbToLinear(1.0f) == doctest::Approx(1.0f).epsilon(0.001));
	CHECK(Image::SrgbToLinear(0.5f) == doctest::Approx(0.2140f).epsilon(0.01));
	for (const f32 value : { 0.05f, 0.25f, 0.5f, 0.75f, 0.95f })
	{
		const f32 roundTrip = Image::LinearToSrgb(Image::SrgbToLinear(value));
		CHECK(roundTrip == doctest::Approx(value).epsilon(0.001));
	}
}

TEST_CASE("Texture: sampler presets describe the intended filtering")
{
	const SamplerSettings albedo = SamplerSettings::Albedo();
	CHECK(albedo.MagFilter == TextureFilter::Linear);
	CHECK(albedo.MinFilter == TextureFilter::Linear);
	CHECK(albedo.MipFilter == TextureFilter::Linear);
	CHECK(albedo.AddressU == TextureWrapMode::Repeat);
	CHECK(albedo.MaxAnisotropy > 1.0f);

	const SamplerSettings clamped = SamplerSettings::Clamped();
	CHECK(clamped.AddressU == TextureWrapMode::Clamp);
	CHECK(clamped.AddressV == TextureWrapMode::Clamp);
	CHECK(clamped.MaxAnisotropy == doctest::Approx(1.0f));

	const SamplerSettings point = SamplerSettings::Point();
	CHECK(point.MagFilter == TextureFilter::Nearest);
	CHECK(point.MinFilter == TextureFilter::Nearest);
	CHECK(point.MipFilter == TextureFilter::Nearest);
}

TEST_CASE("Texture: checkerboard generation alternates cells")
{
	const Image image = Image::CreateCheckerboard(4, 4, 2, glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
	REQUIRE(image.IsValid());
	const std::vector<f32>& pixels = image.GetMipPixels(0);
	const auto redAt = [&pixels](u32 width, u32 x, u32 y)
	{
		return pixels[(static_cast<usize>(y) * width + x) * 4];
	};
	CHECK(redAt(4, 0, 0) == doctest::Approx(1.0f));
	CHECK(redAt(4, 2, 0) == doctest::Approx(0.0f));
	CHECK(redAt(4, 0, 2) == doctest::Approx(0.0f));
	CHECK(redAt(4, 2, 2) == doctest::Approx(1.0f));
}

TEST_CASE("Texture: GPU upload without a device fails instead of crashing")
{
	const Image image = Image::CreateSolid(1, 1, glm::vec4(1.0f));
	CHECK(Texture::Create(nullptr, image, SamplerSettings{}) == nullptr);
}
