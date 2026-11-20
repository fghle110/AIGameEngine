#include "Aegis/Assets/Texture.h"

#include "Aegis/Core/Log.h"

#include <glm/packing.hpp>

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aegis
{
	namespace
	{
		nvrhi::SamplerAddressMode ToNvrhiAddressMode(TextureWrapMode mode)
		{
			switch (mode)
			{
				case TextureWrapMode::Repeat: return nvrhi::SamplerAddressMode::Repeat;
				case TextureWrapMode::Clamp: return nvrhi::SamplerAddressMode::Clamp;
				case TextureWrapMode::Mirror: return nvrhi::SamplerAddressMode::Mirror;
			}
			return nvrhi::SamplerAddressMode::Repeat;
		}
	}

	SamplerSettings SamplerSettings::Albedo()
	{
		SamplerSettings settings;
		settings.MaxAnisotropy = 4.0f;
		return settings;
	}

	SamplerSettings SamplerSettings::NormalMap()
	{
		SamplerSettings settings;
		settings.MaxAnisotropy = 4.0f;
		return settings;
	}

	SamplerSettings SamplerSettings::Clamped()
	{
		SamplerSettings settings;
		settings.AddressU = TextureWrapMode::Clamp;
		settings.AddressV = TextureWrapMode::Clamp;
		settings.AddressW = TextureWrapMode::Clamp;
		settings.MaxAnisotropy = 1.0f;
		return settings;
	}

	SamplerSettings SamplerSettings::Point()
	{
		SamplerSettings settings;
		settings.MinFilter = TextureFilter::Nearest;
		settings.MagFilter = TextureFilter::Nearest;
		settings.MipFilter = TextureFilter::Nearest;
		settings.MaxAnisotropy = 1.0f;
		return settings;
	}

	f32 Image::SrgbToLinear(f32 value)
	{
		const f32 clamped = Max(value, 0.0f);
		if (clamped <= 0.04045f)
		{
			return clamped / 12.92f;
		}
		return std::pow((clamped + 0.055f) / 1.055f, 2.4f);
	}

	f32 Image::LinearToSrgb(f32 value)
	{
		const f32 clamped = Max(value, 0.0f);
		if (clamped <= 0.0031308f)
		{
			return clamped * 12.92f;
		}
		return 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
	}

	u32 Image::calculateMipWidth(u32 width, u32 mipLevel)
	{
		const u32 shifted = mipLevel >= 32 ? 0u : (width >> mipLevel);
		return Max(shifted, 1u);
	}

	u32 Image::calculateMipHeight(u32 height, u32 mipLevel)
	{
		const u32 shifted = mipLevel >= 32 ? 0u : (height >> mipLevel);
		return Max(shifted, 1u);
	}

	Image Image::create(u32 width, u32 height, std::vector<f32> basePixels)
	{
		Image image;
		const usize expected = static_cast<usize>(width) * height * 4;
		if (width == 0 || height == 0 || basePixels.size() != expected)
		{
			AE_CORE_ERROR("Image data is {} floats, but {}x{} RGBA needs {}", basePixels.size(), width, height,
				expected);
			return image;
		}

		image.m_Width = width;
		image.m_Height = height;
		image.m_Mips.push_back(std::move(basePixels));
		image.GenerateMips();
		return image;
	}

	Image Image::FromRgba8(u32 width, u32 height, const u8* bytes, TextureColorSpace colorSpace)
	{
		if (bytes == nullptr)
		{
			AE_CORE_ERROR("Image::FromRgba8 needs pixel data");
			return Image();
		}

		const usize texelCount = static_cast<usize>(width) * height;
		std::vector<f32> pixels(texelCount * 4, 0.0f);
		const bool linearise = colorSpace != TextureColorSpace::Linear;
		for (usize i = 0; i < texelCount * 4; ++i)
		{
			const f32 normalized = static_cast<f32>(bytes[i]) / 255.0f;
			// Alpha is coverage, never a colour, so it is not sRGB encoded.
			const bool isAlpha = (i % 4) == 3;
			pixels[i] = (linearise && !isAlpha) ? SrgbToLinear(normalized) : normalized;
		}
		return create(width, height, std::move(pixels));
	}

	Image Image::FromLinearRgba(u32 width, u32 height, std::vector<f32> linearPixels)
	{
		Image image;
		if (width == 0 || height == 0 || linearPixels.size() < static_cast<usize>(width) * height * 4)
		{
			return image;
		}
		image.m_Width = width;
		image.m_Height = height;
		image.m_Mips.push_back(std::move(linearPixels));
		return image;
	}

	Image Image::CreateSolid(u32 width, u32 height, const glm::vec4& linearColor)
	{
		std::vector<f32> pixels(static_cast<usize>(width) * height * 4, 0.0f);
		for (usize i = 0; i < pixels.size(); i += 4)
		{
			pixels[i + 0] = linearColor.r;
			pixels[i + 1] = linearColor.g;
			pixels[i + 2] = linearColor.b;
			pixels[i + 3] = linearColor.a;
		}
		return create(width, height, std::move(pixels));
	}

	Image Image::CreateCheckerboard(u32 width, u32 height, u32 cellPixels, const glm::vec3& colorA,
		const glm::vec3& colorB)
	{
		const u32 cell = Max(cellPixels, 1u);
		std::vector<f32> pixels(static_cast<usize>(width) * height * 4, 0.0f);
		for (u32 y = 0; y < height; ++y)
		{
			for (u32 x = 0; x < width; ++x)
			{
				const bool alternate = ((x / cell) + (y / cell)) % 2 == 0;
				const glm::vec3& color = alternate ? colorA : colorB;
				const usize offset = (static_cast<usize>(y) * width + x) * 4;
				pixels[offset + 0] = color.r;
				pixels[offset + 1] = color.g;
				pixels[offset + 2] = color.b;
				pixels[offset + 3] = 1.0f;
			}
		}
		return create(width, height, std::move(pixels));
	}

	std::optional<Image> Image::LoadFromFile(const FileSystem::Path& path, TextureColorSpace colorSpace)
	{
		std::vector<u8> fileBytes;
		if (!FileSystem::TryReadBinaryFile(path, fileBytes))
		{
			AE_CORE_ERROR("Could not read the texture file '{}'", FileSystem::ToUtf8(path));
			return std::nullopt;
		}
		return DecodeFromMemory(fileBytes.data(), fileBytes.size(), colorSpace, FileSystem::GetFileName(path));
	}

	std::optional<Image> Image::DecodeFromMemory(const u8* bytes, usize size, TextureColorSpace colorSpace,
		const std::string& name)
	{
		if (bytes == nullptr || size == 0)
		{
			AE_CORE_ERROR("Cannot decode an empty image buffer ({})", name);
			return std::nullopt;
		}

		const int byteCount = static_cast<int>(size);
		int width = 0;
		int height = 0;
		int channels = 0;

		const bool isHdr = stbi_is_hdr_from_memory(bytes, byteCount) != 0;
		Image image;
		if (isHdr)
		{
			if (colorSpace == TextureColorSpace::Srgb)
			{
				AE_CORE_WARN("'{}' is an HDR image; ignoring the sRGB colour space request", name);
			}
			f32* pixels = stbi_loadf_from_memory(bytes, byteCount, &width, &height, &channels, 4);
			if (pixels == nullptr)
			{
				AE_CORE_ERROR("Could not decode the HDR image '{}': {}", name, stbi_failure_reason());
				return std::nullopt;
			}
			std::vector<f32> linear(static_cast<usize>(width) * height * 4);
			std::memcpy(linear.data(), pixels, linear.size() * sizeof(f32));
			stbi_image_free(pixels);
			image = create(static_cast<u32>(width), static_cast<u32>(height), std::move(linear));
		}
		else
		{
			u8* pixels = stbi_load_from_memory(bytes, byteCount, &width, &height, &channels, 4);
			if (pixels == nullptr)
			{
				AE_CORE_ERROR("Could not decode the image '{}': {}", name, stbi_failure_reason());
				return std::nullopt;
			}
			image = FromRgba8(static_cast<u32>(width), static_cast<u32>(height), pixels, colorSpace);
			stbi_image_free(pixels);
		}

		if (!image.IsValid())
		{
			return std::nullopt;
		}
		image.SetName(name);
		return image;
	}

	const std::vector<f32>& Image::GetMipPixels(u32 mipLevel) const
	{
		static const std::vector<f32> s_Empty;
		if (m_Mips.empty())
		{
			AE_ASSERT(false, "Image::GetMipPixels called on an empty image");
			return s_Empty;
		}
		const u32 mip = Min(mipLevel, GetMipCount() - 1);
		return m_Mips[mip];
	}

	u32 Image::GetMipWidth(u32 mipLevel) const
	{
		return calculateMipWidth(m_Width, Min(mipLevel, GetMipCount() - 1));
	}

	u32 Image::GetMipHeight(u32 mipLevel) const
	{
		return calculateMipHeight(m_Height, Min(mipLevel, GetMipCount() - 1));
	}

	void Image::GenerateMips()
	{
		if (m_Mips.empty() || m_Width == 0 || m_Height == 0)
		{
			return;
		}
		m_Mips.resize(1);

		u32 width = m_Width;
		u32 height = m_Height;
		while (width > 1 || height > 1)
		{
			const u32 nextWidth = Max(width / 2, 1u);
			const u32 nextHeight = Max(height / 2, 1u);
			const std::vector<f32>& source = m_Mips.back();
			std::vector<f32> destination(static_cast<usize>(nextWidth) * nextHeight * 4, 0.0f);

			for (u32 y = 0; y < nextHeight; ++y)
			{
				// Clamping the taps keeps odd-sized levels (the 1xN tail) correct: the
				// last row/column is averaged with itself instead of out of bounds.
				const u32 y0 = Min(y * 2, height - 1);
				const u32 y1 = Min(y * 2 + 1, height - 1);
				for (u32 x = 0; x < nextWidth; ++x)
				{
					const u32 x0 = Min(x * 2, width - 1);
					const u32 x1 = Min(x * 2 + 1, width - 1);
					const usize offset = (static_cast<usize>(y) * nextWidth + x) * 4;
					for (u32 channel = 0; channel < 4; ++channel)
					{
						const f32 topLeft = source[(static_cast<usize>(y0) * width + x0) * 4 + channel];
						const f32 topRight = source[(static_cast<usize>(y0) * width + x1) * 4 + channel];
						const f32 bottomLeft = source[(static_cast<usize>(y1) * width + x0) * 4 + channel];
						const f32 bottomRight = source[(static_cast<usize>(y1) * width + x1) * 4 + channel];
						destination[offset + channel] = (topLeft + topRight + bottomLeft + bottomRight) * 0.25f;
					}
				}
			}

			m_Mips.push_back(std::move(destination));
			width = nextWidth;
			height = nextHeight;
		}
	}

	Ref<Texture> Texture::Create(nvrhi::IDevice* device, const Image& image, const SamplerSettings& sampler,
		const std::string& debugName)
	{
		if (device == nullptr || !image.IsValid())
		{
			AE_CORE_ERROR("Texture::Create needs a device and a valid image");
			return nullptr;
		}

		nvrhi::TextureDesc desc;
		desc.width = image.GetWidth();
		desc.height = image.GetHeight();
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.dimension = nvrhi::TextureDimension::Texture2D;
		desc.mipLevels = image.GetMipCount();
		desc.isShaderResource = true;
		desc.isRenderTarget = false;
		desc.isUAV = false;
		desc.debugName = debugName.empty() ? image.GetName() : debugName;
		// The texture is written once at load time and then sampled for many frames.
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;

		Ref<Texture> texture = CreateRef<Texture>();
		texture->m_Texture = device->createTexture(desc);
		if (texture->m_Texture == nullptr)
		{
			AE_CORE_ERROR("Could not create the GPU texture '{}'", desc.debugName);
			return nullptr;
		}

		nvrhi::SamplerDesc samplerDesc;
		samplerDesc.setMinFilter(sampler.MinFilter == TextureFilter::Linear);
		samplerDesc.setMagFilter(sampler.MagFilter == TextureFilter::Linear);
		samplerDesc.setMipFilter(sampler.MipFilter == TextureFilter::Linear);
		samplerDesc.addressU = ToNvrhiAddressMode(sampler.AddressU);
		samplerDesc.addressV = ToNvrhiAddressMode(sampler.AddressV);
		samplerDesc.addressW = ToNvrhiAddressMode(sampler.AddressW);
		samplerDesc.setMaxAnisotropy(Max(sampler.MaxAnisotropy, 1.0f));
		texture->m_Sampler = device->createSampler(samplerDesc);
		if (texture->m_Sampler == nullptr)
		{
			AE_CORE_ERROR("Could not create the sampler for '{}'", desc.debugName);
			return nullptr;
		}

		nvrhi::CommandListHandle commandList = device->createCommandList();
		commandList->open();
		for (u32 mip = 0; mip < image.GetMipCount(); ++mip)
		{
			const std::vector<f32>& pixels = image.GetMipPixels(mip);
			std::vector<u32> packed(pixels.size() / 2, 0u);
			for (usize i = 0; i < packed.size(); ++i)
			{
				packed[i] = glm::packHalf2x16(glm::vec2(pixels[i * 2 + 0], pixels[i * 2 + 1]));
			}
			commandList->writeTexture(texture->m_Texture, 0, mip, packed.data(),
				static_cast<usize>(image.GetMipWidth(mip)) * 4 * sizeof(u16));
		}
		commandList->close();
		nvrhi::ICommandList* rawList = commandList;
		device->executeCommandLists(&rawList, 1);
		device->waitForIdle();

		texture->m_Width = image.GetWidth();
		texture->m_Height = image.GetHeight();
		texture->m_MipCount = image.GetMipCount();
		texture->m_DebugName = desc.debugName;
		AE_CORE_TRACE("Uploaded texture '{}' ({}x{}, {} mips)", desc.debugName, texture->m_Width, texture->m_Height,
			texture->m_MipCount);
		return texture;
	}

	Ref<Texture> Texture::CreateWhite(nvrhi::IDevice* device)
	{
		const Image image = Image::CreateSolid(1, 1, glm::vec4(1.0f));
		const SamplerSettings sampler = SamplerSettings::Clamped();
		return Create(device, image, sampler, "White fallback");
	}

	Ref<Texture> Texture::CreateFlatNormal(nvrhi::IDevice* device)
	{
		// A white texel would decode to (1, 1, 1), which is a 45 degree tilt, not the
		// untouched normal the shader expects when a material has no normal map.
		const Image image = Image::CreateSolid(1, 1, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
		const SamplerSettings sampler = SamplerSettings::Clamped();
		return Create(device, image, sampler, "Flat normal fallback");
	}
}
