#include "Aegis/Renderer/Renderer.h"

#include "Aegis/Core/Config.h"
#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Core/Window.h"
#include "Aegis/Renderer/Vulkan/VulkanRenderContext.h"
#include "Aegis/Renderer/Vulkan/VulkanSwapchain.h"

#include <stb_image_write.h>

#include <cstring>
#include <vector>

namespace aegis
{
	namespace
	{
	// Converts a BGRA8 or RGBA8 staging readback into an RGBA8 byte buffer.
	std::vector<u8> ConvertToRgba(const void* source, u32 width, u32 height, usize rowPitch, bool sourceIsBgra)
	{
		std::vector<u8> rgba(static_cast<usize>(width) * height * 4);
		const auto* sourceBytes = static_cast<const u8*>(source);
		for (u32 y = 0; y < height; ++y)
		{
			const u8* row = sourceBytes + y * rowPitch;
			u8* destination = rgba.data() + static_cast<usize>(y) * width * 4;
			for (u32 x = 0; x < width; ++x)
			{
				if (sourceIsBgra)
				{
					destination[x * 4 + 0] = row[x * 4 + 2];
					destination[x * 4 + 1] = row[x * 4 + 1];
					destination[x * 4 + 2] = row[x * 4 + 0];
					destination[x * 4 + 3] = row[x * 4 + 3];
				}
				else
				{
					std::memcpy(destination + x * 4, row + x * 4, 4);
				}
			}
		}
		return rgba;
	}
	}
}

namespace aegis
{
	Renderer::Renderer(const RendererSpecification& specification)
		: m_Specification(specification)
		, m_VSync(specification.VSync)
	{
		if (m_Specification.Window == nullptr)
		{
			AE_CORE_CRITICAL("Renderer requires a window");
			return;
		}

		VulkanContextSpecification contextSpecification;
		contextSpecification.Window = m_Specification.Window;
		contextSpecification.EnableValidation = m_Specification.EnableValidation;
		contextSpecification.ApplicationName = std::string(c_EngineName);

		m_Context = CreateScope<VulkanRenderContext>(contextSpecification);
		if (!m_Context->IsValid())
		{
			AE_CORE_CRITICAL("Vulkan render context could not be created");
			return;
		}

		m_GpuName = m_Context->GetGpuName();
		m_Width = m_Specification.Window->GetWidth();
		m_Height = m_Specification.Window->GetHeight();

		m_Swapchain = CreateScope<VulkanSwapchain>(*m_Context, m_Width, m_Height, m_VSync);
		if (!m_Swapchain->IsValid())
		{
			AE_CORE_CRITICAL("Swapchain could not be created");
			return;
		}

		createDepthTexture();
		createFramebuffers();

		m_CommandList = m_Context->GetNvrhiDevice()->createCommandList();
		if (m_CommandList == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the frame command list");
			return;
		}

		m_Initialized = true;
		AE_CORE_INFO("Renderer initialized ({}x{})", m_Width, m_Height);
	}

	Renderer::~Renderer()
	{
		if (m_Context != nullptr && m_Context->IsValid())
		{
			m_Context->GetNvrhiDevice()->waitForIdle();
		}
		m_CommandList = nullptr;
		destroyFramebuffers();
		m_DepthTexture = nullptr;
		m_Swapchain = nullptr;
		m_Context = nullptr;
	}

	void Renderer::createDepthTexture()
	{
		nvrhi::TextureDesc desc;
		desc.width = m_Width;
		desc.height = m_Height;
		desc.format = nvrhi::Format::D32;
		desc.dimension = nvrhi::TextureDimension::Texture2D;
		desc.isRenderTarget = true;
		desc.isShaderResource = false;
		desc.isUAV = false;
		desc.clearValue = nvrhi::Color(1.0f);
		desc.useClearValue = true;
		desc.debugName = "Scene depth";
		desc.initialState = nvrhi::ResourceStates::DepthWrite;
		desc.keepInitialState = true;

		m_DepthTexture = m_Context->GetNvrhiDevice()->createTexture(desc);
	}

	void Renderer::createFramebuffers()
	{
		destroyFramebuffers();
		const u32 imageCount = m_Swapchain->GetImageCount();
		m_Framebuffers.reserve(imageCount);

		for (u32 i = 0; i < imageCount; ++i)
		{
			nvrhi::FramebufferDesc desc;
			desc.addColorAttachment(m_Swapchain->GetImage(i));
			desc.setDepthAttachment(m_DepthTexture);
			nvrhi::FramebufferHandle framebuffer = m_Context->GetNvrhiDevice()->createFramebuffer(desc);
			if (framebuffer == nullptr)
			{
				AE_CORE_CRITICAL("Could not create swapchain framebuffer {}", i);
			}
			m_Framebuffers.push_back(std::move(framebuffer));
		}
	}

	void Renderer::destroyFramebuffers()
	{
		m_Framebuffers.clear();
	}

	bool Renderer::recreateSwapchainIfNeeded()
	{
		const u32 windowWidth = m_Specification.Window->GetWidth();
		const u32 windowHeight = m_Specification.Window->GetHeight();
		if (windowWidth == 0 || windowHeight == 0)
		{
			return false; // minimised
		}

		const bool sizeChanged = windowWidth != m_Width || windowHeight != m_Height;
		if (!m_Swapchain->NeedsRecreate() && !sizeChanged)
		{
			return true;
		}

		m_Width = windowWidth;
		m_Height = windowHeight;
		if (!m_Swapchain->Recreate(m_Width, m_Height, m_VSync))
		{
			AE_CORE_ERROR("Could not recreate the swapchain");
			return false;
		}

		createDepthTexture();
		createFramebuffers();
		AE_CORE_INFO("Render targets resized to {}x{}", m_Width, m_Height);
		return true;
	}

	bool Renderer::BeginFrame(const glm::vec4& clearColor)
	{
		if (!m_Initialized || m_Swapchain == nullptr)
		{
			return false;
		}
		if (!recreateSwapchainIfNeeded())
		{
			return false;
		}

		m_ImageIndex = m_Swapchain->AcquireImage();
		if (m_ImageIndex == VulkanSwapchain::c_InvalidImage)
		{
			return false;
		}
		if (m_ImageIndex >= m_Framebuffers.size())
		{
			createFramebuffers();
			if (m_ImageIndex >= m_Framebuffers.size())
			{
				return false;
			}
		}

		m_CommandList->open();
		if (m_Specification.EnableDebugNames)
		{
			m_CommandList->beginMarker("Aegis Frame");
		}

		nvrhi::ITexture* colorTarget = m_Swapchain->GetImage(m_ImageIndex);
		m_CurrentColorTarget = colorTarget;
		m_CommandList->clearTextureFloat(colorTarget, nvrhi::AllSubresources,
			nvrhi::Color(clearColor.r, clearColor.g, clearColor.b, clearColor.a));
		m_CommandList->clearDepthStencilTexture(m_DepthTexture, nvrhi::AllSubresources, true, 1.0f, false, 0);

		m_FrameActive = true;
		return true;
	}

	void Renderer::EndFrame()
	{
		if (!m_FrameActive || m_CommandList == nullptr)
		{
			return;
		}

		nvrhi::IDevice* device = m_Context->GetNvrhiDevice();

		nvrhi::StagingTextureHandle screenshotStaging;
		if (!m_ScreenshotPath.empty() && m_CurrentColorTarget != nullptr &&
			m_CurrentColorTarget->getDesc().sampleCount == 1)
		{
			screenshotStaging = device->createStagingTexture(m_CurrentColorTarget->getDesc(),
				nvrhi::CpuAccessMode::Read);
			if (screenshotStaging != nullptr)
			{
				m_CommandList->copyTexture(screenshotStaging, nvrhi::TextureSlice(), m_CurrentColorTarget,
					nvrhi::TextureSlice());
			}
		}

		if (m_Specification.EnableDebugNames)
		{
			m_CommandList->endMarker();
		}
		m_CommandList->close();

		nvrhi::ICommandList* commandList = m_CommandList.Get();
		const u64 submissionId = device->executeCommandLists(&commandList, 1);

		if (screenshotStaging != nullptr)
		{
			device->waitForIdle();
			usize rowPitch = 0;
			if (void* mapped = device->mapStagingTexture(screenshotStaging, nvrhi::TextureSlice(),
					nvrhi::CpuAccessMode::Read, &rowPitch))
			{
				const nvrhi::TextureDesc desc = m_CurrentColorTarget->getDesc();
				const bool isBgra = desc.format == nvrhi::Format::BGRA8_UNORM ||
									desc.format == nvrhi::Format::SBGRA8_UNORM;
				const std::vector<u8> rgba = ConvertToRgba(mapped, desc.width, desc.height, rowPitch, isBgra);
				device->unmapStagingTexture(screenshotStaging);

				FileSystem::EnsureParentDirectory(FileSystem::FromUtf8(m_ScreenshotPath));
				const std::string path = FileSystem::ToUtf8(FileSystem::FromUtf8(m_ScreenshotPath));
				if (stbi_write_png(path.c_str(), static_cast<int>(desc.width), static_cast<int>(desc.height), 4,
						rgba.data(), static_cast<int>(desc.width) * 4) != 0)
				{
					AE_CORE_INFO("Screenshot written to '{}'", m_ScreenshotPath);
				}
				else
				{
					AE_CORE_ERROR("Could not write the screenshot to '{}'", m_ScreenshotPath);
				}
			}
			else
			{
				AE_CORE_ERROR("Could not map the screenshot staging texture");
			}
			m_ScreenshotPath.clear();
		}

		m_Swapchain->Present(m_ImageIndex, submissionId);
		device->runGarbageCollection();

		m_FrameActive = false;
		m_CurrentColorTarget = nullptr;
	}

	void Renderer::RequestScreenshot(std::string filePath)
	{
		m_ScreenshotPath = std::move(filePath);
	}

	void Renderer::Resize(u32 width, u32 height)
	{
		// The actual recreation happens in BeginFrame where the device is safe to
		// touch (the resize event can arrive while the GPU is still presenting).
		if (width > 0 && height > 0)
		{
			m_Specification.Window->SetVSync(m_VSync);
			(void)width;
			(void)height;
		}
	}

	void Renderer::SetVSync(bool enabled)
	{
		if (m_VSync == enabled)
		{
			return;
		}
		m_VSync = enabled;
		if (m_Swapchain != nullptr)
		{
			m_Swapchain->Recreate(m_Width, m_Height, m_VSync);
			createDepthTexture();
			createFramebuffers();
		}
	}

	nvrhi::IDevice* Renderer::GetDevice() const
	{
		return m_Context != nullptr ? m_Context->GetNvrhiDevice() : nullptr;
	}

	nvrhi::ICommandList* Renderer::GetCommandList() const
	{
		return m_CommandList.Get();
	}

	nvrhi::IFramebuffer* Renderer::GetFramebuffer() const
	{
		return m_ImageIndex < m_Framebuffers.size() ? m_Framebuffers[m_ImageIndex].Get() : nullptr;
	}

	VulkanRenderContext* Renderer::GetContext() const
	{
		return m_Context.get();
	}

	f32 Renderer::GetAspectRatio() const
	{
		return m_Height == 0 ? 1.0f : static_cast<f32>(m_Width) / static_cast<f32>(m_Height);
	}

	const std::string& Renderer::GetGpuName() const
	{
		return m_GpuName;
	}
}
