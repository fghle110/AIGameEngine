#pragma once

#include "Aegis/Core/Base.h"

#include <nvrhi/nvrhi.h>
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace aegis
{
	class Window;

	struct VulkanContextSpecification
	{
		Window* Window = nullptr;
		bool EnableValidation = true;
		std::string ApplicationName = "Aegis";
	};

	// Owns the Vulkan instance, surface, physical device, logical device and the
	// NVRHI device created on top of them. The swapchain is owned by the Renderer.
	class VulkanRenderContext
	{
	public:
		explicit VulkanRenderContext(const VulkanContextSpecification& specification);
		~VulkanRenderContext();

		VulkanRenderContext(const VulkanRenderContext&) = delete;
		VulkanRenderContext& operator=(const VulkanRenderContext&) = delete;

		bool IsValid() const { return m_NvrhiDevice != nullptr; }

		VkInstance GetInstance() const { return m_Instance; }
		VkSurfaceKHR GetSurface() const { return m_Surface; }
		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
		VkDevice GetDevice() const { return m_Device; }
		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		VkQueue GetPresentQueue() const { return m_PresentQueue; }
		u32 GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
		u32 GetPresentQueueFamily() const { return m_PresentQueueFamily; }

		nvrhi::IDevice* GetNvrhiDevice() const { return m_NvrhiDevice.Get(); }
		const VkPhysicalDeviceProperties& GetPhysicalDeviceProperties() const { return m_Properties; }
		const std::string& GetGpuName() const { return m_GpuName; }
		bool IsValidationEnabled() const { return m_ValidationEnabled; }

		const std::vector<const char*>& GetInstanceExtensions() const { return m_InstanceExtensions; }
		const std::vector<const char*>& GetDeviceExtensions() const { return m_DeviceExtensions; }

	private:
		void createInstance();
		void createDebugMessenger();
		void createSurface();
		void pickPhysicalDevice();
		void createDevice();
		void createNvrhiDevice();
		void shutdown();

		VulkanContextSpecification m_Specification;
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue m_PresentQueue = VK_NULL_HANDLE;
		u32 m_GraphicsQueueFamily = 0;
		u32 m_PresentQueueFamily = 0;
		VkPhysicalDeviceProperties m_Properties{};
		std::string m_GpuName;

		std::vector<const char*> m_InstanceExtensions;
		std::vector<const char*> m_DeviceExtensions;

		bool m_ValidationEnabled = false;
		bool m_BufferDeviceAddressEnabled = false;

		nvrhi::DeviceHandle m_NvrhiDevice;

		struct DeviceMessageCallback;
		Scope<DeviceMessageCallback> m_MessageCallback;
	};
}
