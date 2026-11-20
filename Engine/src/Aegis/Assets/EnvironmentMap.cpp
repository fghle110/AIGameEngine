#include "Aegis/Assets/EnvironmentMap.h"

#include "Aegis/Core/Log.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace aegis
{
	namespace
	{
		constexpr f32 c_Pi = 3.14159265358979323846f;

		f32 RadicalInverseVdc(u32 bits)
		{
			bits = (bits << 16u) | (bits >> 16u);
			bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
			bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
			bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
			bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
			return static_cast<f32>(bits) * 2.3283064365386963e-10f;
		}

		glm::vec2 Hammersley(u32 index, u32 count)
		{
			return glm::vec2(static_cast<f32>(index) / static_cast<f32>(std::max(count, 1u)),
				RadicalInverseVdc(index));
		}

		void BuildTangentFrame(const glm::vec3& normal, glm::vec3& outTangent, glm::vec3& outBitangent)
		{
			const glm::vec3 up = std::abs(normal.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			outTangent = glm::normalize(glm::cross(up, normal));
			outBitangent = glm::cross(normal, outTangent);
		}

		glm::vec3 TangentToWorld(const glm::vec3& sample, const glm::vec3& normal)
		{
			glm::vec3 tangent;
			glm::vec3 bitangent;
			BuildTangentFrame(normal, tangent, bitangent);
			return glm::normalize(tangent * sample.x + bitangent * sample.y + normal * sample.z);
		}

		// Cosine-weighted hemisphere sample: the estimator of (1/PI) * integral(L * cos)
		// is simply the average of L over these samples.
		glm::vec3 SampleCosineHemisphere(const glm::vec2& xi, const glm::vec3& normal)
		{
			const f32 phi = 2.0f * c_Pi * xi.x;
			const f32 radius = std::sqrt(xi.y);
			const glm::vec3 local(radius * std::cos(phi), radius * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - xi.y)));
			return TangentToWorld(local, normal);
		}

		glm::vec3 ImportanceSampleGgx(const glm::vec2& xi, const glm::vec3& normal, f32 roughness)
		{
			const f32 alpha = roughness * roughness;
			const f32 phi = 2.0f * c_Pi * xi.x;
			const f32 cosTheta = std::sqrt(std::max(0.0f, (1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y)));
			const f32 sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
			const glm::vec3 local(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
			return TangentToWorld(local, normal);
		}

		f32 GeometrySchlickGgx(f32 nDotX, f32 roughness)
		{
			const f32 r = roughness + 1.0f;
			const f32 k = (r * r) / 8.0f;
			return nDotX / std::max(nDotX * (1.0f - k) + k, 1.0e-6f);
		}

		EnvironmentMap::CubeFaces MakeFaces(u32 size)
		{
			EnvironmentMap::CubeFaces faces;
			for (auto& face : faces)
			{
				face.resize(static_cast<usize>(size) * size);
			}
			return faces;
		}

		void BuildIrradiance(const Image& equirect, u32 size, u32 sampleCount, EnvironmentMap& out)
		{
			out.IrradianceSize = size;
			out.Irradiance = MakeFaces(size);
			for (u32 face = 0; face < 6; ++face)
			{
				for (u32 y = 0; y < size; ++y)
				{
					for (u32 x = 0; x < size; ++x)
					{
						const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(size) * 2.0f - 1.0f;
						const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(size) * 2.0f - 1.0f;
						const glm::vec3 normal = EnvironmentMap::CubeDirection(face, u, v);
						glm::vec3 sum(0.0f);
						for (u32 sample = 0; sample < sampleCount; ++sample)
						{
							const glm::vec3 direction = SampleCosineHemisphere(Hammersley(sample, sampleCount), normal);
							sum += EnvironmentMap::SampleEquirect(equirect, direction);
						}
						out.Irradiance[face][static_cast<usize>(y) * size + x] = sum / static_cast<f32>(sampleCount);
					}
				}
			}
		}

		void BuildSpecular(const Image& equirect, u32 size, u32 mipCount, u32 sampleCount, EnvironmentMap& out)
		{
			out.SpecularSize = size;
			out.SpecularMipCount = mipCount;
			out.Specular.clear();
			out.Specular.reserve(mipCount);
			for (u32 mip = 0; mip < mipCount; ++mip)
			{
				const u32 mipSize = std::max(1u, size >> mip);
				const f32 roughness = mipCount > 1 ? static_cast<f32>(mip) / static_cast<f32>(mipCount - 1) : 0.0f;
				const u32 samples = std::max(32u, sampleCount >> mip);
				EnvironmentMap::CubeFaces faces = MakeFaces(mipSize);
				for (u32 face = 0; face < 6; ++face)
				{
					for (u32 y = 0; y < mipSize; ++y)
					{
						for (u32 x = 0; x < mipSize; ++x)
						{
							const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(mipSize) * 2.0f - 1.0f;
							const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(mipSize) * 2.0f - 1.0f;
							const glm::vec3 normal = EnvironmentMap::CubeDirection(face, u, v);
							glm::vec3 sum(0.0f);
							f32 weight = 0.0f;
							for (u32 sample = 0; sample < samples; ++sample)
							{
								const glm::vec3 halfVector = ImportanceSampleGgx(Hammersley(sample, samples), normal, roughness);
								const glm::vec3 direction = glm::normalize(2.0f * glm::dot(normal, halfVector) * halfVector - normal);
								const f32 nDotL = glm::dot(normal, direction);
								if (nDotL <= 0.0f)
								{
									continue;
								}
								sum += EnvironmentMap::SampleEquirect(equirect, direction) * nDotL;
								weight += nDotL;
							}
							faces[face][static_cast<usize>(y) * mipSize + x] = weight > 0.0f ? sum / weight : glm::vec3(0.0f);
						}
					}
				}
				out.Specular.push_back(std::move(faces));
			}
		}

		void BuildBrdfLut(u32 size, u32 sampleCount, EnvironmentMap& out)
		{
			out.BrdfSize = size;
			out.Brdf.resize(static_cast<usize>(size) * size);
			for (u32 y = 0; y < size; ++y)
			{
				const f32 roughness = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(size);
				for (u32 x = 0; x < size; ++x)
				{
					const f32 nDotV = std::max((static_cast<f32>(x) + 0.5f) / static_cast<f32>(size), 0.001f);
					const glm::vec3 view(std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV);
					const glm::vec3 normal(0.0f, 0.0f, 1.0f);
					f32 scale = 0.0f;
					f32 bias = 0.0f;
					for (u32 sample = 0; sample < sampleCount; ++sample)
					{
						const glm::vec3 halfVector = ImportanceSampleGgx(Hammersley(sample, sampleCount), normal, roughness);
						const f32 vDotH = std::max(glm::dot(view, halfVector), 0.0f);
						const glm::vec3 lightDirection = glm::normalize(2.0f * vDotH * halfVector - view);
						const f32 nDotL = std::max(lightDirection.z, 0.0f);
						const f32 nDotH = std::max(halfVector.z, 0.0f);
						if (nDotL <= 0.0f)
						{
							continue;
						}
						const f32 geometry = GeometrySchlickGgx(nDotV, roughness) * GeometrySchlickGgx(nDotL, roughness);
						const f32 visibility = geometry * vDotH / std::max(nDotH * nDotV, 1.0e-6f);
						const f32 fresnel = std::pow(1.0f - vDotH, 5.0f);
						scale += (1.0f - fresnel) * visibility;
						bias += fresnel * visibility;
					}
					out.Brdf[static_cast<usize>(y) * size + x] = glm::vec2(scale / static_cast<f32>(sampleCount),
						bias / static_cast<f32>(sampleCount));
				}
			}
		}
	}

	bool EnvironmentMap::IsValid() const
	{
		return IrradianceSize > 0 && SpecularSize > 0 && SpecularMipCount > 0 && !Specular.empty() && BrdfSize > 0 &&
			   !Brdf.empty();
	}

	f32 EnvironmentMap::GetMipRoughness(u32 mip) const
	{
		if (SpecularMipCount <= 1)
		{
			return 0.0f;
		}
		return static_cast<f32>(mip) / static_cast<f32>(SpecularMipCount - 1);
	}

	glm::vec3 EnvironmentMap::SkyRadiance(const glm::vec3& direction)
	{
		const f32 up = glm::clamp(direction.y, -1.0f, 1.0f);
		const glm::vec3 zenith(0.14f, 0.22f, 0.42f);
		const glm::vec3 horizon(0.58f, 0.63f, 0.72f);
		const glm::vec3 ground(0.10f, 0.09f, 0.08f);
		if (up >= 0.0f)
		{
			return glm::mix(horizon, zenith, std::pow(up, 0.65f));
		}
		return glm::mix(horizon * 0.55f, ground, glm::clamp(-up * 2.5f, 0.0f, 1.0f));
	}

	glm::vec3 EnvironmentMap::CubeDirection(u32 face, f32 u, f32 v)
	{
		glm::vec3 direction(0.0f);
		switch (face)
		{
			case 0: direction = glm::vec3(1.0f, -v, -u); break;
			case 1: direction = glm::vec3(-1.0f, -v, u); break;
			case 2: direction = glm::vec3(u, 1.0f, v); break;
			case 3: direction = glm::vec3(u, -1.0f, -v); break;
			case 4: direction = glm::vec3(u, -v, 1.0f); break;
			default: direction = glm::vec3(-u, -v, -1.0f); break;
		}
		return glm::normalize(direction);
	}

	glm::vec3 EnvironmentMap::SampleEquirect(const Image& equirect, const glm::vec3& direction)
	{
		if (!equirect.IsValid())
		{
			return glm::vec3(0.0f);
		}
		const glm::vec3 d = glm::normalize(direction);
		const f32 phi = std::atan2(d.z, d.x);
		const f32 theta = std::asin(glm::clamp(d.y, -1.0f, 1.0f));
		const f32 u = phi / (2.0f * c_Pi) + 0.5f;
		const f32 v = 0.5f - theta / c_Pi;

		const u32 width = equirect.GetMipWidth(0);
		const u32 height = equirect.GetMipHeight(0);
		const std::vector<f32>& pixels = equirect.GetMipPixels(0);

		const f32 pixelX = u * static_cast<f32>(width) - 0.5f;
		const f32 pixelY = glm::clamp(v, 0.0f, 1.0f) * static_cast<f32>(height) - 0.5f;
		const i32 x0 = static_cast<i32>(std::floor(pixelX));
		const i32 y0 = static_cast<i32>(std::floor(pixelY));
		const f32 fx = pixelX - static_cast<f32>(x0);
		const f32 fy = pixelY - static_cast<f32>(y0);

		const auto texel = [&](i32 x, i32 y) -> glm::vec3
		{
			const i32 wrappedX = ((x % static_cast<i32>(width)) + static_cast<i32>(width)) % static_cast<i32>(width);
			const i32 clampedY = glm::clamp(y, 0, static_cast<i32>(height) - 1);
			const usize index = (static_cast<usize>(clampedY) * width + static_cast<u32>(wrappedX)) * 4;
			return glm::vec3(pixels[index], pixels[index + 1], pixels[index + 2]);
		};

		const glm::vec3 top = glm::mix(texel(x0, y0), texel(x0 + 1, y0), fx);
		const glm::vec3 bottom = glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), fx);
		return glm::mix(top, bottom, fy);
	}

	Image EnvironmentMap::BuildProceduralSkyEquirect(u32 width, u32 height)
	{
		width = std::max(width, 4u);
		height = std::max(height, 2u);
		std::vector<f32> pixels(static_cast<usize>(width) * height * 4);
		for (u32 y = 0; y < height; ++y)
		{
			const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(height);
			const f32 theta = (0.5f - v) * c_Pi;
			for (u32 x = 0; x < width; ++x)
			{
				const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(width);
				const f32 phi = (u - 0.5f) * 2.0f * c_Pi;
				const glm::vec3 direction(std::cos(theta) * std::cos(phi), std::sin(theta),
					std::cos(theta) * std::sin(phi));
				const glm::vec3 radiance = SkyRadiance(direction);
				const usize index = (static_cast<usize>(y) * width + x) * 4;
				pixels[index] = radiance.x;
				pixels[index + 1] = radiance.y;
				pixels[index + 2] = radiance.z;
				pixels[index + 3] = 1.0f;
			}
		}
		return Image::FromLinearRgba(width, height, std::move(pixels));
	}

	EnvironmentMap EnvironmentMap::BuildProceduralSky(u32 specularSize, u32 irradianceSize, u32 sampleCount,
		u32 brdfSize)
	{
		return BuildFromEquirect(BuildProceduralSkyEquirect(), specularSize, irradianceSize, sampleCount, brdfSize);
	}

	EnvironmentMap EnvironmentMap::BuildFromEquirect(const Image& equirect, u32 specularSize, u32 irradianceSize,
		u32 sampleCount, u32 brdfSize)
	{
		EnvironmentMap result;
		if (!equirect.IsValid())
		{
			return result;
		}

		specularSize = std::max(specularSize, 8u);
		irradianceSize = std::max(irradianceSize, 4u);
		brdfSize = std::max(brdfSize, 8u);
		sampleCount = std::max(sampleCount, 16u);

		u32 mipCount = 1;
		while ((specularSize >> mipCount) >= 4u)
		{
			++mipCount;
		}

		BuildIrradiance(equirect, irradianceSize, sampleCount, result);
		BuildSpecular(equirect, specularSize, mipCount, sampleCount, result);
		BuildBrdfLut(brdfSize, std::max(sampleCount / 2u, 32u), result);
		return result;
	}

	std::optional<EnvironmentMap> EnvironmentMap::LoadFromHdrFile(const FileSystem::Path& path, u32 specularSize,
		u32 irradianceSize, u32 sampleCount, u32 brdfSize)
	{
		const std::optional<Image> image = Image::LoadFromFile(path, TextureColorSpace::Linear);
		if (!image.has_value())
		{
			AE_CORE_ERROR("Could not load the HDRI '{}'", FileSystem::ToUtf8(path));
			return std::nullopt;
		}
		return BuildFromEquirect(*image, specularSize, irradianceSize, sampleCount, brdfSize);
	}
}
