#pragma once

#include <vulkan/vulkan.h>

namespace aegis
{
	// NVRHI's Vulkan backend uses Vulkan-Hpp's dynamic dispatcher. The storage
	// lives in VulkanDispatcher.cpp while the engine itself talks to Vulkan
	// through volk, so the two worlds never share function symbols.
	void InitializeNvrhiVulkanDispatcher(VkInstance instance, PFN_vkGetInstanceProcAddr getInstanceProcAddr,
		VkDevice device);
}
