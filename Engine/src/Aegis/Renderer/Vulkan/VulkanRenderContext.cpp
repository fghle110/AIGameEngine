#include "Aegis/Renderer/Vulkan/VulkanRenderContext.h"

#include "Aegis/Core/Log.h"
#include "Aegis/Core/Window.h"
#include "Aegis/Renderer/Vulkan/VulkanDispatcher.h"

#include <volk.h>

#include <GLFW/glfw3.h>

#include <nvrhi/vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace aegis
{
	namespace
	{
		constexpr std::array<const char*, 1> c_ValidationLayers = { "VK_LAYER_KHRONOS_validation" };

		bool IsInstanceLayerAvailable(const char* layerName)
		{
			u32 layerCount = 0;
			if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) != VK_SUCCESS)
			{
				return false;
			}
			std::vector<VkLayerProperties> layers(layerCount);
			if (vkEnumerateInstanceLayerProperties(&layerCount, layers.data()) != VK_SUCCESS)
			{
				return false;
			}
			return std::any_of(layers.begin(), layers.end(), [layerName](const VkLayerProperties& layer)
			{
				return std::strcmp(layer.layerName, layerName) == 0;
			});
		}

		bool IsInstanceExtensionAvailable(const char* extensionName)
		{
			u32 extensionCount = 0;
			if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr) != VK_SUCCESS)
			{
				return false;
			}
			std::vector<VkExtensionProperties> extensions(extensionCount);
			if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()) != VK_SUCCESS)
			{
				return false;
			}
			return std::any_of(extensions.begin(), extensions.end(), [extensionName](const VkExtensionProperties& ext)
			{
				return std::strcmp(ext.extensionName, extensionName) == 0;
			});
		}

		bool IsDeviceExtensionAvailable(VkPhysicalDevice device, const char* extensionName)
		{
			u32 extensionCount = 0;
			if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr) != VK_SUCCESS)
			{
				return false;
			}
			std::vector<VkExtensionProperties> extensions(extensionCount);
			if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()) != VK_SUCCESS)
			{
				return false;
			}
			return std::any_of(extensions.begin(), extensions.end(), [extensionName](const VkExtensionProperties& ext)
			{
				return std::strcmp(ext.extensionName, extensionName) == 0;
			});
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT /*types*/, const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
			void* /*userData*/)
		{
			const char* message = callbackData != nullptr && callbackData->pMessage != nullptr ? callbackData->pMessage
																								: "(no message)";
			if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
			{
				AE_CORE_ERROR("[Vulkan] {}", message);
			}
			else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
			{
				AE_CORE_WARN("[Vulkan] {}", message);
			}
			else
			{
				AE_CORE_TRACE("[Vulkan] {}", message);
			}
			return VK_FALSE;
		}

		struct QueueFamilySelection
		{
			u32 GraphicsFamily = 0xFFFFFFFFu;
			u32 PresentFamily = 0xFFFFFFFFu;
			bool IsComplete() const { return GraphicsFamily != 0xFFFFFFFFu && PresentFamily != 0xFFFFFFFFu; }
		};

		QueueFamilySelection FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
		{
			QueueFamilySelection selection;

			u32 familyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
			std::vector<VkQueueFamilyProperties> families(familyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

			for (u32 i = 0; i < familyCount; ++i)
			{
				if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
				{
					selection.GraphicsFamily = i;
				}

				VkBool32 presentSupported = VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupported);
				if (presentSupported == VK_TRUE)
				{
					selection.PresentFamily = i;
				}

				if (selection.IsComplete())
				{
					break;
				}
			}

			// Prefer a single family that can do both, which avoids cross-queue sync.
			if (selection.GraphicsFamily == selection.PresentFamily)
			{
				return selection;
			}
			for (u32 i = 0; i < familyCount; ++i)
			{
				const bool graphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
				VkBool32 presentSupported = VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupported);
				if (graphics && presentSupported == VK_TRUE)
				{
					selection.GraphicsFamily = i;
					selection.PresentFamily = i;
					break;
				}
			}
			return selection;
		}

		bool SupportsDeviceExtension(VkPhysicalDevice device, const char* name)
		{
			return IsDeviceExtensionAvailable(device, name);
		}
	}

	struct VulkanRenderContext::DeviceMessageCallback final : public nvrhi::IMessageCallback
	{
		void message(nvrhi::MessageSeverity severity, const char* messageText) override
		{
			switch (severity)
			{
				case nvrhi::MessageSeverity::Info: AE_CORE_TRACE("[NVRHI] {}", messageText); break;
				case nvrhi::MessageSeverity::Warning: AE_CORE_WARN("[NVRHI] {}", messageText); break;
				case nvrhi::MessageSeverity::Error: AE_CORE_ERROR("[NVRHI] {}", messageText); break;
				case nvrhi::MessageSeverity::Fatal: AE_CORE_CRITICAL("[NVRHI] {}", messageText); break;
			}
		}
	};

	VulkanRenderContext::VulkanRenderContext(const VulkanContextSpecification& specification)
		: m_Specification(specification)
	{
		AE_ASSERT(m_Specification.Window != nullptr, "A window is required to create the Vulkan context");

		if (volkInitialize() != VK_SUCCESS)
		{
			AE_CORE_CRITICAL("Could not load the Vulkan loader (vulkan-1.dll / libvulkan.so)");
			return;
		}

		AE_CORE_INFO("[vulkan] creating instance");
		createInstance();
		if (m_Instance == VK_NULL_HANDLE)
		{
			return;
		}
		AE_CORE_INFO("[vulkan] loading instance functions");
		volkLoadInstance(m_Instance);
		createDebugMessenger();
		AE_CORE_INFO("[vulkan] creating surface");
		createSurface();
		AE_CORE_INFO("[vulkan] picking physical device");
		pickPhysicalDevice();
		if (m_PhysicalDevice == VK_NULL_HANDLE)
		{
			return;
		}
		AE_CORE_INFO("[vulkan] creating device");
		createDevice();
		if (m_Device == VK_NULL_HANDLE)
		{
			return;
		}
		AE_CORE_INFO("[vulkan] loading device functions");
		volkLoadDevice(m_Device);
		AE_CORE_INFO("[vulkan] creating nvrhi device");
		createNvrhiDevice();

		if (m_NvrhiDevice != nullptr)
		{
			AE_CORE_INFO("Renderer: Vulkan | GPU: {} | API {}.{}.{} | validation {}", m_GpuName,
				VK_API_VERSION_MAJOR(m_Properties.apiVersion), VK_API_VERSION_MINOR(m_Properties.apiVersion),
				VK_API_VERSION_PATCH(m_Properties.apiVersion), m_ValidationEnabled ? "on" : "off");
		}
	}

	VulkanRenderContext::~VulkanRenderContext()
	{
		shutdown();
	}

	void VulkanRenderContext::createInstance()
	{
		m_ValidationEnabled = m_Specification.EnableValidation && IsInstanceLayerAvailable(c_ValidationLayers[0]);
		if (m_Specification.EnableValidation && !m_ValidationEnabled)
		{
			AE_CORE_WARN("Vulkan validation layer is not installed; continuing without validation");
		}

		u32 glfwExtensionCount = 0;
		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
		if (glfwExtensions == nullptr)
		{
			AE_CORE_CRITICAL("GLFW did not provide the required Vulkan instance extensions");
			return;
		}

		m_InstanceExtensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (IsInstanceExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		{
			m_InstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

#if defined(__APPLE__)
		m_InstanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		m_InstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

		// NVRHI's Vulkan backend uses synchronisation2, which is core in Vulkan 1.3.
		u32 loaderVersion = VK_API_VERSION_1_3;
		AE_CORE_INFO("[vulkan] querying loader version");
		if (vkEnumerateInstanceVersion != nullptr)
		{
			AE_CORE_INFO("[vulkan] loader version function present");
			const VkResult versionResult = vkEnumerateInstanceVersion(&loaderVersion);
			AE_CORE_INFO("[vulkan] loader version {}.{}.{} (result {})", VK_API_VERSION_MAJOR(loaderVersion),
				VK_API_VERSION_MINOR(loaderVersion), VK_API_VERSION_PATCH(loaderVersion),
				static_cast<int>(versionResult));
		}
		else
		{
			AE_CORE_INFO("[vulkan] loader has no vkEnumerateInstanceVersion; assuming 1.0");
			loaderVersion = VK_API_VERSION_1_0;
		}
		const u32 requestedVersion =
			loaderVersion >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;

		VkApplicationInfo applicationInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		applicationInfo.pApplicationName = m_Specification.ApplicationName.c_str();
		applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
		applicationInfo.pEngineName = "Aegis";
		applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
		applicationInfo.apiVersion = requestedVersion;

		VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		createInfo.pApplicationInfo = &applicationInfo;
		createInfo.enabledExtensionCount = static_cast<u32>(m_InstanceExtensions.size());
		createInfo.ppEnabledExtensionNames = m_InstanceExtensions.data();
		createInfo.enabledLayerCount = m_ValidationEnabled ? static_cast<u32>(c_ValidationLayers.size()) : 0u;
		createInfo.ppEnabledLayerNames = m_ValidationEnabled ? c_ValidationLayers.data() : nullptr;
#if defined(__APPLE__)
		createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

		const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
		if (result != VK_SUCCESS)
		{
			AE_CORE_CRITICAL("vkCreateInstance failed: {}", static_cast<int>(result));
			m_Instance = VK_NULL_HANDLE;
		}
	}

	void VulkanRenderContext::createDebugMessenger()
	{
		if (!m_ValidationEnabled || !IsInstanceExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		{
			return;
		}

		VkDebugUtilsMessengerCreateInfoEXT createInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
									 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
								 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
								 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = VulkanDebugCallback;

		if (vkCreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS)
		{
			AE_CORE_WARN("Could not create the Vulkan debug messenger");
			m_DebugMessenger = VK_NULL_HANDLE;
		}
	}

	void VulkanRenderContext::createSurface()
	{
		GLFWwindow* window = static_cast<GLFWwindow*>(m_Specification.Window->GetNativeWindow());
		if (window == nullptr)
		{
			AE_CORE_CRITICAL("The window has no native handle; the Vulkan surface cannot be created");
			return;
		}
		const VkResult result = glfwCreateWindowSurface(m_Instance, window, nullptr, &m_Surface);
		if (result != VK_SUCCESS)
		{
			AE_CORE_CRITICAL("glfwCreateWindowSurface failed: {}", static_cast<int>(result));
			m_Surface = VK_NULL_HANDLE;
		}
	}

	void VulkanRenderContext::pickPhysicalDevice()
	{
		u32 deviceCount = 0;
		vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
		if (deviceCount == 0)
		{
			AE_CORE_CRITICAL("No Vulkan-capable GPU was found");
			return;
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

		i32 bestScore = -1;
		for (const VkPhysicalDevice device : devices)
		{
			const QueueFamilySelection families = FindQueueFamilies(device, m_Surface);
			if (!families.IsComplete())
			{
				continue;
			}
			if (!SupportsDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			{
				continue;
			}

			VkPhysicalDeviceProperties properties{};
			vkGetPhysicalDeviceProperties(device, &properties);

			VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features2.pNext = &features12;
			vkGetPhysicalDeviceFeatures2(device, &features2);
			if (features12.timelineSemaphore != VK_TRUE)
			{
				AE_CORE_WARN("Skipping GPU '{}': timeline semaphores are not supported", properties.deviceName);
				continue;
			}

			i32 score = 1;
			switch (properties.deviceType)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 1000; break;
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 500; break;
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 300; break;
				case VK_PHYSICAL_DEVICE_TYPE_CPU: score = 100; break;
				default: score = 10; break;
			}

			if (score > bestScore)
			{
				bestScore = score;
				m_PhysicalDevice = device;
				m_GraphicsQueueFamily = families.GraphicsFamily;
				m_PresentQueueFamily = families.PresentFamily;
				m_Properties = properties;
				m_GpuName = properties.deviceName;
			}
		}

		if (m_PhysicalDevice == VK_NULL_HANDLE)
		{
			AE_CORE_CRITICAL("No Vulkan GPU with a present-capable queue and swapchain support was found");
		}
	}

	void VulkanRenderContext::createDevice()
	{
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);

		VkPhysicalDeviceVulkan13Features supported13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceVulkan12Features supported12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		supported12.pNext = &supported13;
		VkPhysicalDeviceFeatures2 supported2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		supported2.pNext = &supported12;
		vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &supported2);

		VkPhysicalDeviceVulkan13Features enabled13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		enabled13.synchronization2 = VK_TRUE;
		enabled13.dynamicRendering = supported13.dynamicRendering;
		enabled13.maintenance4 = supported13.maintenance4;

		VkPhysicalDeviceVulkan12Features enabled12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		enabled12.pNext = &enabled13;
		enabled12.timelineSemaphore = VK_TRUE;
		enabled12.drawIndirectCount = supported12.drawIndirectCount;
		enabled12.descriptorIndexing = supported12.descriptorIndexing;
		enabled12.descriptorBindingPartiallyBound = supported12.descriptorBindingPartiallyBound;
		enabled12.descriptorBindingSampledImageUpdateAfterBind = supported12.descriptorBindingSampledImageUpdateAfterBind;
		enabled12.descriptorBindingStorageBufferUpdateAfterBind =
			supported12.descriptorBindingStorageBufferUpdateAfterBind;
		enabled12.descriptorBindingStorageImageUpdateAfterBind = supported12.descriptorBindingStorageImageUpdateAfterBind;
		enabled12.descriptorBindingUniformBufferUpdateAfterBind =
			supported12.descriptorBindingUniformBufferUpdateAfterBind;
		enabled12.bufferDeviceAddress = supported12.bufferDeviceAddress;
		enabled12.shaderSampledImageArrayNonUniformIndexing = supported12.shaderSampledImageArrayNonUniformIndexing;
		enabled12.shaderStorageBufferArrayNonUniformIndexing = supported12.shaderStorageBufferArrayNonUniformIndexing;
		enabled12.shaderStorageImageArrayNonUniformIndexing = supported12.shaderStorageImageArrayNonUniformIndexing;
		m_BufferDeviceAddressEnabled = supported12.bufferDeviceAddress == VK_TRUE;

		VkPhysicalDeviceFeatures enabledFeatures{};
		enabledFeatures.samplerAnisotropy = supported2.features.samplerAnisotropy;
		enabledFeatures.fillModeNonSolid = supported2.features.fillModeNonSolid;
		enabledFeatures.independentBlend = supported2.features.independentBlend;
		enabledFeatures.depthClamp = supported2.features.depthClamp;

		std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
#if defined(__APPLE__)
		if (IsDeviceExtensionAvailable(m_PhysicalDevice, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
		{
			extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
		}
		if (IsDeviceExtensionAvailable(m_PhysicalDevice, VK_KHR_MAINTENANCE_3_EXTENSION_NAME))
		{
			extensions.push_back(VK_KHR_MAINTENANCE_3_EXTENSION_NAME);
		}
#endif
		m_DeviceExtensions = extensions;

		const float queuePriority = 1.0f;
		std::vector<u32> uniqueFamilies{ m_GraphicsQueueFamily };
		if (m_PresentQueueFamily != m_GraphicsQueueFamily)
		{
			uniqueFamilies.push_back(m_PresentQueueFamily);
		}

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		queueCreateInfos.reserve(uniqueFamilies.size());
		for (const u32 family : uniqueFamilies)
		{
			VkDeviceQueueCreateInfo queueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
			queueCreateInfo.queueFamilyIndex = family;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}

		VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		createInfo.pNext = &enabled12;
		createInfo.queueCreateInfoCount = static_cast<u32>(queueCreateInfos.size());
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();
		createInfo.pEnabledFeatures = &enabledFeatures;
		createInfo.enabledLayerCount = 0;
		createInfo.ppEnabledLayerNames = nullptr;

		const VkResult result = vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
		if (result != VK_SUCCESS)
		{
			AE_CORE_CRITICAL("vkCreateDevice failed: {}", static_cast<int>(result));
			m_Device = VK_NULL_HANDLE;
			return;
		}

		vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
		if (m_PresentQueueFamily == m_GraphicsQueueFamily)
		{
			m_PresentQueue = m_GraphicsQueue;
		}
		else
		{
			vkGetDeviceQueue(m_Device, m_PresentQueueFamily, 0, &m_PresentQueue);
		}
	}

	void VulkanRenderContext::createNvrhiDevice()
	{
		InitializeNvrhiVulkanDispatcher(m_Instance, vkGetInstanceProcAddr, m_Device);

		m_MessageCallback = CreateScope<DeviceMessageCallback>();

		nvrhi::vulkan::DeviceDesc deviceDesc;
		deviceDesc.errorCB = m_MessageCallback.get();
		deviceDesc.instance = m_Instance;
		deviceDesc.physicalDevice = m_PhysicalDevice;
		deviceDesc.device = m_Device;
		deviceDesc.graphicsQueue = m_GraphicsQueue;
		deviceDesc.graphicsQueueIndex = static_cast<int>(m_GraphicsQueueFamily);
		deviceDesc.transferQueue = nullptr;
		deviceDesc.transferQueueIndex = -1;
		deviceDesc.computeQueue = nullptr;
		deviceDesc.computeQueueIndex = -1;
		deviceDesc.allocationCallbacks = nullptr;
		deviceDesc.instanceExtensions = m_InstanceExtensions.data();
		deviceDesc.numInstanceExtensions = m_InstanceExtensions.size();
		deviceDesc.deviceExtensions = m_DeviceExtensions.data();
		deviceDesc.numDeviceExtensions = m_DeviceExtensions.size();
		deviceDesc.bufferDeviceAddressSupported = m_BufferDeviceAddressEnabled;
		deviceDesc.descriptorBindingUniformBufferUpdateAfterBind = true;

		m_NvrhiDevice = nvrhi::vulkan::createDevice(deviceDesc);
		if (m_NvrhiDevice == nullptr)
		{
			AE_CORE_CRITICAL("NVRHI failed to create the Vulkan device");
		}
	}

	void VulkanRenderContext::shutdown()
	{
		m_NvrhiDevice = nullptr;
		m_MessageCallback = nullptr;

		if (m_Device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_Device);
			vkDestroyDevice(m_Device, nullptr);
			m_Device = VK_NULL_HANDLE;
		}
		if (m_Surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
			m_Surface = VK_NULL_HANDLE;
		}
		if (m_DebugMessenger != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
			m_DebugMessenger = VK_NULL_HANDLE;
		}
		if (m_Instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(m_Instance, nullptr);
			m_Instance = VK_NULL_HANDLE;
		}
		volkFinalize();
	}
}
