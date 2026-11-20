// The only translation unit that includes Vulkan-Hpp. It provides the dynamic
// dispatcher storage NVRHI expects and hands it the instance/device created by
// the engine through volk.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "Aegis/Renderer/Vulkan/VulkanDispatcher.h"

namespace aegis
{
	void InitializeNvrhiVulkanDispatcher(VkInstance instance, PFN_vkGetInstanceProcAddr getInstanceProcAddr,
		VkDevice device)
	{
		VULKAN_HPP_DEFAULT_DISPATCHER.init(instance, getInstanceProcAddr, device);
	}
}
