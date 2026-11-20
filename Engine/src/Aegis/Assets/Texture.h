#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/FileSystem.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <optional>
#include <string>
#include <vector>

namespace aegis
{
	enum class TextureFilter : u8
	{
		Nearest = 0,
		Linear
	};

	enum class TextureWrapMode : u8
	{
		Repeat = 0,
		Clamp,
		Mirror
	};

	// Every texture owns the sampler it is meant to be read with, so a material can
	// mix an anisotropic albedo map with a clamped, unfiltered data map.
	struct SamplerSettings
	{
		TextureFilter MinFilter = TextureFilter::Linear;
		TextureFilter MagFilter = TextureFilter::Linear;
		TextureFilter MipFilter = TextureFilter::Linear;
		TextureWrapMode AddressU = TextureWrapMode::Repeat;
		TextureWrapMode AddressV = TextureWrapMode::Repeat;
		TextureWrapMode AddressW = TextureWrapMode::Repeat;
		f32 MaxAnisotropy = 4.0f;

		static SamplerSettings Albedo();
		static SamplerSettings NormalMap();
		static SamplerSettings Clamped();
		static SamplerSettings Point();
	};

	// How the pixel data of a file is encoded. Colour maps are authored in sRGB
	// and must be linearised before filtering; data maps are already linear.
	enum class TextureColorSpace : u8
	{
		Auto = 0, // sRGB for LDR files, linear for .hdr
		Srgb,
		Linear
	};

	// CPU-side texture. Pixels are always stored as linear RGBA32F so filtering,
	// mip generation and the GPU upload share one code path.
	class Image
	{
	public:
		static std::optional<Image> LoadFromFile(const FileSystem::Path& path,
			TextureColorSpace colorSpace = TextureColorSpace::Auto);
		// Decodes PNG/JPG/HDR bytes that are already in memory (embedded glTF images).
		static std::optional<Image> DecodeFromMemory(const u8* bytes, usize size, TextureColorSpace colorSpace,
			const std::string& name = {});

		// Bytes holds width * height * 4 values in row-major RGBA order.
		static Image FromRgba8(u32 width, u32 height, const u8* bytes, TextureColorSpace colorSpace);
		static Image CreateSolid(u32 width, u32 height, const glm::vec4& linearColor);
		// Builds an image from linear RGBA32F pixels (HDR environment authors, IBL
		// precomputation and tests). Generates no mips; call GenerateMips().
		static Image FromLinearRgba(u32 width, u32 height, std::vector<f32> linearPixels);
		static Image CreateCheckerboard(u32 width, u32 height, u32 cellPixels, const glm::vec3& colorA,
			const glm::vec3& colorB);

		bool IsValid() const { return m_Width > 0 && m_Height > 0 && !m_Mips.empty(); }
		u32 GetWidth() const { return m_Width; }
		u32 GetHeight() const { return m_Height; }
		u32 GetMipCount() const { return static_cast<u32>(m_Mips.size()); }

		// Linear RGBA32F texels of one level, width * height * 4 floats.
		const std::vector<f32>& GetMipPixels(u32 mipLevel) const;
		u32 GetMipWidth(u32 mipLevel) const;
		u32 GetMipHeight(u32 mipLevel) const;

		const std::string& GetName() const { return m_Name; }
		void SetName(std::string name) { m_Name = std::move(name); }

		// Builds the box-filtered chain down to 1x1 from the base level. Calling it
		// again rebuilds the chain instead of appending to it.
		void GenerateMips();

		static f32 SrgbToLinear(f32 value);
		static f32 LinearToSrgb(f32 value);

	private:
		static Image create(u32 width, u32 height, std::vector<f32> basePixels);
		static u32 calculateMipWidth(u32 width, u32 mipLevel);
		static u32 calculateMipHeight(u32 height, u32 mipLevel);

		u32 m_Width = 0;
		u32 m_Height = 0;
		std::vector<std::vector<f32>> m_Mips;
		std::string m_Name;
	};

	// GPU texture plus its sampler. Creation uploads the whole mip chain through
	// NVRHI; the CPU image is not retained.
	class Texture
	{
	public:
		static Ref<Texture> Create(nvrhi::IDevice* device, const Image& image, const SamplerSettings& sampler,
			const std::string& debugName = {});
		static Ref<Texture> CreateWhite(nvrhi::IDevice* device);
		// 1x1 "no perturbation" tangent-space normal, (0.5, 0.5, 1.0) encoded.
		static Ref<Texture> CreateFlatNormal(nvrhi::IDevice* device);

		bool IsValid() const { return m_Texture != nullptr && m_Sampler != nullptr; }
		nvrhi::ITexture* GetTexture() const { return m_Texture.Get(); }
		nvrhi::ISampler* GetSampler() const { return m_Sampler.Get(); }
		u32 GetWidth() const { return m_Width; }
		u32 GetHeight() const { return m_Height; }
		u32 GetMipCount() const { return m_MipCount; }
		const std::string& GetDebugName() const { return m_DebugName; }

	private:
		nvrhi::TextureHandle m_Texture;
		nvrhi::SamplerHandle m_Sampler;
		u32 m_Width = 0;
		u32 m_Height = 0;
		u32 m_MipCount = 0;
		std::string m_DebugName;
	};
}
