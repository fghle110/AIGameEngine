#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Commands/CommandBus.h"
#include "Aegis/Core/CrashHandler.h"
#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Log.h"
#include "Editor/EditorCommands.h"
#include "Aegis/Renderer/SceneRenderer.h"
#include "Aegis/Renderer/Vulkan/VulkanDispatcher.h"
#include "Aegis/Scene/Components.h"
#include "Aegis/Scene/Entity.h"
#include "Aegis/Scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nvrhi/vulkan.h>
#include <volk.h>

#include <cstring>
#include <format>
#include <vector>

using namespace aegis;

namespace
{
	struct DeviceMessageCallback final : nvrhi::IMessageCallback
	{
		void message(nvrhi::MessageSeverity severity, const char* messageText) override
		{
			if (severity == nvrhi::MessageSeverity::Error || severity == nvrhi::MessageSeverity::Fatal)
			{
				AE_CORE_ERROR("[NVRHI test] {}", messageText);
			}
		}
	};

	// A window-less Vulkan + NVRHI device. The renderer only needs a device and a
	// command list, so the GPU test does not have to open a window.
	struct HeadlessDevice
	{
		VkInstance Instance = VK_NULL_HANDLE;
		VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;
		VkQueue GraphicsQueue = VK_NULL_HANDLE;
		u32 GraphicsQueueFamily = 0;
		std::vector<const char*> DeviceExtensions;
		Scope<DeviceMessageCallback> MessageCallback;
		nvrhi::DeviceHandle Nvrhi;

		~HeadlessDevice()
		{
			Nvrhi = nullptr;
			MessageCallback = nullptr;
			if (Device != VK_NULL_HANDLE)
			{
				vkDeviceWaitIdle(Device);
				vkDestroyDevice(Device, nullptr);
			}
			if (Instance != VK_NULL_HANDLE)
			{
				vkDestroyInstance(Instance, nullptr);
			}
		}
	};

	bool HasDeviceExtension(VkPhysicalDevice device, const char* name)
	{
		u32 count = 0;
		if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
		{
			return false;
		}
		std::vector<VkExtensionProperties> extensions(count);
		if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS)
		{
			return false;
		}
		for (const VkExtensionProperties& extension : extensions)
		{
			if (std::strcmp(extension.extensionName, name) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool CreateHeadlessDevice(HeadlessDevice& out)
	{
		if (volkInitialize() != VK_SUCCESS)
		{
			return false;
		}

		u32 loaderVersion = VK_API_VERSION_1_0;
		if (vkEnumerateInstanceVersion == nullptr || vkEnumerateInstanceVersion(&loaderVersion) != VK_SUCCESS)
		{
			return false;
		}
		if (loaderVersion < VK_API_VERSION_1_3)
		{
			return false;
		}

		VkApplicationInfo applicationInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		applicationInfo.pApplicationName = "Aegis tests";
		applicationInfo.pEngineName = "Aegis";
		applicationInfo.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceInfo.pApplicationInfo = &applicationInfo;
		if (vkCreateInstance(&instanceInfo, nullptr, &out.Instance) != VK_SUCCESS)
		{
			return false;
		}
		volkLoadInstance(out.Instance);

		u32 deviceCount = 0;
		if (vkEnumeratePhysicalDevices(out.Instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0)
		{
			return false;
		}
		std::vector<VkPhysicalDevice> devices(deviceCount);
		if (vkEnumeratePhysicalDevices(out.Instance, &deviceCount, devices.data()) != VK_SUCCESS)
		{
			return false;
		}

		for (const VkPhysicalDevice candidate : devices)
		{
			u32 familyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
			std::vector<VkQueueFamilyProperties> families(familyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

			u32 graphicsFamily = 0xFFFFFFFFu;
			for (u32 i = 0; i < familyCount; ++i)
			{
				if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
				{
					graphicsFamily = i;
					break;
				}
			}
			if (graphicsFamily == 0xFFFFFFFFu)
			{
				continue;
			}

			VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			features12.pNext = &features13;
			VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features2.pNext = &features12;
			vkGetPhysicalDeviceFeatures2(candidate, &features2);

			// The renderer needs timeline semaphores, synchronisation2 and dynamic
			// rendering, exactly like the windowed context in VulkanRenderContext.
			if (features12.timelineSemaphore != VK_TRUE || features13.synchronization2 != VK_TRUE ||
				features13.dynamicRendering != VK_TRUE)
			{
				continue;
			}
			if (!HasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			{
				continue;
			}

			out.PhysicalDevice = candidate;
			out.GraphicsQueueFamily = graphicsFamily;
			break;
		}

		if (out.PhysicalDevice == VK_NULL_HANDLE)
		{
			return false;
		}

		VkPhysicalDeviceVulkan13Features supported13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceVulkan12Features supported12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		supported12.pNext = &supported13;
		VkPhysicalDeviceFeatures2 supported2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		supported2.pNext = &supported12;
		vkGetPhysicalDeviceFeatures2(out.PhysicalDevice, &supported2);

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
		enabled12.descriptorBindingStorageBufferUpdateAfterBind = supported12.descriptorBindingStorageBufferUpdateAfterBind;
		enabled12.descriptorBindingStorageImageUpdateAfterBind = supported12.descriptorBindingStorageImageUpdateAfterBind;
		enabled12.descriptorBindingUniformBufferUpdateAfterBind = supported12.descriptorBindingUniformBufferUpdateAfterBind;
		enabled12.bufferDeviceAddress = supported12.bufferDeviceAddress;
		enabled12.shaderSampledImageArrayNonUniformIndexing = supported12.shaderSampledImageArrayNonUniformIndexing;
		enabled12.shaderStorageBufferArrayNonUniformIndexing = supported12.shaderStorageBufferArrayNonUniformIndexing;
		enabled12.shaderStorageImageArrayNonUniformIndexing = supported12.shaderStorageImageArrayNonUniformIndexing;

		VkPhysicalDeviceFeatures enabledFeatures{};
		// The material samplers use anisotropic filtering.
		enabledFeatures.samplerAnisotropy = supported2.features.samplerAnisotropy;
		enabledFeatures.fillModeNonSolid = supported2.features.fillModeNonSolid;
		enabledFeatures.independentBlend = supported2.features.independentBlend;
		enabledFeatures.depthClamp = supported2.features.depthClamp;

		const float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueInfo.queueFamilyIndex = out.GraphicsQueueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &queuePriority;

		out.DeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		deviceInfo.pNext = &enabled12;
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = static_cast<u32>(out.DeviceExtensions.size());
		deviceInfo.ppEnabledExtensionNames = out.DeviceExtensions.data();
		deviceInfo.pEnabledFeatures = &enabledFeatures;
		if (vkCreateDevice(out.PhysicalDevice, &deviceInfo, nullptr, &out.Device) != VK_SUCCESS)
		{
			return false;
		}

		volkLoadDevice(out.Device);
		vkGetDeviceQueue(out.Device, out.GraphicsQueueFamily, 0, &out.GraphicsQueue);

		InitializeNvrhiVulkanDispatcher(out.Instance, vkGetInstanceProcAddr, out.Device);

		out.MessageCallback = CreateScope<DeviceMessageCallback>();
		nvrhi::vulkan::DeviceDesc deviceDesc;
		deviceDesc.errorCB = out.MessageCallback.get();
		deviceDesc.instance = out.Instance;
		deviceDesc.physicalDevice = out.PhysicalDevice;
		deviceDesc.device = out.Device;
		deviceDesc.graphicsQueue = out.GraphicsQueue;
		deviceDesc.graphicsQueueIndex = static_cast<int>(out.GraphicsQueueFamily);
		deviceDesc.transferQueue = nullptr;
		deviceDesc.transferQueueIndex = -1;
		deviceDesc.computeQueue = nullptr;
		deviceDesc.computeQueueIndex = -1;
		deviceDesc.deviceExtensions = out.DeviceExtensions.data();
		deviceDesc.numDeviceExtensions = out.DeviceExtensions.size();
		deviceDesc.bufferDeviceAddressSupported = enabled12.bufferDeviceAddress == VK_TRUE;
		deviceDesc.descriptorBindingUniformBufferUpdateAfterBind = true;

		out.Nvrhi = nvrhi::vulkan::createDevice(deviceDesc);
		return out.Nvrhi != nullptr;
	}

	std::vector<u8> ReadBackTexture(nvrhi::IDevice* device, nvrhi::ITexture* texture, u32& outWidth, u32& outHeight)
	{
		const nvrhi::TextureDesc desc = texture->getDesc();
		outWidth = desc.width;
		outHeight = desc.height;

		nvrhi::StagingTextureHandle staging = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
		if (staging == nullptr)
		{
			return {};
		}

		nvrhi::CommandListHandle commandList = device->createCommandList();
		commandList->open();
		commandList->copyTexture(staging, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());
		commandList->close();
		nvrhi::ICommandList* rawList = commandList;
		device->executeCommandLists(&rawList, 1);
		device->waitForIdle();

		usize rowPitch = 0;
		void* mapped = device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
		if (mapped == nullptr)
		{
			return {};
		}

		std::vector<u8> pixels(static_cast<usize>(desc.width) * desc.height * 4, 0);
		for (u32 row = 0; row < desc.height; ++row)
		{
			std::memcpy(pixels.data() + static_cast<usize>(row) * desc.width * 4,
				static_cast<const u8*>(mapped) + row * rowPitch, static_cast<usize>(desc.width) * 4);
		}
		device->unmapStagingTexture(staging);
		return pixels;
	}

	Entity CreateLitCube(Scene& scene, SceneRenderer& renderer)
	{
		Entity cube = scene.CreateEntity("Cube");
		MeshRendererComponent rendererComponent;
		rendererComponent.Mesh = renderer.GetAssetCache().GetBuiltinMesh(PrimitiveType::Cube);
		REQUIRE(rendererComponent.Mesh.IsValid());

		const Uuid materialId = Uuid::Generate();
		MaterialFactors factors;
		factors.BaseColorFactor = glm::vec4(0.85f, 0.18f, 0.12f, 1.0f);
		factors.MetallicFactor = 0.0f;
		factors.RoughnessFactor = 0.4f;
		REQUIRE_FALSE(renderer.GetAssetCache().AddMaterial(materialId, factors, MaterialTextures{}, "test red").IsNull());
		rendererComponent.Materials.push_back(AssetHandle{ materialId, AssetType::Material });
		cube.AddComponent<MeshRendererComponent>(rendererComponent);

		Entity sun = scene.CreateEntity("Sun");
		LightComponent light;
		light.Type = LightType::Directional;
		light.Intensity = 4.0f;
		light.Color = glm::vec3(1.0f, 0.97f, 0.9f);
		sun.AddComponent<LightComponent>(light);
		sun.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-45.0f, 25.0f, 0.0f));

		scene.UpdateTransforms();
		return cube;
	}
}

TEST_CASE("SceneRenderer: a null device is rejected without crashing")
{
	SceneRenderer renderer;
	CHECK_FALSE(renderer.Initialize(nullptr));
	CHECK_FALSE(renderer.IsInitialized());

	// Resizing and rendering before initialization must be harmless.
	renderer.Resize(0, 0);
	CHECK(renderer.GetWidth() == 1);
	CHECK(renderer.GetHeight() == 1);
	CHECK(renderer.GetFinalColor() == nullptr);
	CHECK(renderer.GetStats().DrawCalls == 0);
	renderer.Shutdown();
}

TEST_CASE("SceneRenderer: GPU frame renders a lit cube into the final colour")
{
	HeadlessDevice device;
	if (!CreateHeadlessDevice(device))
	{
		AE_CORE_WARN("Vulkan 1.3 is unavailable on this machine: skipping the SceneRenderer GPU test");
		return;
	}

	constexpr u32 c_Size = 64;
	SceneRenderer renderer;
	REQUIRE(renderer.Initialize(device.Nvrhi.Get()));
	CHECK(renderer.IsInitialized());

	renderer.Resize(c_Size, c_Size);
	REQUIRE(renderer.GetWidth() == c_Size);
	REQUIRE(renderer.GetHeight() == c_Size);
	REQUIRE(renderer.GetFinalColor() != nullptr);
	CHECK(renderer.GetFinalColor()->getDesc().width == c_Size);
	CHECK(renderer.GetFinalColor()->getDesc().height == c_Size);

	Scene scene("GPU test");
	CreateLitCube(scene, renderer);

	const glm::vec3 cameraPosition(0.0f, 1.6f, 4.2f);
	const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projection = glm::perspective(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);
	const glm::mat4 viewProjection = projection * view;
	const glm::mat4 lightViewProjection = SceneRenderer::ComputeDirectionalShadowMatrix(scene, cameraPosition, 20.0f);

	nvrhi::CommandListHandle commandList = device.Nvrhi->createCommandList();
	commandList->open();
	renderer.Render(commandList, scene, viewProjection, cameraPosition, lightViewProjection, true);
	commandList->close();
	nvrhi::ICommandList* rawList = commandList;
	device.Nvrhi->executeCommandLists(&rawList, 1);
	device.Nvrhi->waitForIdle();

	const SceneRendererStats& stats = renderer.GetStats();
	CHECK(stats.VisibleMeshes == 1);
	CHECK(stats.DrawCalls == 1);
	CHECK(stats.ShadowDrawCalls == 1);
	CHECK(stats.Triangles == 12);

	u32 width = 0;
	u32 height = 0;
	const std::vector<u8> pixels = ReadBackTexture(device.Nvrhi.Get(), renderer.GetFinalColor(), width, height);
	REQUIRE(width == c_Size);
	REQUIRE(height == c_Size);
	REQUIRE(pixels.size() == static_cast<usize>(c_Size) * c_Size * 4);

	const auto pixelAt = [&pixels](u32 x, u32 y)
	{
		const usize offset = (static_cast<usize>(y) * c_Size + x) * 4;
		return glm::ivec4(pixels[offset + 0], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]);
	};

	// The cube covers the centre of the frame with a warm red material.
	// The cube is small in frame, so count the pixels that clearly belong to it: a
	// sun-lit red surface instead of the black clear colour.
	u32 litPixels = 0;
	u32 redPixels = 0;
	for (u32 y = 0; y < c_Size; ++y)
	{
		for (u32 x = 0; x < c_Size; ++x)
		{
			const glm::ivec4 pixel = pixelAt(x, y);
			if (pixel.r + pixel.g + pixel.b > 24)
			{
				++litPixels;
			}
			if (pixel.r > 60 && pixel.r > pixel.b + 30)
			{
				++redPixels;
			}
		}
	}
	CHECK(litPixels > c_Size * c_Size / 20);
	CHECK(redPixels > c_Size * c_Size / 50);

	// The corner shows the procedural sky: blue dominant, and darker than the sun-lit
	// geometry in the middle of the frame.
	const glm::ivec4 corner = pixelAt(1, 1);
	CHECK(corner.b >= corner.r);
	CHECK(corner.r + corner.g + corner.b < c_Size * 12);

	renderer.Shutdown();
	CHECK_FALSE(renderer.IsInitialized());
}

TEST_CASE("SceneRenderer: a textured plane shows its albedo map")
{
	HeadlessDevice device;
	if (!CreateHeadlessDevice(device))
	{
		AE_CORE_WARN("Vulkan 1.3 is unavailable on this machine: skipping the textured plane GPU test");
		return;
	}

	constexpr u32 c_Size = 32;
	SceneRenderer renderer;
	REQUIRE(renderer.Initialize(device.Nvrhi.Get()));
	renderer.Resize(c_Size, c_Size);

	AssetCache& cache = renderer.GetAssetCache();
	const Uuid textureId = Uuid::Generate();
	const Image checkerboard = Image::CreateCheckerboard(16, 16, 8, glm::vec3(1.0f), glm::vec3(0.2f));
	REQUIRE_FALSE(cache.AddTexture(textureId, checkerboard, SamplerSettings::Clamped()).IsNull());
	const Ref<Texture> texture = cache.GetTexture(textureId);
	REQUIRE(texture != nullptr);
	CHECK(texture->GetWidth() == 16);
	CHECK(texture->GetMipCount() == 5);

	MaterialTextures textures;
	textures.Albedo = texture;
	const Uuid materialId = Uuid::Generate();
	MaterialFactors factors;
	factors.BaseColorFactor = glm::vec4(1.0f);
	factors.RoughnessFactor = 1.0f;
	factors.MetallicFactor = 0.0f;
	REQUIRE_FALSE(cache.AddMaterial(materialId, factors, textures, "checker").IsNull());

	Scene scene("textured plane");
	Entity floor = scene.CreateEntity("Floor");
	MeshRendererComponent floorRenderer;
	floorRenderer.Mesh = cache.GetBuiltinMesh(PrimitiveType::Plane);
	floorRenderer.CastShadows = false;
	floorRenderer.Materials.push_back(AssetHandle{ materialId, AssetType::Material });
	floor.AddComponent<MeshRendererComponent>(floorRenderer);
	floor.GetComponent<TransformComponent>().LocalScale = glm::vec3(8.0f, 1.0f, 8.0f);

	Entity sun = scene.CreateEntity("Sun");
	LightComponent light;
	light.Type = LightType::Directional;
	light.Intensity = 4.0f;
	sun.AddComponent<LightComponent>(light);
	sun.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-50.0f, 20.0f, 0.0f));
	scene.UpdateTransforms();

	const glm::vec3 cameraPosition(0.0f, 2.0f, 3.0f);
	const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projection = glm::perspective(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);
	const glm::mat4 lightViewProjection = SceneRenderer::ComputeDirectionalShadowMatrix(scene, cameraPosition, 20.0f);

	nvrhi::CommandListHandle commandList = device.Nvrhi->createCommandList();
	commandList->open();
	renderer.Render(commandList, scene, projection * view, cameraPosition, lightViewProjection, true);
	commandList->close();
	nvrhi::ICommandList* rawList = commandList;
	device.Nvrhi->executeCommandLists(&rawList, 1);
	device.Nvrhi->waitForIdle();

	CHECK(renderer.GetStats().DrawCalls == 1);

	u32 width = 0;
	u32 height = 0;
	const std::vector<u8> pixels = ReadBackTexture(device.Nvrhi.Get(), renderer.GetFinalColor(), width, height);
	REQUIRE(pixels.size() == static_cast<usize>(c_Size) * c_Size * 4);

	const usize centre = (static_cast<usize>(c_Size / 2) * c_Size + c_Size / 2) * 4;
	const glm::ivec4 centrePixel(pixels[centre + 0], pixels[centre + 1], pixels[centre + 2], pixels[centre + 3]);
	// The plane faces the sun and samples a white/black checker, so the centre must be lit
	// unless the albedo map or the lighting path is broken.
	CHECK(centrePixel.r > 16);
	CHECK(centrePixel.g > 16);
	CHECK(centrePixel.b > 16);

	renderer.Shutdown();
}

TEST_CASE("SceneRenderer: resizing recreates the render targets")
{
	HeadlessDevice device;
	if (!CreateHeadlessDevice(device))
	{
		AE_CORE_WARN("Vulkan 1.3 is unavailable on this machine: skipping the SceneRenderer resize test");
		return;
	}

	SceneRenderer renderer;
	REQUIRE(renderer.Initialize(device.Nvrhi.Get()));
	renderer.Resize(40, 24);
	CHECK(renderer.GetWidth() == 40);
	CHECK(renderer.GetHeight() == 24);
	REQUIRE(renderer.GetFinalColor() != nullptr);
	CHECK(renderer.GetFinalColor()->getDesc().width == 40);

	renderer.Resize(17, 33);
	CHECK(renderer.GetWidth() == 17);
	CHECK(renderer.GetHeight() == 33);
	REQUIRE(renderer.GetFinalColor() != nullptr);
	CHECK(renderer.GetFinalColor()->getDesc().width == 17);
	CHECK(renderer.GetFinalColor()->getDesc().height == 33);

	// Zero-sized requests are clamped instead of creating invalid resources.
	renderer.Resize(0, 0);
	CHECK(renderer.GetWidth() == 1);
	CHECK(renderer.GetHeight() == 1);

	renderer.Shutdown();
}

TEST_CASE("SceneRenderer: SSAO darkens the frame and can be toggled at runtime")
{
	HeadlessDevice device;
	if (!CreateHeadlessDevice(device))
	{
		AE_CORE_WARN("Vulkan 1.3 is unavailable on this machine: skipping the SSAO GPU test");
		return;
	}

	constexpr u32 c_Size = 64;
	SceneRenderer renderer;
	REQUIRE(renderer.Initialize(device.Nvrhi.Get()));
	renderer.Resize(c_Size, c_Size);

	// A cube sitting on a large floor creates the corner where ambient occlusion
	// must darken the image.
	Scene scene("ssao");
	Entity floor = scene.CreateEntity("Floor");
	MeshRendererComponent floorRenderer;
	floorRenderer.Mesh = renderer.GetAssetCache().GetBuiltinMesh(PrimitiveType::Plane);
	floorRenderer.CastShadows = false;
	floor.AddComponent<MeshRendererComponent>(floorRenderer);
	floor.GetComponent<TransformComponent>().LocalScale = glm::vec3(8.0f, 1.0f, 8.0f);

	Entity cube = scene.CreateEntity("Cube");
	MeshRendererComponent cubeRenderer;
	cubeRenderer.Mesh = renderer.GetAssetCache().GetBuiltinMesh(PrimitiveType::Cube);
	cube.AddComponent<MeshRendererComponent>(cubeRenderer);
	cube.GetComponent<TransformComponent>().LocalPosition = glm::vec3(0.0f, 0.5f, 0.0f);

	Entity sun = scene.CreateEntity("Sun");
	LightComponent light;
	light.Type = LightType::Directional;
	light.Intensity = 3.0f;
	sun.AddComponent<LightComponent>(light);
	sun.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-60.0f, 25.0f, 0.0f));
	scene.UpdateTransforms();

	const glm::vec3 cameraPosition(1.5f, 1.4f, 3.0f);
	const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.0f, 0.35f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projection = glm::perspective(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);
	const glm::mat4 lightViewProjection = SceneRenderer::ComputeDirectionalShadowMatrix(scene, cameraPosition, 20.0f);

	const auto renderBrightness = [&]()
	{
		nvrhi::CommandListHandle commandList = device.Nvrhi->createCommandList();
		commandList->open();
		renderer.Render(commandList, scene, projection * view, cameraPosition, lightViewProjection, false);
		commandList->close();
		nvrhi::ICommandList* rawList = commandList;
		device.Nvrhi->executeCommandLists(&rawList, 1);
		device.Nvrhi->waitForIdle();

		u32 width = 0;
		u32 height = 0;
		const std::vector<u8> pixels = ReadBackTexture(device.Nvrhi.Get(), renderer.GetFinalColor(), width, height);
		REQUIRE(width == c_Size);
		REQUIRE(height == c_Size);
		REQUIRE(pixels.size() == static_cast<usize>(c_Size) * c_Size * 4);

		f64 luminance = 0.0;
		for (usize i = 0; i < pixels.size(); i += 4)
		{
			luminance += 0.299 * pixels[i] + 0.587 * pixels[i + 1] + 0.114 * pixels[i + 2];
		}
		return luminance / static_cast<f64>(c_Size * c_Size);
	};

	scene.GetEnvironment().EnableSSAO = true;
	const f64 withSsao = renderBrightness();
	CHECK(renderer.IsSsaoActive());
	CHECK(withSsao > 10.0); // the scene is lit, not black

	scene.GetEnvironment().EnableSSAO = false;
	const f64 withoutSsao = renderBrightness();
	CHECK_FALSE(renderer.IsSsaoActive());
	CHECK(withoutSsao > 10.0);

	// Occlusion only removes indirect light, so the frame gets darker but stays lit.
	CHECK(withoutSsao > withSsao);
	CHECK(withoutSsao - withSsao > 0.05);

	// Re-enabling must rebuild the AO path instead of leaving stale state.
	scene.GetEnvironment().EnableSSAO = true;
	const f64 reEnabled = renderBrightness();
	CHECK(renderer.IsSsaoActive());
	CHECK(reEnabled < withoutSsao);
	CHECK(reEnabled == doctest::Approx(withSsao).epsilon(0.02));

	renderer.Shutdown();
}

TEST_CASE("SceneRenderer: bloom adds energy and can be toggled at runtime")
{
	HeadlessDevice device;
	if (!CreateHeadlessDevice(device))
	{
		AE_CORE_WARN("Vulkan 1.3 is unavailable on this machine: skipping the bloom GPU test");
		return;
	}

	constexpr u32 c_Size = 64;
	SceneRenderer renderer;
	REQUIRE(renderer.Initialize(device.Nvrhi.Get()));
	renderer.Resize(c_Size, c_Size);

	// A large bright floor under a strong sun gives the bright pass plenty of
	// energy in the centre of the frame.
	Scene scene("bloom");
	AssetCache& cache = renderer.GetAssetCache();
	const Uuid materialId = Uuid::Generate();
	MaterialFactors factors;
	factors.BaseColorFactor = glm::vec4(1.0f);
	factors.RoughnessFactor = 1.0f;
	factors.MetallicFactor = 0.0f;
	REQUIRE_FALSE(cache.AddMaterial(materialId, factors, MaterialTextures{}, "bloom white").IsNull());

	Entity floor = scene.CreateEntity("Floor");
	MeshRendererComponent floorRenderer;
	floorRenderer.Mesh = cache.GetBuiltinMesh(PrimitiveType::Plane);
	floorRenderer.CastShadows = false;
	floorRenderer.Materials.push_back(AssetHandle{ materialId, AssetType::Material });
	floor.AddComponent<MeshRendererComponent>(floorRenderer);
	floor.GetComponent<TransformComponent>().LocalScale = glm::vec3(12.0f, 1.0f, 12.0f);

	Entity sun = scene.CreateEntity("Sun");
	LightComponent light;
	light.Type = LightType::Directional;
	light.Intensity = 6.0f;
	sun.AddComponent<LightComponent>(light);
	sun.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-70.0f, 0.0f, 0.0f));
	scene.UpdateTransforms();

	const glm::vec3 cameraPosition(0.0f, 2.5f, 4.0f);
	const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projection = glm::perspective(glm::radians(55.0f), 1.0f, 0.1f, 100.0f);
	const glm::mat4 lightViewProjection = SceneRenderer::ComputeDirectionalShadowMatrix(scene, cameraPosition, 20.0f);

	const auto renderBrightness = [&]()
	{
		nvrhi::CommandListHandle commandList = device.Nvrhi->createCommandList();
		commandList->open();
		renderer.Render(commandList, scene, projection * view, cameraPosition, lightViewProjection, false);
		commandList->close();
		nvrhi::ICommandList* rawList = commandList;
		device.Nvrhi->executeCommandLists(&rawList, 1);
		device.Nvrhi->waitForIdle();

		u32 width = 0;
		u32 height = 0;
		const std::vector<u8> pixels = ReadBackTexture(device.Nvrhi.Get(), renderer.GetFinalColor(), width, height);
		REQUIRE(width == c_Size);
		REQUIRE(height == c_Size);
		REQUIRE(pixels.size() == static_cast<usize>(c_Size) * c_Size * 4);
		f64 luminance = 0.0;
		for (usize i = 0; i < pixels.size(); i += 4)
		{
			luminance += 0.299 * pixels[i] + 0.587 * pixels[i + 1] + 0.114 * pixels[i + 2];
		}
		return luminance / static_cast<f64>(c_Size * c_Size);
	};

	// One warm-up frame uploads the environment map, so the measured frames are
	// not affected by first-use IBL upload state.
	renderBrightness();

	scene.GetEnvironment().EnableBloom = false;
	const f64 withoutBloom = renderBrightness();
	CHECK_FALSE(renderer.IsBloomActive());
	CHECK(withoutBloom > 20.0);

	scene.GetEnvironment().EnableBloom = true;
	scene.GetEnvironment().BloomThreshold = 0.4f;
	scene.GetEnvironment().BloomIntensity = 2.0f;
	const f64 withBloom = renderBrightness();
	CHECK(renderer.IsBloomActive());
	CHECK(withBloom > withoutBloom + 0.5);

	// Disabling restores the previous image exactly: the chain leaves no stale
	// energy behind and the tonemap stops compositing the bloom texture.
	scene.GetEnvironment().EnableBloom = false;
	const f64 reDisabled = renderBrightness();
	CHECK_FALSE(renderer.IsBloomActive());
	CHECK(reDisabled == doctest::Approx(withoutBloom).epsilon(0.001));

	renderer.Shutdown();
}

namespace
{
	std::string EncodeBase64ForImport(const std::vector<u8>& bytes)
	{
		static const char c_Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string encoded;
		encoded.reserve(((bytes.size() + 2) / 3) * 4);
		usize index = 0;
		while (index + 2 < bytes.size())
		{
			const u32 triple = (static_cast<u32>(bytes[index]) << 16) | (static_cast<u32>(bytes[index + 1]) << 8) |
				static_cast<u32>(bytes[index + 2]);
			encoded.push_back(c_Alphabet[(triple >> 18) & 0x3F]);
			encoded.push_back(c_Alphabet[(triple >> 12) & 0x3F]);
			encoded.push_back(c_Alphabet[(triple >> 6) & 0x3F]);
			encoded.push_back(c_Alphabet[triple & 0x3F]);
			index += 3;
		}
		const usize remaining = bytes.size() - index;
		if (remaining == 1)
		{
			const u32 triple = static_cast<u32>(bytes[index]) << 16;
			encoded.push_back(c_Alphabet[(triple >> 18) & 0x3F]);
			encoded.push_back(c_Alphabet[(triple >> 12) & 0x3F]);
			encoded += "==";
		}
		else if (remaining == 2)
		{
			const u32 triple = (static_cast<u32>(bytes[index]) << 16) | (static_cast<u32>(bytes[index + 1]) << 8);
			encoded.push_back(c_Alphabet[(triple >> 18) & 0x3F]);
			encoded.push_back(c_Alphabet[(triple >> 12) & 0x3F]);
			encoded.push_back(c_Alphabet[(triple >> 6) & 0x3F]);
			encoded += '=';
		}
		return encoded;
	}

	void AppendFloatsForImport(std::vector<u8>& buffer, const std::vector<f32>& values)
	{
		for (const f32 value : values)
		{
			u8 bytes[sizeof(f32)];
			std::memcpy(bytes, &value, sizeof(f32));
			buffer.insert(buffer.end(), bytes, bytes + sizeof(f32));
		}
	}

	void AppendU16ForImport(std::vector<u8>& buffer, const std::vector<u16>& values)
	{
		for (const u16 value : values)
		{
			u8 bytes[sizeof(u16)];
			std::memcpy(bytes, &value, sizeof(u16));
			buffer.insert(buffer.end(), bytes, bytes + sizeof(u16));
		}
	}

	// One untextured triangle whose material is a flat red so a rendered pixel can
		// prove that the imported material reached the GPU.
	std::string MinimalImportGltf(const std::string& bufferUri)
	{
		return std::format(R"({{
	"asset": {{ "version": "2.0", "generator": "Aegis tests" }},
	"scene": 0,
	"scenes": [{{ "nodes": [0] }}],
	"nodes": [{{ "name": "Node", "mesh": 0 }}],
	"meshes": [{{ "name": "Triangle", "primitives": [{{
		"attributes": {{ "POSITION": 0 }}, "indices": 1, "material": 0 }}] }}],
	"materials": [{{ "name": "Imported Red", "pbrMetallicRoughness": {{
		"baseColorFactor": [0.9, 0.2, 0.1, 1.0],
		"metallicFactor": 0.0,
		"roughnessFactor": 0.9 }} }}],
	"accessors": [
		{{ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0],
		  "max": [1, 1, 0] }},
		{{ "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }}],
	"bufferViews": [
		{{ "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962 }},
		{{ "buffer": 0, "byteOffset": 36, "byteLength": 8, "target": 34963 }}],
	"buffers": [{{ "byteLength": 44, "uri": "{}" }}]
}})", bufferUri);
	}
}

TEST_CASE("Scene commands: scene.importGltf creates a renderable entity")
{
	HeadlessDevice device;
	if (!CreateHeadlessDevice(device))
	{
		AE_CORE_WARN("Vulkan 1.3 is unavailable on this machine: skipping the glTF import test");
		return;
	}

	constexpr u32 c_Size = 64;
	SceneRenderer renderer;
	REQUIRE(renderer.Initialize(device.Nvrhi.Get()));
		renderer.Resize(c_Size, c_Size);

	// Build a self-contained glTF with an embedded buffer.
	std::vector<u8> geometry;
	AppendFloatsForImport(geometry, { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f });
	AppendU16ForImport(geometry, { 0, 1, 2 });
	AppendU16ForImport(geometry, { 0 });
	REQUIRE(geometry.size() == 44);
	const std::string document = MinimalImportGltf(
		"data:application/octet-stream;base64," + EncodeBase64ForImport(geometry));
	const FileSystem::Path gltfPath = FileSystem::Path("TestAssets") / FileSystem::Path("import_command.gltf");
	REQUIRE(FileSystem::WriteTextFile(gltfPath, document));

	Scene scene("import");
	Entity sun = scene.CreateEntity("Sun");
	LightComponent light;
	light.Type = LightType::Directional;
	light.Intensity = 4.0f;
	sun.AddComponent<LightComponent>(light);
	sun.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-60.0f, 0.0f, 0.0f));
	scene.UpdateTransforms();

	Uuid selection = Uuid::Null();
	EditorCommandContext context;
	context.Scene = &scene;
	context.Selection = &selection;
	context.Assets = &renderer.GetAssetCache();
	RegisterEditorCommands(context);

	const auto importParams = [](const char* path, const char* name)
	{
		JsonValue params = JsonValue::MakeObject();
		params.Set("path", std::string(path));
		params.Set("name", std::string(name));
		JsonValue position = JsonValue::MakeArray();
		position.Push(0.0);
		position.Push(0.0);
		position.Push(0.0);
		params.Set("position", std::move(position));
		return params;
	};

	// A missing file fails with a readable error instead of creating an empty entity.
	const CommandResult missing = CommandBus::Get().Execute("scene.importGltf",
		importParams("TestAssets/import_command_missing.gltf", "Missing"));
	CHECK_FALSE(missing.Success);
	CHECK(scene.FindEntity("Missing").IsValid() == false);

	const CommandResult imported = CommandBus::Get().Execute("scene.importGltf",
		importParams(FileSystem::ToUtf8(gltfPath).c_str(), "Imported Triangle"));
	REQUIRE(imported.Success);
	CHECK(imported.Value.GetString("name") == "Imported Triangle");
	CHECK(imported.Value.GetFloat("submeshes") == doctest::Approx(1.0f));
	CHECK(imported.Value.GetFloat("materials") == doctest::Approx(1.0f));

	Entity entity = scene.FindEntity("Imported Triangle");
	REQUIRE(entity.IsValid());
	REQUIRE(entity.HasComponent<MeshRendererComponent>());
	const MeshRendererComponent& rendererComponent = entity.GetComponent<MeshRendererComponent>();
	REQUIRE(rendererComponent.Mesh.IsValid());
	const AssetCache::CachedMesh* mesh = renderer.GetAssetCache().GetMesh(rendererComponent.Mesh.Id);
	REQUIRE(mesh != nullptr);
	CHECK(mesh->Geometry.GetSubmeshes().size() == 1);
	REQUIRE(mesh->MaterialIds.size() == 1);
	CHECK(renderer.GetAssetCache().GetMaterial(mesh->MaterialIds[0]) != nullptr);

	// The imported material must reach the GPU: render the triangle and check the colour.
	const glm::vec3 cameraPosition(0.3f, 0.3f, 3.0f);
	const glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.3f, 0.3f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projection = glm::perspective(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);
	const glm::mat4 lightViewProjection = SceneRenderer::ComputeDirectionalShadowMatrix(scene, cameraPosition, 20.0f);
	nvrhi::CommandListHandle commandList = device.Nvrhi->createCommandList();
	commandList->open();
	renderer.Render(commandList, scene, projection * view, cameraPosition, lightViewProjection, false);
	commandList->close();
	nvrhi::ICommandList* rawList = commandList;
	device.Nvrhi->executeCommandLists(&rawList, 1);
	device.Nvrhi->waitForIdle();

	CHECK(renderer.GetStats().DrawCalls == 1);
	u32 width = 0;
	u32 height = 0;
	const std::vector<u8> pixels = ReadBackTexture(device.Nvrhi.Get(), renderer.GetFinalColor(), width, height);
	REQUIRE(width == c_Size);
	REQUIRE(height == c_Size);
	const usize centre = (static_cast<usize>(c_Size / 2) * c_Size + c_Size / 2) * 4;
	const glm::ivec4 centrePixel(pixels[centre + 0], pixels[centre + 1], pixels[centre + 2], pixels[centre + 3]);
	CHECK(centrePixel.r > 20);
	CHECK(centrePixel.r > centrePixel.b);

	// Leave the registry with inert handlers so no later test sees dangling state.
	context.Scene = nullptr;
	context.Assets = nullptr;
	RegisterEditorCommands(context);
	CHECK(FileSystem::RemoveFile(gltfPath));
}
