#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Renderer/Vulkan/VulkanRenderContext.h"

#include <nvrhi/nvrhi.h>
#include <vulkan/vulkan.h>

#include <vector>

namespace aegis
{
	// Vulkan swapchain with NVRHI texture wrappers for its images. Frame timing is
	// explicit: the image is acquired on the CPU, the render command list is
	// submitted through NVRHI, and presentation waits on NVRHI's timeline
	// semaphore so the GPU never presents an unfinished frame.
	class VulkanSwapchain
	{
	public:
		static constexpr u32 c_InvalidImage = 0xFFFFFFFFu;

		VulkanSwapchain(VulkanRenderContext& context, u32 width, u32 height, bool vsync);
		~VulkanSwapchain();

		VulkanSwapchain(const VulkanSwapchain&) = delete;
		VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

		bool IsValid() const { return m_Swapchain != VK_NULL_HANDLE; }

		// Recreates the swapchain for a new size / vsync mode. Safe to call every
		// frame: it returns false when nothing changed.
		bool Recreate(u32 width, u32 height, bool vsync);

		u32 AcquireImage();
		void Present(u32 imageIndex, u64 submissionId);

		nvrhi::ITexture* GetImage(u32 index) const;
		u32 GetImageCount() const { return static_cast<u32>(m_Textures.size()); }
		u32 GetWidth() const { return m_Extent.width; }
		u32 GetHeight() const { return m_Extent.height; }
		nvrhi::Format GetFormat() const { return m_NvrhiFormat; }
		VkFormat GetVulkanFormat() const { return m_Format; }
		bool IsVSync() const { return m_VSync; }
		bool NeedsRecreate() const { return m_NeedsRecreate; }

	private:
		void create();
		void destroy();
		void createSynchronisation();
		void destroySynchronisation();

		VulkanRenderContext& m_Context;

		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		VkFormat m_Format = VK_FORMAT_UNDEFINED;
		nvrhi::Format m_NvrhiFormat = nvrhi::Format::UNKNOWN;
		VkExtent2D m_Extent{ 0, 0 };
		bool m_VSync = true;
		bool m_NeedsRecreate = false;

		u32 m_RequestedWidth = 0;
		u32 m_RequestedHeight = 0;

		std::vector<VkImage> m_Images;
		std::vector<nvrhi::TextureHandle> m_Textures;

		VkFence m_AcquireFence = VK_NULL_HANDLE;
		VkSemaphore m_PresentSemaphore = VK_NULL_HANDLE;
		VkFence m_PresentFence = VK_NULL_HANDLE;
	};
}
