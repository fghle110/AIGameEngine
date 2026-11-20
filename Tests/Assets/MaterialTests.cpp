#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Assets/Material.h"

#include <glm/glm.hpp>

using namespace aegis;

TEST_CASE("Material: engine defaults are a neutral dielectric")
{
	const MaterialFactors factors = MaterialFactors::EngineDefault();
	CHECK(factors.BaseColorFactor == glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
	CHECK(factors.MetallicFactor == doctest::Approx(0.0f));
	CHECK(factors.RoughnessFactor == doctest::Approx(0.5f));
	CHECK(factors.NormalScale == doctest::Approx(1.0f));
	CHECK(factors.OcclusionStrength == doctest::Approx(1.0f));
	CHECK(factors.EmissiveFactor == glm::vec3(0.0f));
	CHECK(factors.EmissiveStrength == doctest::Approx(1.0f));
}

TEST_CASE("Material: glTF defaults follow the specification")
{
	const MaterialFactors factors = MaterialFactors::GltfDefault();
	CHECK(factors.BaseColorFactor == glm::vec4(1.0f));
	CHECK(factors.MetallicFactor == doctest::Approx(1.0f));
	CHECK(factors.RoughnessFactor == doctest::Approx(1.0f));
	CHECK(factors.NormalScale == doctest::Approx(1.0f));
	CHECK(factors.OcclusionStrength == doctest::Approx(1.0f));
	CHECK(factors.EmissiveFactor == glm::vec3(0.0f));
	CHECK(factors.EmissiveStrength == doctest::Approx(1.0f));
}

TEST_CASE("Material: a default constructed factor set matches the engine default")
{
	const MaterialFactors defaulted;
	const MaterialFactors expected = MaterialFactors::EngineDefault();
	CHECK(defaulted.BaseColorFactor == expected.BaseColorFactor);
	CHECK(defaulted.MetallicFactor == doctest::Approx(expected.MetallicFactor));
	CHECK(defaulted.RoughnessFactor == doctest::Approx(expected.RoughnessFactor));
	CHECK(defaulted.NormalScale == doctest::Approx(expected.NormalScale));
	CHECK(defaulted.OcclusionStrength == doctest::Approx(expected.OcclusionStrength));
	CHECK(defaulted.EmissiveFactor == expected.EmissiveFactor);
	CHECK(defaulted.EmissiveStrength == doctest::Approx(expected.EmissiveStrength));
}

TEST_CASE("Material: an empty texture set reports no maps")
{
	const MaterialTextures textures;
	CHECK_FALSE(textures.HasAnyTexture());
	CHECK(textures.Albedo == nullptr);
	CHECK(textures.Normal == nullptr);
	CHECK(textures.MetallicRoughness == nullptr);
	CHECK(textures.Occlusion == nullptr);
	CHECK(textures.Emissive == nullptr);
}

TEST_CASE("Material: creating without a device or layout fails explicitly")
{
	MaterialTextures textures;
	CHECK(Material::Create(nullptr, nullptr, MaterialFactors{}, textures, "test") == nullptr);
}
