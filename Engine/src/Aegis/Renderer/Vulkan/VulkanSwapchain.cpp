#include "Aegis/Renderer/Vulkan/VulkanSwapchain.h"

#include "Aegis/Core/Log.h"

#include <volk.h>

#include <nvrhi/vulkan.h>

#include <algorithm>
#include <limits>

namespace aegis
{
	namespace
	{
		nvrhi::Format ToNvrhiFormat(VkFormat format)
		{
			switch (format)
			{
				case VK_FORMAT_B8G8R8A8_SRGB: return nvrhi::Format::SBGRA8_UNORM;
				case VK_FORMAT_B8G8R8A8_UNORM: return nvrhi::Format::BGRA8_UNORM;
				case VK_FORMAT_R8G8B8A8_SRGB: return nvrhi::Format::SRGBA8_UNORM;
				case VK_FORMAT_R8G8B8A8_UNORM: return nvrhi::Format::RGBA8_UNORM;
				case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return nvrhi::Format::R10G10B10A2_UNORM;
				default: break;
			}
			AE_CORE_WARN("Unsupported swapchain format {}; falling back to BGRA8_UNORM", static_cast<int>(format));
			return nvrhi::Format::BGRA8_UNORM;
		}

		VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
		{
			for (const auto& format : formats)
			{
				if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
					format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				{
					return format;
				}
			}
			for (const auto& format : formats)
			{
				if (format.format == VK_FORMAT_R8G8B8A8_SRGB &&
					format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				{
					return format;
				}
			}
			return formats.front();
		}

		VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync)
		{
			if (vsync)
			{
				return VK_PRESENT_MODE_FIFO_KHR;
			}
			for (const VkPresentModeKHR mode : modes)
			{
				if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
				{
					return mode;
				}
			}
			for (const VkPresentModeKHR mode : modes)
			{
				if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
				{
					return mode;
				}
			}
			return VK_PRESENT_MODE_FIFO_KHR;
		}
	}

	VulkanSwapchain::VulkanSwapchain(VulkanRenderContext& context, u32 width, u32 height, bool vsync)
		: m_Context(context)
		, m_RequestedWidth(width)
		, m_RequestedHeight(height)
		, m_VSync(vsync)
	{
		createSynchronisation();
		create();
	}

	VulkanSwapchain::~VulkanSwapchain()
	{
		destroy();
		destroySynchronisation();
	}

	void VulkanSwapchain::createSynchronisation()
	{
		VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

		if (vkCreateFence(m_Context.GetDevice(), &fenceInfo, nullptr, &m_AcquireFence) != VK_SUCCESS)
		{
			AE_CORE_CRITICAL("Could not create the swapchain acquire fence");
		}

		// The present fence starts signalled: the first AcquireImage may wait on it.
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		if (vkCreateFence(m_Context.GetDevice(), &fenceInfo, nullptr, &m_PresentFence) != VK_SUCCESS)
		{
			AE_CORE_CRITICAL("Could not create the swapchain present fence");
		}

		VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		if (vkCreateSemaphore(m_Context.GetDevice(), &semaphoreInfo, nullptr, &m_PresentSemaphore) != VK_SUCCESS)
		{
			AE_CORE_CRITICAL("Could not create the swapchain present semaphore");
		}
	}

	void VulkanSwapchain::destroySynchronisation()
	{
		const VkDevice device = m_Context.GetDevice();
		if (m_AcquireFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(device, m_AcquireFence, nullptr);
			m_AcquireFence = VK_NULL_HANDLE;
		}
		if (m_PresentFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(device, m_PresentFence, nullptr);
			m_PresentFence = VK_NULL_HANDLE;
		}
		if (m_PresentSemaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(device, m_PresentSemaphore, nullptr);
			m_PresentSemaphore = VK_NULL_HANDLE;
		}
	}

	void VulkanSwapchain::create()
	{
		VkSurfaceCapabilitiesKHR capabilities{};
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Context.GetPhysicalDevice(), m_Context.GetSurface(),
				&capabilities) != VK_SUCCESS)
		{
			AE_CORE_CRITICAL("Could not query swapchain capabilities");
			return;
		}

		u32 formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_Context.GetPhysicalDevice(), m_Context.GetSurface(), &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_Context.GetPhysicalDevice(), m_Context.GetSurface(), &formatCount,
			formats.data());

		u32 presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_Context.GetPhysicalDevice(), m_Context.GetSurface(),
			&presentModeCount, nullptr);
		std::vector<VkPresentModeKHR> presentModes(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_Context.GetPhysicalDevice(), m_Context.GetSurface(),
			&presentModeCount, presentModes.data());

		if (formats.empty() || presentModes.empty())
		{
			AE_CORE_CRITICAL("The window surface exposes no formats or present modes");
			return;
		}

		const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
		const VkPresentModeKHR presentMode = ChoosePresentMode(presentModes, m_VSync);

		VkExtent2D extent = capabilities.currentExtent;
		if (extent.width == std::numeric_limits<u32>::max())
		{
			extent.width = std::clamp(m_RequestedWidth, capabilities.minImageExtent.width,
				capabilities.maxImageExtent.width);
			extent.height = std::clamp(m_RequestedHeight, capabilities.minImageExtent.height,
				capabilities.maxImageExtent.height);
		}

		u32 imageCount = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
		{
			imageCount = capabilities.maxImageCount;
		}

		VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		for (const VkCompositeAlphaFlagBitsKHR candidate : { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
				  VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
				  VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR })
		{
			if ((capabilities.supportedCompositeAlpha & candidate) != 0)
			{
				compositeAlpha = candidate;
				break;
			}
		}

		VkSwapchainCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		createInfo.surface = m_Context.GetSurface();
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
								  VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.preTransform = capabilities.currentTransform;
		createInfo.compositeAlpha = compositeAlpha;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = m_Swapchain;

		const VkResult result = vkCreateSwapchainKHR(m_Context.GetDevice(), &createInfo, nullptr, &m_Swapchain);
		if (result != VK_SUCCESS)
		{
			AE_CORE_CRITICAL("vkCreateSwapchainKHR failed: {}", static_cast<int>(result));
			m_Swapchain = VK_NULL_HANDLE;
			return;
		}

		// The old swapchain (if any) is retired once the new one exists.
		if (createInfo.oldSwapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_Context.GetDevice(), createInfo.oldSwapchain, nullptr);
		}

		m_Format = surfaceFormat.format;
		m_NvrhiFormat = ToNvrhiFormat(m_Format);
		m_Extent = extent;
		m_Textures.clear();
		m_Images.clear();

		u32 swapchainImageCount = 0;
		vkGetSwapchainImagesKHR(m_Context.GetDevice(), m_Swapchain, &swapchainImageCount, nullptr);
		m_Images.resize(swapchainImageCount);
		vkGetSwapchainImagesKHR(m_Context.GetDevice(), m_Swapchain, &swapchainImageCount, m_Images.data());

		m_Textures.reserve(m_Images.size());
		nvrhi::IDevice* device = m_Context.GetNvrhiDevice();
		for (usize i = 0; i < m_Images.size(); ++i)
		{
			nvrhi::TextureDesc desc;
			desc.width = extent.width;
			desc.height = extent.height;
			desc.format = m_NvrhiFormat;
			desc.dimension = nvrhi::TextureDimension::Texture2D;
			desc.isRenderTarget = true;
			desc.isShaderResource = false;
			desc.isUAV = false;
			desc.debugName = std::format("Swapchain image {}", i);
			desc.enableAutomaticStateTracking(nvrhi::ResourceStates::Present);

			nvrhi::TextureHandle texture = device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image,
				nvrhi::Object(static_cast<u64>(reinterpret_cast<uintptr_t>(m_Images[i]))), desc);
			if (texture == nullptr)
			{
				AE_CORE_CRITICAL("Could not wrap swapchain image {}", i);
			}
			m_Textures.push_back(std::move(texture));
		}

		AE_CORE_INFO("Swapchain: {}x{}, {} images, {} format, present mode {}", m_Extent.width, m_Extent.height,
			static_cast<u32>(m_Textures.size()), static_cast<int>(m_Format), static_cast<int>(presentMode));

		m_NeedsRecreate = false;
	}

	void VulkanSwapchain::destroy()
	{
		m_Textures.clear();
		m_Images.clear();
		if (m_Swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_Context.GetDevice(), m_Swapchain, nullptr);
			m_Swapchain = VK_NULL_HANDLE;
		}
	}

	bool VulkanSwapchain::Recreate(u32 width, u32 height, bool vsync)
	{
		if (width == 0 || height == 0)
		{
			return false;
		}
		if (!m_NeedsRecreate && m_Swapchain != VK_NULL_HANDLE && width == m_RequestedWidth &&
			height == m_RequestedHeight && vsync == m_VSync)
		{
			return false;
		}

		const VkDevice device = m_Context.GetDevice();
		vkDeviceWaitIdle(device);

		m_RequestedWidth = width;
		m_RequestedHeight = height;
		m_VSync = vsync;

		// Present fences/semaphores survive a resize because the device is idle.
		create();
		return IsValid();
	}

	u32 VulkanSwapchain::AcquireImage()
	{
		if (!IsValid())
		{
			return c_InvalidImage;
		}

		const VkDevice device = m_Context.GetDevice();

		// The previous frame's present must be observed before the binary semaphore
		// is reused.
		if (vkWaitForFences(device, 1, &m_PresentFence, VK_TRUE, 1000000000ull) != VK_SUCCESS)
		{
			AE_CORE_ERROR("Timed out waiting for the previous present to complete");
			m_NeedsRecreate = true;
			return c_InvalidImage;
		}
		vkResetFences(device, 1, &m_PresentFence);

		vkResetFences(device, 1, &m_AcquireFence);
		u32 imageIndex = 0;
		const VkResult result = vkAcquireNextImageKHR(device, m_Swapchain, 1000000000ull, VK_NULL_HANDLE,
			m_AcquireFence, &imageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR)
		{
			m_NeedsRecreate = true;
			return c_InvalidImage;
		}
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			AE_CORE_ERROR("vkAcquireNextImageKHR failed: {}", static_cast<int>(result));
			m_NeedsRecreate = true;
			return c_InvalidImage;
		}
		if (result == VK_SUBOPTIMAL_KHR)
		{
			m_NeedsRecreate = true;
		}

		if (vkWaitForFences(device, 1, &m_AcquireFence, VK_TRUE, 1000000000ull) != VK_SUCCESS)
		{
			AE_CORE_ERROR("Timed out waiting for the swapchain image to become available");
			m_NeedsRecreate = true;
			return c_InvalidImage;
		}
		vkResetFences(device, 1, &m_AcquireFence);

		return imageIndex;
	}

	void VulkanSwapchain::Present(u32 imageIndex, u64 submissionId)
	{
		if (!IsValid() || imageIndex >= m_Textures.size())
		{
			return;
		}

		auto* vulkanDevice = static_cast<nvrhi::vulkan::IDevice*>(m_Context.GetNvrhiDevice());
		const VkSemaphore timelineSemaphore = vulkanDevice->getQueueSemaphore(nvrhi::CommandQueue::Graphics);
		AE_ASSERT(timelineSemaphore != VK_NULL_HANDLE, "NVRHI did not expose its queue timeline semaphore");

		const VkDevice device = m_Context.GetDevice();

		u64 waitValue = submissionId;
		u64 signalValue = 0;
		VkTimelineSemaphoreSubmitInfo timelineInfo{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
		timelineInfo.waitSemaphoreValueCount = 1;
		timelineInfo.pWaitSemaphoreValues = &waitValue;
		timelineInfo.signalSemaphoreValueCount = 1;
		timelineInfo.pSignalSemaphoreValues = &signalValue;

		VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.pNext = &timelineInfo;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &timelineSemaphore;
		constexpr VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		submitInfo.pWaitDstStageMask = &waitStage;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &m_PresentSemaphore;
		submitInfo.commandBufferCount = 0;

		VkResult result = vkQueueSubmit(m_Context.GetPresentQueue(), 1, &submitInfo, m_PresentFence);
		if (result != VK_SUCCESS)
		{
			AE_CORE_ERROR("Could not submit the present synchronisation: {}", static_cast<int>(result));
			return;
		}

		VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &m_PresentSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &m_Swapchain;
		presentInfo.pImageIndices = &imageIndex;

		result = vkQueuePresentKHR(m_Context.GetPresentQueue(), &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			m_NeedsRecreate = true;
		}
		else if (result != VK_SUCCESS)
		{
			AE_CORE_ERROR("vkQueuePresentKHR failed: {}", static_cast<int>(result));
		}
	}

	nvrhi::ITexture* VulkanSwapchain::GetImage(u32 index) const
	{
		return index < m_Textures.size() ? m_Textures[index].Get() : nullptr;
	}
}
