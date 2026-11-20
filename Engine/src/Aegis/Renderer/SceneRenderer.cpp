#include "Aegis/Renderer/SceneRenderer.h"

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Scene/Camera.h"
#include "Aegis/Scene/Components.h"
#include "Aegis/Scene/Entity.h"
#include "Aegis/Scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <optional>

namespace aegis
{
	namespace
	{
		// The scene renders into a half-float target and presents through an sRGB
		// texture, so the tonemapper writes linear values and the hardware encodes
		// them exactly once.
		constexpr nvrhi::Format c_HdrFormat = nvrhi::Format::RGBA16_FLOAT;
		constexpr nvrhi::Format c_DepthFormat = nvrhi::Format::D32;
		constexpr nvrhi::Format c_DisplayFormat = nvrhi::Format::SRGBA8_UNORM;
		constexpr nvrhi::Format c_AoFormat = nvrhi::Format::R8_UNORM;

		bool LoadShaderBytecode(const FileSystem::Path& directory, const char* fileName, std::vector<u8>& outBytecode)
		{
			if (!FileSystem::TryReadBinaryFile(directory / fileName, outBytecode) || outBytecode.empty())
			{
				AE_CORE_CRITICAL("Could not read the shader '{}' from '{}'", fileName, FileSystem::ToUtf8(directory));
				return false;
			}
			return true;
		}

		nvrhi::ShaderHandle CreateShader(nvrhi::IDevice* device, nvrhi::ShaderType type, const std::vector<u8>& bytecode,
			const char* debugName)
		{
			nvrhi::ShaderDesc desc;
			desc.setShaderType(type);
			desc.debugName = debugName;
			nvrhi::ShaderHandle shader = device->createShader(desc, bytecode.data(), bytecode.size());
			if (shader == nullptr)
			{
				AE_CORE_CRITICAL("Could not create the shader '{}'", debugName);
			}
			return shader;
		}

		void FillObjectConstants(SceneObjectConstants& constants, const glm::mat4& model,
			const MaterialFactors& factors)
		{
			constants.Model = model;
			constants.BaseColorFactor = factors.BaseColorFactor;
			constants.MetallicRoughnessNormalOcclusion = glm::vec4(factors.MetallicFactor, factors.RoughnessFactor,
				factors.NormalScale, factors.OcclusionStrength);
			constants.Emissive = glm::vec4(factors.EmissiveFactor, factors.EmissiveStrength);
		}

		// Full-target viewport. NVRHI takes a DX-style viewport (origin top-left), which
		// the Vulkan backend converts to a negative-height viewport internally.
		nvrhi::ViewportState FullViewport(f32 width, f32 height)
		{
			return nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(0.0f, width, 0.0f, height, 0.0f, 1.0f));
		}

		nvrhi::BindingLayoutItem PushConstantItem(u32 slot, usize size)
		{
			nvrhi::BindingLayoutItem item{};
			item.setType(nvrhi::ResourceType::PushConstants).setSlot(slot).setSize(size);
			return item;
		}

		// NVRHI maps a binding slot to a Vulkan binding as (per-type offset + slot), and
		// the default offsets reserve 0..127 for SRVs, 128.. for samplers and 256.. for
		// constant buffers. The shaders are hand written GLSL with plain 0-based
		// bindings, so every layout pins the offsets to zero to make slot == binding.
		nvrhi::VulkanBindingOffsets ZeroBindingOffsets()
		{
			nvrhi::VulkanBindingOffsets offsets;
			offsets.shaderResource = 0;
			offsets.sampler = 0;
			offsets.constantBuffer = 0;
			offsets.unorderedAccess = 0;
			return offsets;
		}
	}

	SceneRenderer::~SceneRenderer()
	{
		Shutdown();
	}

	bool SceneRenderer::Initialize(nvrhi::IDevice* device)
	{
		if (device == nullptr)
		{
			AE_CORE_CRITICAL("SceneRenderer::Initialize needs a device");
			return false;
		}
		if (m_Initialized)
		{
			return true;
		}

		m_Device = device;
		if (!createBindingLayouts() || !createShadowResources() || !loadShaders() || !createPipelines())
		{
			Shutdown();
			return false;
		}
		if (!m_AssetCache.Initialize(device, m_MaterialBindingLayout))
		{
			Shutdown();
			return false;
		}
		m_AssetCache.RegisterBuiltinPrimitives();

		m_Width = 1;
		m_Height = 1;
		if (!createFrameResources())
		{
			Shutdown();
			return false;
		}

		m_Initialized = true;
		AE_CORE_INFO("Scene renderer ready ({}x{} shadow map)", c_ShadowMapResolution, c_ShadowMapResolution);
		return true;
	}

	void SceneRenderer::Shutdown()
	{
		if (m_Device != nullptr)
		{
			m_Device->waitForIdle();
		}

		releaseFrameResources();
		m_AssetCache.Shutdown();

		m_FrameBindingSet = nullptr;
		m_DepthBindingSet = nullptr;
		m_PostBindingSet = nullptr;
		m_FrameBindingLayout = nullptr;
		m_MaterialBindingLayout = nullptr;
		m_DepthBindingLayout = nullptr;
		m_PostBindingLayout = nullptr;

		m_ShadowPipeline = nullptr;
		m_PbrPipeline = nullptr;
		m_TonemapPipeline = nullptr;
		m_SkyPipeline = nullptr;
		m_SkyPixelShader = nullptr;
		m_PrepassPipeline = nullptr;
		m_SsaoPipeline = nullptr;
		m_SsaoBlurPipeline = nullptr;
		m_SsaoPixelShader = nullptr;
		m_SsaoBlurPixelShader = nullptr;
		m_SsaoBindingLayout = nullptr;
		m_BlurBindingLayout = nullptr;
		m_SsaoBindingSet = nullptr;
		m_BlurBindingSet = nullptr;
		m_CameraDepthBindingSet = nullptr;
		m_CameraDepthConstantBuffer = nullptr;
		m_AoTexture = nullptr;
		m_AoBlurTexture = nullptr;
		m_AoFallback = nullptr;
		m_PrepassFramebuffer = nullptr;
		m_AoFramebuffer = nullptr;
		m_AoBlurFramebuffer = nullptr;
		m_AoSampler = nullptr;
		m_SsaoActive = false;

		m_SceneInputLayout = nullptr;
		m_DepthInputLayout = nullptr;
		m_PbrVertexShader = nullptr;
		m_PbrPixelShader = nullptr;
		m_DepthVertexShader = nullptr;
		m_FullscreenVertexShader = nullptr;
		m_TonemapPixelShader = nullptr;

		m_FrameConstantBuffer = nullptr;
		m_DepthConstantBuffer = nullptr;
		m_ShadowSampler = nullptr;
		m_EnvironmentSampler = nullptr;
		m_HdrSampler = nullptr;
		m_IrradianceCube = nullptr;
		m_SpecularCube = nullptr;
		m_BrdfLut = nullptr;
		m_EnvironmentMap = EnvironmentMap{};
		m_EnvironmentSource.clear();
		m_EnvironmentActive = false;
		m_ShadowMap = nullptr;
		m_ShadowFramebuffer = nullptr;

		m_DrawItems.clear();
		m_Device = nullptr;
		m_Initialized = false;
	}

	bool SceneRenderer::createBindingLayouts()
	{
		// Set 0 of the forward pass: frame constants, the shadow map and the
		// per-object push constants.
		nvrhi::BindingLayoutDesc frameLayoutDesc;
		frameLayoutDesc.visibility = nvrhi::ShaderType::All;
		frameLayoutDesc.registerSpace = 0;
		frameLayoutDesc.registerSpaceIsDescriptorSet = true;
		frameLayoutDesc.bindingOffsets = ZeroBindingOffsets();
		frameLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::ConstantBuffer(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::Sampler(2),
			PushConstantItem(3, sizeof(SceneObjectConstants)),
			// Image-based lighting: irradiance cube, prefiltered specular cube, BRDF LUT.
			nvrhi::BindingLayoutItem::Texture_SRV(4),
			nvrhi::BindingLayoutItem::Texture_SRV(5),
			nvrhi::BindingLayoutItem::Texture_SRV(6),
			nvrhi::BindingLayoutItem::Sampler(7),
			// Screen-space ambient occlusion result (written by the SSAO blur pass).
			nvrhi::BindingLayoutItem::Texture_SRV(8),
			nvrhi::BindingLayoutItem::Sampler(9),
		};
		m_FrameBindingLayout = m_Device->createBindingLayout(frameLayoutDesc);

		// Set 1: per-material maps and the samplers they are read with.
		nvrhi::BindingLayoutDesc materialLayoutDesc;
		materialLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
		materialLayoutDesc.registerSpace = 1;
		materialLayoutDesc.registerSpaceIsDescriptorSet = true;
		materialLayoutDesc.bindingOffsets = ZeroBindingOffsets();
		materialLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::Texture_SRV(2),
			nvrhi::BindingLayoutItem::Texture_SRV(3),
			nvrhi::BindingLayoutItem::Texture_SRV(4),
			nvrhi::BindingLayoutItem::Sampler(5),
			nvrhi::BindingLayoutItem::Sampler(6),
			nvrhi::BindingLayoutItem::Sampler(7),
			nvrhi::BindingLayoutItem::Sampler(8),
			nvrhi::BindingLayoutItem::Sampler(9),
		};
		m_MaterialBindingLayout = m_Device->createBindingLayout(materialLayoutDesc);

		// Set 0 of the depth-only pass. It deliberately does not bind the shadow map:
		// a texture cannot be sampled while it is the depth attachment.
		nvrhi::BindingLayoutDesc depthLayoutDesc;
		depthLayoutDesc.visibility = nvrhi::ShaderType::All;
		depthLayoutDesc.registerSpace = 0;
		depthLayoutDesc.registerSpaceIsDescriptorSet = true;
		depthLayoutDesc.bindingOffsets = ZeroBindingOffsets();
		depthLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::ConstantBuffer(0),
			PushConstantItem(1, sizeof(SceneObjectConstants)),
		};
		m_DepthBindingLayout = m_Device->createBindingLayout(depthLayoutDesc);

		// Set 0 of the post pass: the HDR source plus the tonemap push constants.
		nvrhi::BindingLayoutDesc postLayoutDesc;
		postLayoutDesc.visibility = nvrhi::ShaderType::All;
		postLayoutDesc.registerSpace = 0;
		postLayoutDesc.registerSpaceIsDescriptorSet = true;
		postLayoutDesc.bindingOffsets = ZeroBindingOffsets();
		postLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			PushConstantItem(2, sizeof(ScenePostConstants)),
		};
		m_PostBindingLayout = m_Device->createBindingLayout(postLayoutDesc);

		// Set 0 of the SSAO pass: frame constants (for the inverse view projection),
		// the depth buffer and the AO parameters as push constants.
		nvrhi::BindingLayoutDesc ssaoLayoutDesc;
		ssaoLayoutDesc.visibility = nvrhi::ShaderType::All;
		ssaoLayoutDesc.registerSpace = 0;
		ssaoLayoutDesc.registerSpaceIsDescriptorSet = true;
		ssaoLayoutDesc.bindingOffsets = ZeroBindingOffsets();
		ssaoLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::ConstantBuffer(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::Sampler(2),
			PushConstantItem(3, sizeof(SceneSsaoConstants)),
		};
		m_SsaoBindingLayout = m_Device->createBindingLayout(ssaoLayoutDesc);

		// Set 0 of the SSAO blur pass.
		nvrhi::BindingLayoutDesc blurLayoutDesc;
		blurLayoutDesc.visibility = nvrhi::ShaderType::All;
		blurLayoutDesc.registerSpace = 0;
		blurLayoutDesc.registerSpaceIsDescriptorSet = true;
		blurLayoutDesc.bindingOffsets = ZeroBindingOffsets();
		blurLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			PushConstantItem(2, sizeof(SceneBlurConstants)),
		};
		m_BlurBindingLayout = m_Device->createBindingLayout(blurLayoutDesc);

		if (m_FrameBindingLayout == nullptr || m_MaterialBindingLayout == nullptr || m_DepthBindingLayout == nullptr ||
			m_PostBindingLayout == nullptr || m_SsaoBindingLayout == nullptr || m_BlurBindingLayout == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the scene renderer binding layouts");
			return false;
		}
		return true;
	}

	bool SceneRenderer::createShadowResources()
	{
		nvrhi::TextureDesc shadowDesc;
		shadowDesc.width = c_ShadowMapResolution;
		shadowDesc.height = c_ShadowMapResolution;
		shadowDesc.format = c_DepthFormat;
		shadowDesc.dimension = nvrhi::TextureDimension::Texture2D;
		shadowDesc.isRenderTarget = true;
		shadowDesc.isShaderResource = true;
		shadowDesc.clearValue = nvrhi::Color(1.0f);
		shadowDesc.useClearValue = true;
		shadowDesc.initialState = nvrhi::ResourceStates::DepthWrite;
		shadowDesc.keepInitialState = true;
		shadowDesc.debugName = "Scene shadow map";
		m_ShadowMap = m_Device->createTexture(shadowDesc);
		if (m_ShadowMap == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the shadow map");
			return false;
		}

		nvrhi::FramebufferDesc shadowFramebufferDesc;
		shadowFramebufferDesc.setDepthAttachment(m_ShadowMap);
		m_ShadowFramebuffer = m_Device->createFramebuffer(shadowFramebufferDesc);
		if (m_ShadowFramebuffer == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the shadow framebuffer");
			return false;
		}

		nvrhi::BufferDesc depthConstantDesc;
		depthConstantDesc.byteSize = sizeof(glm::mat4);
		depthConstantDesc.isConstantBuffer = true;
		depthConstantDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		depthConstantDesc.keepInitialState = true;
		depthConstantDesc.debugName = "Scene depth constants";
		m_DepthConstantBuffer = m_Device->createBuffer(depthConstantDesc);

		nvrhi::BufferDesc frameConstantDesc;
		frameConstantDesc.byteSize = sizeof(SceneFrameConstants);
		frameConstantDesc.isConstantBuffer = true;
		frameConstantDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		frameConstantDesc.keepInitialState = true;
		frameConstantDesc.debugName = "Scene frame constants";
		m_FrameConstantBuffer = m_Device->createBuffer(frameConstantDesc);
		if (m_DepthConstantBuffer == nullptr || m_FrameConstantBuffer == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the scene constant buffers");
			return false;
		}

		nvrhi::SamplerDesc shadowSamplerDesc;
		shadowSamplerDesc.setMinFilter(true);
		shadowSamplerDesc.setMagFilter(true);
		shadowSamplerDesc.setMipFilter(false);
		shadowSamplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
		m_ShadowSampler = m_Device->createSampler(shadowSamplerDesc);
		if (m_ShadowSampler == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the shadow sampler");
			return false;
		}

		nvrhi::SamplerDesc environmentSamplerDesc;
		environmentSamplerDesc.setMinFilter(true);
		environmentSamplerDesc.setMagFilter(true);
		environmentSamplerDesc.setMipFilter(true);
		environmentSamplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
		m_EnvironmentSampler = m_Device->createSampler(environmentSamplerDesc);
		if (m_EnvironmentSampler == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the environment sampler");
			return false;
		}

		// Camera-space depth prepass constant buffer (same shader and layout as the
		// shadow pass, different matrix).
		nvrhi::BufferDesc cameraDepthDesc;
		cameraDepthDesc.byteSize = sizeof(glm::mat4);
		cameraDepthDesc.isConstantBuffer = true;
		cameraDepthDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		cameraDepthDesc.keepInitialState = true;
		cameraDepthDesc.debugName = "Scene camera depth constants";
		m_CameraDepthConstantBuffer = m_Device->createBuffer(cameraDepthDesc);

		nvrhi::BindingSetDesc depthSetDesc;
		depthSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_DepthConstantBuffer));
		m_DepthBindingSet = m_Device->createBindingSet(depthSetDesc, m_DepthBindingLayout);

		nvrhi::BindingSetDesc cameraDepthSetDesc;
		cameraDepthSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_CameraDepthConstantBuffer));
		m_CameraDepthBindingSet = m_Device->createBindingSet(cameraDepthSetDesc, m_DepthBindingLayout);

		nvrhi::SamplerDesc aoSamplerDesc;
		aoSamplerDesc.setMinFilter(true);
		aoSamplerDesc.setMagFilter(true);
		aoSamplerDesc.setMipFilter(false);
		aoSamplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
		m_AoSampler = m_Device->createSampler(aoSamplerDesc);

		// 1x1 white AO fallback so the frame binding set is valid before the first
		// resize creates the real targets, and whenever SSAO is disabled.
		nvrhi::TextureDesc aoFallbackDesc;
		aoFallbackDesc.width = 1;
		aoFallbackDesc.height = 1;
		aoFallbackDesc.format = c_AoFormat;
		aoFallbackDesc.dimension = nvrhi::TextureDimension::Texture2D;
		aoFallbackDesc.isShaderResource = true;
		aoFallbackDesc.initialState = nvrhi::ResourceStates::ShaderResource;
		aoFallbackDesc.keepInitialState = true;
		aoFallbackDesc.debugName = "SSAO white fallback";
		m_AoFallback = m_Device->createTexture(aoFallbackDesc);
		m_AoTexture = m_AoFallback;
		if (m_AoFallback == nullptr || m_AoSampler == nullptr || m_CameraDepthConstantBuffer == nullptr ||
			m_CameraDepthBindingSet == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the SSAO fallback resources");
			return false;
		}

		if (nvrhi::CommandListHandle aoUpload = m_Device->createCommandList(); aoUpload != nullptr)
		{
			const u8 white = 0xFF;
			aoUpload->open();
			aoUpload->writeTexture(m_AoFallback, 0, 0, &white, 1);
			aoUpload->close();
			nvrhi::ICommandList* rawAoUpload = aoUpload;
			m_Device->executeCommandLists(&rawAoUpload, 1);
			m_Device->waitForIdle();
		}

		if (m_DepthBindingSet == nullptr || !createEnvironmentResources())
		{
			AE_CORE_CRITICAL("Could not create the scene binding sets");
			return false;
		}
		rebuildFrameBindingSet();
		if (m_FrameBindingSet == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the frame binding set");
			return false;
		}
		return true;
	}

	nvrhi::TextureHandle SceneRenderer::createCubeTexture(u32 size, u32 mips, const std::string& debugName)
	{
		nvrhi::TextureDesc desc;
		desc.width = size;
		desc.height = size;
		desc.arraySize = 6;
		desc.mipLevels = mips;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.dimension = nvrhi::TextureDimension::TextureCube;
		desc.isShaderResource = true;
		desc.isRenderTarget = false;
		desc.isUAV = false;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		desc.debugName = debugName;
		return m_Device->createTexture(desc);
	}

	bool SceneRenderer::createEnvironmentResources()
	{
		// 1x1 fallbacks keep the descriptor set valid while IBL is disabled; the
		// shader ignores them whenever uEnvironment.x is zero.
		m_IrradianceCube = createCubeTexture(1, 1, "IBL fallback irradiance");
		m_SpecularCube = createCubeTexture(1, 1, "IBL fallback specular");
		if (m_IrradianceCube == nullptr || m_SpecularCube == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the IBL fallback cubes");
			return false;
		}

		nvrhi::TextureDesc lutDesc;
		lutDesc.width = 1;
		lutDesc.height = 1;
		lutDesc.format = nvrhi::Format::RGBA16_FLOAT;
		lutDesc.dimension = nvrhi::TextureDimension::Texture2D;
		lutDesc.isShaderResource = true;
		lutDesc.initialState = nvrhi::ResourceStates::ShaderResource;
		lutDesc.keepInitialState = true;
		lutDesc.debugName = "IBL fallback BRDF LUT";
		m_BrdfLut = m_Device->createTexture(lutDesc);
		if (m_BrdfLut == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the BRDF LUT fallback");
			return false;
		}

		// RGBA16F textures need packed half floats; writeTexture does not convert.
		const std::array<u32, 2> white = { glm::packHalf2x16(glm::vec2(1.0f, 1.0f)),
			glm::packHalf2x16(glm::vec2(1.0f, 1.0f)) };
		const std::array<u32, 2> brdfFallback = { glm::packHalf2x16(glm::vec2(1.0f, 0.0f)),
			glm::packHalf2x16(glm::vec2(0.0f, 1.0f)) };
		nvrhi::CommandListHandle upload = m_Device->createCommandList();
		upload->open();
		for (u32 face = 0; face < 6; ++face)
		{
			upload->writeTexture(m_IrradianceCube, face, 0, white.data(), white.size() * sizeof(u32));
			upload->writeTexture(m_SpecularCube, face, 0, white.data(), white.size() * sizeof(u32));
		}
		upload->writeTexture(m_BrdfLut, 0, 0, brdfFallback.data(), brdfFallback.size() * sizeof(u32));
		upload->close();
		nvrhi::ICommandList* raw = upload;
		m_Device->executeCommandLists(&raw, 1);
		m_Device->waitForIdle();
		return true;
	}

	bool SceneRenderer::loadShaders()
	{
		const FileSystem::Path shaderDirectory = FileSystem::GetExecutableDirectory() / "shaders";

		std::vector<u8> pbrVertexBytecode;
		std::vector<u8> pbrPixelBytecode;
		std::vector<u8> depthVertexBytecode;
		std::vector<u8> fullscreenVertexBytecode;
		std::vector<u8> tonemapPixelBytecode;
		std::vector<u8> skyPixelBytecode;
		std::vector<u8> ssaoPixelBytecode;
		std::vector<u8> ssaoBlurPixelBytecode;
		if (!LoadShaderBytecode(shaderDirectory, "SceneSsao.frag.spv", ssaoPixelBytecode) ||
			!LoadShaderBytecode(shaderDirectory, "SceneSsaoBlur.frag.spv", ssaoBlurPixelBytecode) ||
			!LoadShaderBytecode(shaderDirectory, "SceneSky.frag.spv", skyPixelBytecode) ||
			!LoadShaderBytecode(shaderDirectory, "ScenePbr.vert.spv", pbrVertexBytecode) ||
			!LoadShaderBytecode(shaderDirectory, "ScenePbr.frag.spv", pbrPixelBytecode) ||
			!LoadShaderBytecode(shaderDirectory, "SceneDepthOnly.vert.spv", depthVertexBytecode) ||
			!LoadShaderBytecode(shaderDirectory, "ScenePost.vert.spv", fullscreenVertexBytecode) ||
			!LoadShaderBytecode(shaderDirectory, "SceneTonemap.frag.spv", tonemapPixelBytecode))
		{
			return false;
		}

		m_PbrVertexShader = CreateShader(m_Device, nvrhi::ShaderType::Vertex, pbrVertexBytecode, "ScenePbr.vert");
		m_PbrPixelShader = CreateShader(m_Device, nvrhi::ShaderType::Pixel, pbrPixelBytecode, "ScenePbr.frag");
		m_DepthVertexShader = CreateShader(m_Device, nvrhi::ShaderType::Vertex, depthVertexBytecode,
			"SceneDepthOnly.vert");
		m_FullscreenVertexShader = CreateShader(m_Device, nvrhi::ShaderType::Vertex, fullscreenVertexBytecode,
			"ScenePost.vert");
		m_TonemapPixelShader = CreateShader(m_Device, nvrhi::ShaderType::Pixel, tonemapPixelBytecode,
			"SceneTonemap.frag");
		m_SkyPixelShader = CreateShader(m_Device, nvrhi::ShaderType::Pixel, skyPixelBytecode, "SceneSky.frag");
		m_SsaoPixelShader = CreateShader(m_Device, nvrhi::ShaderType::Pixel, ssaoPixelBytecode, "SceneSsao.frag");
		m_SsaoBlurPixelShader = CreateShader(m_Device, nvrhi::ShaderType::Pixel, ssaoBlurPixelBytecode,
			"SceneSsaoBlur.frag");

		return m_PbrVertexShader != nullptr && m_PbrPixelShader != nullptr && m_DepthVertexShader != nullptr &&
			   m_FullscreenVertexShader != nullptr && m_TonemapPixelShader != nullptr &&
			   m_SkyPixelShader != nullptr && m_SsaoPixelShader != nullptr && m_SsaoBlurPixelShader != nullptr;
	}

	bool SceneRenderer::createPipelines()
	{
		nvrhi::VertexAttributeDesc sceneAttributes[4];
		sceneAttributes[0].setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT)
			.setOffset(offsetof(Vertex, Position)).setElementStride(sizeof(Vertex)).setBufferIndex(0);
		sceneAttributes[1].setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT)
			.setOffset(offsetof(Vertex, Normal)).setElementStride(sizeof(Vertex)).setBufferIndex(0);
		sceneAttributes[2].setName("TANGENT").setFormat(nvrhi::Format::RGBA32_FLOAT)
			.setOffset(offsetof(Vertex, Tangent)).setElementStride(sizeof(Vertex)).setBufferIndex(0);
		sceneAttributes[3].setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT)
			.setOffset(offsetof(Vertex, Uv)).setElementStride(sizeof(Vertex)).setBufferIndex(0);
		m_SceneInputLayout = m_Device->createInputLayout(sceneAttributes, 4, m_PbrVertexShader);

		nvrhi::VertexAttributeDesc depthAttribute;
		depthAttribute.setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT)
			.setOffset(offsetof(Vertex, Position)).setElementStride(sizeof(Vertex)).setBufferIndex(0);
		m_DepthInputLayout = m_Device->createInputLayout(&depthAttribute, 1, m_DepthVertexShader);
		if (m_SceneInputLayout == nullptr || m_DepthInputLayout == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the scene input layouts");
			return false;
		}

		nvrhi::GraphicsPipelineDesc shadowDesc;
		shadowDesc.setPrimType(nvrhi::PrimitiveType::TriangleList);
		shadowDesc.setVertexShader(m_DepthVertexShader);
		shadowDesc.setInputLayout(m_DepthInputLayout);
		shadowDesc.bindingLayouts = { m_DepthBindingLayout };
		shadowDesc.renderState.depthStencilState.depthTestEnable = true;
		shadowDesc.renderState.depthStencilState.depthWriteEnable = true;
		shadowDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
		shadowDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
		// Mesh builders (and glTF) wind front faces counter-clockwise when seen from
		// outside, which is the opposite of NVRHI's Direct3D-style default.
		shadowDesc.renderState.rasterState.frontCounterClockwise = true;
		// Slope-scaled bias removes most acne on the 2K map; the shader adds a small
		// normal-offset bias on top.
		shadowDesc.renderState.rasterState.depthBias = 0;
		shadowDesc.renderState.rasterState.slopeScaledDepthBias = 2.0f;
		shadowDesc.renderState.rasterState.depthBiasClamp = 0.0f;

		nvrhi::FramebufferInfo shadowInfo;
		shadowInfo.setDepthFormat(c_DepthFormat);
		shadowInfo.setSampleCount(1);
		m_ShadowPipeline = m_Device->createGraphicsPipeline(shadowDesc, shadowInfo);

		nvrhi::GraphicsPipelineDesc pbrDesc;
		pbrDesc.setPrimType(nvrhi::PrimitiveType::TriangleList);
		pbrDesc.setVertexShader(m_PbrVertexShader);
		pbrDesc.setPixelShader(m_PbrPixelShader);
		pbrDesc.setInputLayout(m_SceneInputLayout);
		pbrDesc.bindingLayouts = { m_FrameBindingLayout, m_MaterialBindingLayout };
		pbrDesc.renderState.depthStencilState.depthTestEnable = true;
		pbrDesc.renderState.depthStencilState.depthWriteEnable = true;
		// LessEqual so the forward pass passes the depth written by the SSAO prepass.
		pbrDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
		pbrDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
		pbrDesc.renderState.rasterState.frontCounterClockwise = true;

		nvrhi::FramebufferInfo pbrInfo;
		pbrInfo.addColorFormat(c_HdrFormat);
		pbrInfo.setDepthFormat(c_DepthFormat);
		pbrInfo.setSampleCount(1);
		m_PbrPipeline = m_Device->createGraphicsPipeline(pbrDesc, pbrInfo);

		nvrhi::GraphicsPipelineDesc skyDesc;
		skyDesc.setPrimType(nvrhi::PrimitiveType::TriangleList);
		skyDesc.setVertexShader(m_FullscreenVertexShader);
		skyDesc.setPixelShader(m_SkyPixelShader);
		skyDesc.bindingLayouts = { m_FrameBindingLayout };
		skyDesc.renderState.depthStencilState.depthTestEnable = false;
		skyDesc.renderState.depthStencilState.depthWriteEnable = false;
		skyDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;

		nvrhi::FramebufferInfo skyInfo;
		skyInfo.addColorFormat(c_HdrFormat);
		skyInfo.setDepthFormat(c_DepthFormat);
		skyInfo.setSampleCount(1);
		m_SkyPipeline = m_Device->createGraphicsPipeline(skyDesc, skyInfo);

		nvrhi::GraphicsPipelineDesc tonemapDesc;
		tonemapDesc.setPrimType(nvrhi::PrimitiveType::TriangleList);
		tonemapDesc.setVertexShader(m_FullscreenVertexShader);
		tonemapDesc.setPixelShader(m_TonemapPixelShader);
		tonemapDesc.bindingLayouts = { m_PostBindingLayout };
		tonemapDesc.renderState.depthStencilState.depthTestEnable = false;
		tonemapDesc.renderState.depthStencilState.depthWriteEnable = false;
		tonemapDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;

		nvrhi::FramebufferInfo tonemapInfo;
		tonemapInfo.addColorFormat(c_DisplayFormat);
		tonemapInfo.setSampleCount(1);
		m_TonemapPipeline = m_Device->createGraphicsPipeline(tonemapDesc, tonemapInfo);

		// Camera depth prepass: the same depth-only vertex shader as the shadow pass,
		// but without the shadow depth bias.
		nvrhi::GraphicsPipelineDesc prepassDesc;
		prepassDesc.setPrimType(nvrhi::PrimitiveType::TriangleList);
		prepassDesc.setVertexShader(m_DepthVertexShader);
		prepassDesc.setInputLayout(m_DepthInputLayout);
		prepassDesc.bindingLayouts = { m_DepthBindingLayout };
		prepassDesc.renderState.depthStencilState.depthTestEnable = true;
		prepassDesc.renderState.depthStencilState.depthWriteEnable = true;
		prepassDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
		prepassDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
		prepassDesc.renderState.rasterState.frontCounterClockwise = true;

		nvrhi::FramebufferInfo prepassInfo;
		prepassInfo.setDepthFormat(c_DepthFormat);
		prepassInfo.setSampleCount(1);
		m_PrepassPipeline = m_Device->createGraphicsPipeline(prepassDesc, prepassInfo);

		nvrhi::FramebufferInfo aoInfo;
		aoInfo.addColorFormat(c_AoFormat);
		aoInfo.setSampleCount(1);

		nvrhi::GraphicsPipelineDesc ssaoDesc;
		ssaoDesc.setPrimType(nvrhi::PrimitiveType::TriangleList);
		ssaoDesc.setVertexShader(m_FullscreenVertexShader);
		ssaoDesc.setPixelShader(m_SsaoPixelShader);
		ssaoDesc.bindingLayouts = { m_SsaoBindingLayout };
		ssaoDesc.renderState.depthStencilState.depthTestEnable = false;
		ssaoDesc.renderState.depthStencilState.depthWriteEnable = false;
		ssaoDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		m_SsaoPipeline = m_Device->createGraphicsPipeline(ssaoDesc, aoInfo);

		nvrhi::GraphicsPipelineDesc ssaoBlurDesc;
		ssaoBlurDesc.setPrimType(nvrhi::PrimitiveType::TriangleList);
		ssaoBlurDesc.setVertexShader(m_FullscreenVertexShader);
		ssaoBlurDesc.setPixelShader(m_SsaoBlurPixelShader);
		ssaoBlurDesc.bindingLayouts = { m_BlurBindingLayout };
		ssaoBlurDesc.renderState.depthStencilState.depthTestEnable = false;
		ssaoBlurDesc.renderState.depthStencilState.depthWriteEnable = false;
		ssaoBlurDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		m_SsaoBlurPipeline = m_Device->createGraphicsPipeline(ssaoBlurDesc, aoInfo);

		if (m_ShadowPipeline == nullptr || m_PbrPipeline == nullptr || m_TonemapPipeline == nullptr ||
			m_SkyPipeline == nullptr || m_PrepassPipeline == nullptr || m_SsaoPipeline == nullptr ||
			m_SsaoBlurPipeline == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the scene graphics pipelines");
			return false;
		}
		return true;
	}

	void SceneRenderer::releaseFrameResources()
	{
		m_SceneFramebuffer = nullptr;
		m_PostFramebuffer = nullptr;
		m_PostBindingSet = nullptr;
		m_HdrColor = nullptr;
		m_DepthTexture = nullptr;
		m_FinalColor = nullptr;
		m_PrepassFramebuffer = nullptr;
		m_AoFramebuffer = nullptr;
		m_AoBlurFramebuffer = nullptr;
		m_SsaoBindingSet = nullptr;
		m_BlurBindingSet = nullptr;
		m_AoTexture = m_AoFallback;
		m_AoBlurTexture = nullptr;
	}

	bool SceneRenderer::createFrameResources()
	{
		releaseFrameResources();

		nvrhi::TextureDesc hdrDesc;
		hdrDesc.width = m_Width;
		hdrDesc.height = m_Height;
		hdrDesc.format = c_HdrFormat;
		hdrDesc.dimension = nvrhi::TextureDimension::Texture2D;
		hdrDesc.isRenderTarget = true;
		hdrDesc.isShaderResource = true;
		hdrDesc.clearValue = nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f);
		hdrDesc.useClearValue = true;
		hdrDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		hdrDesc.keepInitialState = true;
		hdrDesc.debugName = "Scene HDR colour";
		m_HdrColor = m_Device->createTexture(hdrDesc);

		nvrhi::TextureDesc depthDesc;
		depthDesc.width = m_Width;
		depthDesc.height = m_Height;
		depthDesc.format = c_DepthFormat;
		depthDesc.dimension = nvrhi::TextureDimension::Texture2D;
		depthDesc.isRenderTarget = true;
		// Read by the SSAO pass, so it needs a shader-resource view.
		depthDesc.isShaderResource = true;
		depthDesc.clearValue = nvrhi::Color(1.0f);
		depthDesc.useClearValue = true;
		depthDesc.initialState = nvrhi::ResourceStates::DepthWrite;
		depthDesc.keepInitialState = true;
		depthDesc.debugName = "Scene depth";
		m_DepthTexture = m_Device->createTexture(depthDesc);

		nvrhi::TextureDesc finalDesc;
		finalDesc.width = m_Width;
		finalDesc.height = m_Height;
		finalDesc.format = c_DisplayFormat;
		finalDesc.dimension = nvrhi::TextureDimension::Texture2D;
		finalDesc.isRenderTarget = true;
		finalDesc.isShaderResource = true;
		finalDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		finalDesc.keepInitialState = true;
		finalDesc.debugName = "Scene final colour";
		m_FinalColor = m_Device->createTexture(finalDesc);

		if (m_HdrColor == nullptr || m_DepthTexture == nullptr || m_FinalColor == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the scene render targets at {}x{}", m_Width, m_Height);
			return false;
		}

		nvrhi::FramebufferDesc sceneFramebufferDesc;
		sceneFramebufferDesc.addColorAttachment(m_HdrColor);
		sceneFramebufferDesc.setDepthAttachment(m_DepthTexture);
		m_SceneFramebuffer = m_Device->createFramebuffer(sceneFramebufferDesc);

		nvrhi::FramebufferDesc postFramebufferDesc;
		postFramebufferDesc.addColorAttachment(m_FinalColor);
		m_PostFramebuffer = m_Device->createFramebuffer(postFramebufferDesc);

		if (m_HdrSampler == nullptr)
		{
			nvrhi::SamplerDesc hdrSamplerDesc;
			hdrSamplerDesc.setMinFilter(true);
			hdrSamplerDesc.setMagFilter(true);
			hdrSamplerDesc.setMipFilter(false);
			hdrSamplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
			m_HdrSampler = m_Device->createSampler(hdrSamplerDesc);
		}

		nvrhi::BindingSetDesc postSetDesc;
		postSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_HdrColor));
		postSetDesc.addItem(nvrhi::BindingSetItem::Sampler(1, m_HdrSampler));
		m_PostBindingSet = m_Device->createBindingSet(postSetDesc, m_PostBindingLayout);

		if (m_SceneFramebuffer == nullptr || m_PostFramebuffer == nullptr || m_PostBindingSet == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the scene framebuffers at {}x{}", m_Width, m_Height);
			return false;
		}

		// SSAO runs at half resolution and reads the depth buffer written by the
		// prepass. Two R8 targets ping-pong between the noise pass and the blur.
		const u32 aoWidth = Max(m_Width / 2, 1u);
		const u32 aoHeight = Max(m_Height / 2, 1u);
		nvrhi::TextureDesc aoDesc;
		aoDesc.width = aoWidth;
		aoDesc.height = aoHeight;
		aoDesc.format = c_AoFormat;
		aoDesc.dimension = nvrhi::TextureDimension::Texture2D;
		aoDesc.isRenderTarget = true;
		aoDesc.isShaderResource = true;
		aoDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		aoDesc.keepInitialState = true;
		aoDesc.debugName = "SSAO";
		m_AoTexture = m_Device->createTexture(aoDesc);
		aoDesc.debugName = "SSAO blurred";
		m_AoBlurTexture = m_Device->createTexture(aoDesc);

		nvrhi::FramebufferDesc prepassFramebufferDesc;
		prepassFramebufferDesc.setDepthAttachment(m_DepthTexture);
		m_PrepassFramebuffer = m_Device->createFramebuffer(prepassFramebufferDesc);

		nvrhi::FramebufferDesc aoFramebufferDesc;
		aoFramebufferDesc.addColorAttachment(m_AoTexture);
		m_AoFramebuffer = m_Device->createFramebuffer(aoFramebufferDesc);

		nvrhi::FramebufferDesc aoBlurFramebufferDesc;
		aoBlurFramebufferDesc.addColorAttachment(m_AoBlurTexture);
		m_AoBlurFramebuffer = m_Device->createFramebuffer(aoBlurFramebufferDesc);

		nvrhi::BindingSetDesc ssaoSetDesc;
		ssaoSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstantBuffer));
		ssaoSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_DepthTexture));
		ssaoSetDesc.addItem(nvrhi::BindingSetItem::Sampler(2, m_AoSampler));
		m_SsaoBindingSet = m_Device->createBindingSet(ssaoSetDesc, m_SsaoBindingLayout);

		nvrhi::BindingSetDesc blurSetDesc;
		blurSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_AoTexture));
		blurSetDesc.addItem(nvrhi::BindingSetItem::Sampler(1, m_AoSampler));
		m_BlurBindingSet = m_Device->createBindingSet(blurSetDesc, m_BlurBindingLayout);

		rebuildFrameBindingSet();

		if (m_PrepassFramebuffer == nullptr || m_AoFramebuffer == nullptr || m_AoBlurFramebuffer == nullptr ||
			m_SsaoBindingSet == nullptr || m_BlurBindingSet == nullptr || m_FrameBindingSet == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the SSAO resources at {}x{}", aoWidth, aoHeight);
			return false;
		}
		return true;
	}

	void SceneRenderer::Resize(u32 width, u32 height)
	{
		const u32 clampedWidth = Max(width, 1u);
		const u32 clampedHeight = Max(height, 1u);
		if (m_Device == nullptr || (m_Width == clampedWidth && m_Height == clampedHeight && m_HdrColor != nullptr))
		{
			return;
		}

		m_Width = clampedWidth;
		m_Height = clampedHeight;
		if (!createFrameResources())
		{
			AE_CORE_ERROR("Scene renderer could not resize to {}x{}", m_Width, m_Height);
		}
	}

	void SceneRenderer::rebuildFrameBindingSet()
	{
		if (m_Device == nullptr || m_FrameConstantBuffer == nullptr || m_ShadowMap == nullptr ||
			m_IrradianceCube == nullptr || m_SpecularCube == nullptr || m_BrdfLut == nullptr ||
			m_EnvironmentSampler == nullptr || m_AoFallback == nullptr || m_AoSampler == nullptr)
		{
			return;
		}

		nvrhi::BindingSetDesc frameSetDesc;
		frameSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstantBuffer));
		frameSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_ShadowMap));
		frameSetDesc.addItem(nvrhi::BindingSetItem::Sampler(2, m_ShadowSampler));
		nvrhi::BindingSetItem irradianceItem = nvrhi::BindingSetItem::Texture_SRV(4, m_IrradianceCube);
		irradianceItem.dimension = nvrhi::TextureDimension::TextureCube;
		frameSetDesc.addItem(irradianceItem);
		nvrhi::BindingSetItem specularItem = nvrhi::BindingSetItem::Texture_SRV(5, m_SpecularCube);
		specularItem.dimension = nvrhi::TextureDimension::TextureCube;
		frameSetDesc.addItem(specularItem);
		frameSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(6, m_BrdfLut));
		frameSetDesc.addItem(nvrhi::BindingSetItem::Sampler(7, m_EnvironmentSampler));
		frameSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(8, m_AoBlurTexture != nullptr ? m_AoBlurTexture : m_AoFallback));
		frameSetDesc.addItem(nvrhi::BindingSetItem::Sampler(9, m_AoSampler));
		m_FrameBindingSet = m_Device->createBindingSet(frameSetDesc, m_FrameBindingLayout);
		if (m_FrameBindingSet == nullptr)
		{
			AE_CORE_CRITICAL("Could not rebuild the frame binding set after the environment changed");
		}
	}

	void SceneRenderer::uploadEnvironment(const EnvironmentMap& environment, nvrhi::ICommandList* commandList)
	{
		(void)commandList;
		if (!environment.IsValid())
		{
			return;
		}

		nvrhi::TextureHandle irradiance = createCubeTexture(environment.IrradianceSize, 1, "IBL irradiance");
		nvrhi::TextureHandle specular = createCubeTexture(environment.SpecularSize, environment.SpecularMipCount,
			"IBL prefiltered specular");
		if (irradiance == nullptr || specular == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the IBL cube textures");
			return;
		}

		nvrhi::TextureDesc lutDesc;
		lutDesc.width = environment.BrdfSize;
		lutDesc.height = environment.BrdfSize;
		lutDesc.format = nvrhi::Format::RGBA16_FLOAT;
		lutDesc.dimension = nvrhi::TextureDimension::Texture2D;
		lutDesc.isShaderResource = true;
		lutDesc.initialState = nvrhi::ResourceStates::ShaderResource;
		lutDesc.keepInitialState = true;
		lutDesc.debugName = "IBL BRDF LUT";
		nvrhi::TextureHandle brdfLut = m_Device->createTexture(lutDesc);
		if (brdfLut == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the BRDF LUT");
			return;
		}

		nvrhi::CommandListHandle upload = m_Device->createCommandList();
		upload->open();

		// RGBA16F upload: two half2 per texel. writeTexture copies raw bytes, so the
		// data must already be in the texture format.
		const auto packRgba16f = [](const std::vector<glm::vec3>& source, std::vector<u32>& out)
		{
			out.resize(source.size() * 2);
			for (usize i = 0; i < source.size(); ++i)
			{
				out[i * 2] = glm::packHalf2x16(glm::vec2(source[i].x, source[i].y));
				out[i * 2 + 1] = glm::packHalf2x16(glm::vec2(source[i].z, 1.0f));
			}
		};

		std::vector<u32> staging;
		for (u32 face = 0; face < 6; ++face)
		{
			packRgba16f(environment.Irradiance[face], staging);
			upload->writeTexture(irradiance, face, 0, staging.data(),
				static_cast<usize>(environment.IrradianceSize) * 2 * sizeof(u32));
		}

		for (u32 mip = 0; mip < environment.SpecularMipCount && mip < static_cast<u32>(environment.Specular.size()); ++mip)
		{
			const u32 mipSize = std::max(1u, environment.SpecularSize >> mip);
			for (u32 face = 0; face < 6; ++face)
			{
				packRgba16f(environment.Specular[mip][face], staging);
				upload->writeTexture(specular, face, mip, staging.data(),
					static_cast<usize>(mipSize) * 2 * sizeof(u32));
			}
		}

		staging.resize(environment.Brdf.size() * 2);
		for (usize i = 0; i < environment.Brdf.size(); ++i)
		{
			staging[i * 2] = glm::packHalf2x16(glm::vec2(environment.Brdf[i].x, environment.Brdf[i].y));
			staging[i * 2 + 1] = glm::packHalf2x16(glm::vec2(0.0f, 1.0f));
		}
		upload->writeTexture(brdfLut, 0, 0, staging.data(),
			static_cast<usize>(environment.BrdfSize) * 2 * sizeof(u32));

		upload->close();
		nvrhi::ICommandList* rawUpload = upload;
		m_Device->executeCommandLists(&rawUpload, 1);
		m_Device->waitForIdle();

		m_IrradianceCube = std::move(irradiance);
		m_SpecularCube = std::move(specular);
		m_BrdfLut = std::move(brdfLut);
	}

	void SceneRenderer::updateEnvironment(Scene& scene, nvrhi::ICommandList* commandList)
	{
		const EnvironmentSettings& environment = scene.GetEnvironment();
		const std::string key = environment.EnableIBL ? environment.HdriPath : std::string();

		if (!environment.EnableIBL)
		{
			// The shader skips IBL entirely when the flag is zero; the previously
			// uploaded cubes stay bound so no mid-frame upload is needed.
			m_EnvironmentActive = false;
			return;
		}

		if (m_EnvironmentActive && key == m_EnvironmentSource && m_EnvironmentMap.IsValid())
		{
			return;
		}

		const auto start = std::chrono::steady_clock::now();
		EnvironmentMap map;
		if (!key.empty())
		{
			const FileSystem::Path path = FileSystem::FromUtf8(key);
			if (!FileSystem::IsFile(path))
			{
				AE_CORE_WARN("HDRI '{}' was not found; using the procedural sky for IBL", key);
				map = EnvironmentMap::BuildProceduralSky();
			}
			else if (std::optional<EnvironmentMap> loaded = EnvironmentMap::LoadFromHdrFile(path))
			{
				map = std::move(*loaded);
			}
			else
			{
				map = EnvironmentMap::BuildProceduralSky();
			}
		}
		else
		{
			// The procedural sky never changes: precompute it once per process and
			// upload it into each device's cubes.
			static const EnvironmentMap s_ProceduralSky = EnvironmentMap::BuildProceduralSky();
			map = s_ProceduralSky;
		}

		if (!map.IsValid())
		{
			AE_CORE_ERROR("Could not build the environment map; keeping the previous one");
			return;
		}

		uploadEnvironment(map, commandList);
		m_EnvironmentMap = std::move(map);
		m_EnvironmentSource = key;
		m_EnvironmentActive = true;
		rebuildFrameBindingSet();

		const f64 milliseconds =
			std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
		AE_CORE_INFO("IBL environment '{}' built ({}² specular, {} mips, {}² BRDF) in {:.1f} ms",
			key.empty() ? "procedural sky" : key, m_EnvironmentMap.SpecularSize, m_EnvironmentMap.SpecularMipCount,
			m_EnvironmentMap.BrdfSize, milliseconds);
	}

	void SceneRenderer::gatherDrawItems(Scene& scene, const glm::vec3& cameraPosition)
	{
		(void)cameraPosition;
		m_DrawItems.clear();
		m_Stats.VisibleMeshes = 0;
		m_Stats.Triangles = 0;

		ecs::Registry& registry = scene.GetRegistry();
		registry.Each<MeshRendererComponent, TransformComponent>(
			[this, &scene](ecs::EntityHandle handle, MeshRendererComponent& renderer, TransformComponent& transform)
			{
				if (!renderer.Visible)
				{
					return;
				}
				const Entity entity(handle, &scene);
				if (!entity.IsActive())
				{
					return;
				}

				const AssetCache::CachedMesh* mesh = renderer.Mesh.IsValid()
													  ? m_AssetCache.GetMesh(renderer.Mesh.Id)
													  : nullptr;
				if (mesh == nullptr)
				{
					// Scenes and projects can reference a mesh that was never imported or
					// that the cache does not know yet. Drawing the built-in cube keeps the
					// entity visible instead of silently vanishing from the frame.
					mesh = m_AssetCache.GetMesh(m_AssetCache.GetBuiltinMesh(PrimitiveType::Cube).Id);
					if (mesh == nullptr)
					{
						return;
					}
					if (m_MissingMeshWarnings.insert(entity.GetName()).second)
					{
						AE_CORE_WARN("Entity '{}' references a mesh asset that is not loaded; drawing the built-in cube",
							entity.GetName());
					}
				}

				const glm::mat4 model = transform.GetWorldTransform();
				bool anySubmesh = false;
				for (usize submeshIndex = 0; submeshIndex < mesh->Geometry.GetSubmeshes().size(); ++submeshIndex)
				{
					const Submesh& submesh = mesh->Geometry.GetSubmeshes()[submeshIndex];

					// A per-entity material override wins; otherwise the material the mesh
					// asset was registered with is used.
					Uuid materialId = Uuid::Null();
					if (submeshIndex < renderer.Materials.size())
					{
						materialId = renderer.Materials[submeshIndex].Id;
					}
					if (materialId.IsNull() && submeshIndex < mesh->MaterialIds.size())
					{
						materialId = mesh->MaterialIds[submeshIndex];
					}

					const Material* material = m_AssetCache.GetMaterial(materialId);
					if (material == nullptr)
					{
						AE_CORE_DEBUG("Mesh '{}' submesh {} has no resident material", mesh->Name, submeshIndex);
						continue;
					}

					DrawItem item;
					item.Mesh = mesh;
					item.Material = material;
					item.Model = model;
					item.SubmeshIndex = static_cast<u32>(submeshIndex);
					item.CastShadows = renderer.CastShadows;
					item.ReceiveShadows = renderer.ReceiveShadows;
					m_DrawItems.push_back(item);

					m_Stats.Triangles += submesh.IndexCount / 3;
					anySubmesh = true;
				}

				if (anySubmesh)
				{
					++m_Stats.VisibleMeshes;
				}
			});
	}

	void SceneRenderer::fillFrameConstants(Scene& scene, const glm::mat4& viewProjection,
		const glm::vec3& cameraPosition, const glm::mat4& lightViewProjection, bool renderShadows)
	{
		const EnvironmentSettings& environment = scene.GetEnvironment();

		m_FrameConstants.ViewProjection = viewProjection;
		m_FrameConstants.InvViewProjection = glm::inverse(viewProjection);
		m_FrameConstants.LightViewProjection = lightViewProjection;
		m_FrameConstants.CameraPosition = glm::vec4(cameraPosition, 1.0f);
		m_FrameConstants.Ambient = glm::vec4(environment.AmbientColor, environment.AmbientIntensity);

		// Fall back to a fixed sun direction so a scene without a light still renders
		// (intensity 0 keeps its contribution at zero).
		m_FrameConstants.SunDirection = glm::vec4(glm::normalize(glm::vec3(-0.45f, -1.0f, -0.35f)), 0.0f);
		m_FrameConstants.SunColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		m_FrameConstants.LightCounts = glm::uvec4(0u, 0u, 0u, 0u);
		m_FrameConstants.Lights = {};

		bool sunAssigned = false;
		u32 pointLightCount = 0;
		for (Entity entity : scene.GetEntities())
		{
			const LightComponent* light = entity.TryGetComponent<LightComponent>();
			if (light == nullptr)
			{
				continue;
			}
			const TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
			if (transform == nullptr)
			{
				continue;
			}

			if (light->Type == LightType::Directional)
			{
				if (sunAssigned)
				{
					continue;
				}
				// The transform's forward axis is where the light points, so the direction
				// towards it is the negated forward.
				const glm::vec3 forward = Camera::GetForward(*transform);
				m_FrameConstants.SunDirection = glm::vec4(glm::normalize(-forward), Max(light->Intensity, 0.0f));
				m_FrameConstants.SunColor = glm::vec4(light->Color, light->CastShadows ? 1.0f : 0.0f);
				sunAssigned = true;
				continue;
			}

			if (pointLightCount >= c_MaxSceneLights)
			{
				continue;
			}

			const glm::vec3 position = transform->WorldPosition;
			const glm::vec3 forward = Camera::GetForward(*transform);
			const f32 range = Max(light->Range, 0.01f);
			const f32 innerCone = glm::cos(glm::radians(Min(Max(light->InnerConeDegrees, 0.0f), 89.0f)));
			const f32 outerCone = glm::cos(glm::radians(Min(Max(light->OuterConeDegrees, 0.0f), 89.5f)));

			SceneLightConstants& destination = m_FrameConstants.Lights[pointLightCount];
			destination.PositionRange = glm::vec4(position, range);
			destination.ColorIntensity = glm::vec4(light->Color, Max(light->Intensity, 0.0f));
			destination.DirectionType = glm::vec4(forward,
				light->Type == LightType::Spot ? 2.0f : 1.0f);
			destination.SpotAngles = glm::vec4(innerCone, outerCone, 0.0f, 0.0f);
			++pointLightCount;
		}

		m_FrameConstants.LightCounts.x = pointLightCount;
		m_FrameConstants.PostProcess = glm::vec4(Max(environment.Exposure, 0.0f), environment.BloomIntensity,
			environment.ShadowDistance, renderShadows ? 1.0f : 0.0f);
		m_FrameConstants.Environment = glm::vec4(m_EnvironmentActive ? 1.0f : 0.0f,
			static_cast<f32>(Max(m_EnvironmentMap.SpecularMipCount, 1u)), environment.HdriIntensity,
			m_SsaoActive ? 1.0f : 0.0f);
		m_SsaoConstants.Params = glm::vec4(Max(environment.SSAORadius, 0.01f), 0.02f,
			m_SsaoActive ? 1.0f : 0.0f, 0.0f);
		m_Stats.PointLights = pointLightCount;
	}

	void SceneRenderer::recordShadowPass(nvrhi::ICommandList* commandList)
	{
		commandList->beginMarker("Shadow map");
		commandList->clearDepthStencilTexture(m_ShadowMap, nvrhi::AllSubresources, true, 1.0f, false, 0);

		nvrhi::GraphicsState state;
		state.pipeline = m_ShadowPipeline;
		state.framebuffer = m_ShadowFramebuffer;
		state.bindings = { m_DepthBindingSet };
		state.setViewport(FullViewport(static_cast<f32>(c_ShadowMapResolution), static_cast<f32>(c_ShadowMapResolution)));
		commandList->setGraphicsState(state);

		for (const DrawItem& item : m_DrawItems)
		{
			if (!item.CastShadows)
			{
				continue;
			}
			const Submesh& submesh = item.Mesh->Geometry.GetSubmeshes()[item.SubmeshIndex];

			nvrhi::GraphicsState itemState;
			itemState.pipeline = m_ShadowPipeline;
			itemState.framebuffer = m_ShadowFramebuffer;
			itemState.bindings = { m_DepthBindingSet };
			itemState.setViewport(state.viewport);
			itemState.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(item.Mesh->VertexBuffer)
				.setSlot(0).setOffset(0));
			itemState.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(item.Mesh->IndexBuffer)
				.setFormat(nvrhi::Format::R32_UINT).setOffset(0));
			commandList->setGraphicsState(itemState);

			SceneObjectConstants constants;
			FillObjectConstants(constants, item.Model, item.Material->GetFactors());
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->drawIndexed(nvrhi::DrawArguments()
				.setVertexCount(submesh.IndexCount)
				.setStartIndexLocation(submesh.FirstIndex));
			++m_Stats.ShadowDrawCalls;
		}
		commandList->endMarker();
	}

	void SceneRenderer::recordDepthPrepass(nvrhi::ICommandList* commandList)
	{
		commandList->beginMarker("Depth prepass");
		commandList->clearDepthStencilTexture(m_DepthTexture, nvrhi::AllSubresources, true, 1.0f, false, 0);

		const nvrhi::ViewportState viewport = FullViewport(static_cast<f32>(m_Width), static_cast<f32>(m_Height));
		for (const DrawItem& item : m_DrawItems)
		{
			const Submesh& submesh = item.Mesh->Geometry.GetSubmeshes()[item.SubmeshIndex];

			nvrhi::GraphicsState state;
			state.pipeline = m_PrepassPipeline;
			state.framebuffer = m_PrepassFramebuffer;
			state.bindings = { m_CameraDepthBindingSet };
			state.setViewport(viewport);
			state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(item.Mesh->VertexBuffer).setSlot(0)
				.setOffset(0));
			state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(item.Mesh->IndexBuffer)
				.setFormat(nvrhi::Format::R32_UINT).setOffset(0));
			commandList->setGraphicsState(state);

			SceneObjectConstants constants;
			FillObjectConstants(constants, item.Model, item.Material->GetFactors());
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(submesh.IndexCount)
				.setStartIndexLocation(submesh.FirstIndex));
		}
		commandList->endMarker();
	}

	void SceneRenderer::recordSsaoPass(nvrhi::ICommandList* commandList)
	{
		const nvrhi::TextureDesc aoDesc = m_AoTexture->getDesc();
		commandList->beginMarker("SSAO");
		nvrhi::GraphicsState state;
		state.pipeline = m_SsaoPipeline;
		state.framebuffer = m_AoFramebuffer;
		state.bindings = { m_SsaoBindingSet };
		state.setViewport(FullViewport(static_cast<f32>(aoDesc.width), static_cast<f32>(aoDesc.height)));
		commandList->setGraphicsState(state);
		commandList->setPushConstants(&m_SsaoConstants, sizeof(m_SsaoConstants));
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
		commandList->endMarker();
	}

	void SceneRenderer::recordSsaoBlur(nvrhi::ICommandList* commandList)
	{
		const nvrhi::TextureDesc aoDesc = m_AoTexture->getDesc();
		commandList->beginMarker("SSAO blur");
		nvrhi::GraphicsState state;
		state.pipeline = m_SsaoBlurPipeline;
		state.framebuffer = m_AoBlurFramebuffer;
		state.bindings = { m_BlurBindingSet };
		state.setViewport(FullViewport(static_cast<f32>(aoDesc.width), static_cast<f32>(aoDesc.height)));
		commandList->setGraphicsState(state);

		SceneBlurConstants blurConstants;
		blurConstants.TexelSize = glm::vec4(1.0f / static_cast<f32>(aoDesc.width),
			1.0f / static_cast<f32>(aoDesc.height), 0.0f, 0.0f);
		commandList->setPushConstants(&blurConstants, sizeof(blurConstants));
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
		commandList->endMarker();
	}

	void SceneRenderer::recordSkyPass(nvrhi::ICommandList* commandList)
	{
		commandList->beginMarker("Sky");
		nvrhi::GraphicsState state;
		state.pipeline = m_SkyPipeline;
		state.framebuffer = m_SceneFramebuffer;
		state.bindings = { m_FrameBindingSet };
		state.setViewport(FullViewport(static_cast<f32>(m_Width), static_cast<f32>(m_Height)));
		commandList->setGraphicsState(state);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
		commandList->endMarker();
	}

	void SceneRenderer::recordForwardPass(nvrhi::ICommandList* commandList)
	{
		commandList->beginMarker("Forward PBR");
		commandList->clearTextureFloat(m_HdrColor, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
		if (!m_SsaoActive)
		{
			// With SSAO the prepass already cleared the depth buffer.
			commandList->clearDepthStencilTexture(m_DepthTexture, nvrhi::AllSubresources, true, 1.0f, false, 0);
		}

		// The sky fills every pixel the geometry leaves untouched and does not write
		// depth, so the PBR pass simply draws over it.
		recordSkyPass(commandList);

		const nvrhi::ViewportState viewport = FullViewport(static_cast<f32>(m_Width), static_cast<f32>(m_Height));
		for (const DrawItem& item : m_DrawItems)
		{
			const Submesh& submesh = item.Mesh->Geometry.GetSubmeshes()[item.SubmeshIndex];

			nvrhi::GraphicsState state;
			state.pipeline = m_PbrPipeline;
			state.framebuffer = m_SceneFramebuffer;
			state.bindings = { m_FrameBindingSet, item.Material->GetBindingSet() };
			state.setViewport(viewport);
			state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(item.Mesh->VertexBuffer).setSlot(0)
				.setOffset(0));
			state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(item.Mesh->IndexBuffer)
				.setFormat(nvrhi::Format::R32_UINT).setOffset(0));
			commandList->setGraphicsState(state);

			SceneObjectConstants constants;
			FillObjectConstants(constants, item.Model, item.Material->GetFactors());
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->drawIndexed(nvrhi::DrawArguments()
				.setVertexCount(submesh.IndexCount)
				.setStartIndexLocation(submesh.FirstIndex));
			++m_Stats.DrawCalls;
		}
		commandList->endMarker();
	}

	void SceneRenderer::recordPostPass(nvrhi::ICommandList* commandList)
	{
		commandList->beginMarker("Tonemap");
		nvrhi::GraphicsState state;
		state.pipeline = m_TonemapPipeline;
		state.framebuffer = m_PostFramebuffer;
		state.bindings = { m_PostBindingSet };
		state.setViewport(FullViewport(static_cast<f32>(m_Width), static_cast<f32>(m_Height)));
		commandList->setGraphicsState(state);

		ScenePostConstants postConstants;
		postConstants.PostProcess = m_FrameConstants.PostProcess;
		commandList->setPushConstants(&postConstants, sizeof(postConstants));
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
		commandList->endMarker();
	}

	void SceneRenderer::Render(nvrhi::ICommandList* commandList, Scene& scene, const glm::mat4& viewProjection,
		const glm::vec3& cameraPosition, const glm::mat4& lightViewProjection, bool renderShadows)
	{
		if (!m_Initialized || commandList == nullptr || m_HdrColor == nullptr)
		{
			return;
		}

		m_Stats = SceneRendererStats{};
		const EnvironmentSettings& environment = scene.GetEnvironment();
		m_SsaoActive = environment.EnableSSAO && m_PrepassPipeline != nullptr && m_SsaoPipeline != nullptr &&
					   m_SsaoBlurPipeline != nullptr && m_AoFramebuffer != nullptr && m_AoBlurFramebuffer != nullptr &&
					   m_SsaoBindingSet != nullptr && m_BlurBindingSet != nullptr && m_PrepassFramebuffer != nullptr;
		updateEnvironment(scene, commandList);
		gatherDrawItems(scene, cameraPosition);
		fillFrameConstants(scene, viewProjection, cameraPosition, lightViewProjection, renderShadows);

		commandList->writeBuffer(m_FrameConstantBuffer, &m_FrameConstants, sizeof(m_FrameConstants));

		const bool castShadows = renderShadows && m_FrameConstants.SunDirection.w > 0.0f &&
								 m_FrameConstants.SunColor.w > 0.0f;
		if (castShadows)
		{
			const glm::mat4 lightViewProjectionTransposed = m_FrameConstants.LightViewProjection;
			commandList->writeBuffer(m_DepthConstantBuffer, &lightViewProjectionTransposed,
				sizeof(lightViewProjectionTransposed));
			recordShadowPass(commandList);
		}

		if (m_SsaoActive)
		{
			commandList->writeBuffer(m_CameraDepthConstantBuffer, &m_FrameConstants.ViewProjection,
				sizeof(glm::mat4));
			recordDepthPrepass(commandList);
			recordSsaoPass(commandList);
			recordSsaoBlur(commandList);
		}

		recordForwardPass(commandList);
		recordPostPass(commandList);
	}

	glm::mat4 SceneRenderer::ComputeDirectionalShadowMatrix(const Scene& scene, const glm::vec3& cameraPosition,
		f32 shadowDistance)
	{
		// The direction the light travels. Scenes without a sun use the same fallback
		// direction fillFrameConstants assumes.
		glm::vec3 lightDirection = glm::normalize(glm::vec3(-0.45f, -1.0f, -0.35f));
		for (Entity entity : scene.GetEntities())
		{
			const LightComponent* light = entity.TryGetComponent<LightComponent>();
			if (light == nullptr || light->Type != LightType::Directional)
			{
				continue;
			}
			if (const TransformComponent* transform = entity.TryGetComponent<TransformComponent>())
			{
				lightDirection = glm::normalize(Camera::GetForward(*transform));
			}
			break;
		}

		// Fit a box of 2 * extent around the camera; the eye distance leaves room for
		// geometry above the camera that may still cast into the visible area.
		const f32 extent = Max(shadowDistance * 0.5f, 1.0f);
		const glm::vec3 center = cameraPosition;
		const glm::vec3 eye = center - lightDirection * (extent * 2.0f);
		const glm::vec3 up = std::abs(lightDirection.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
																: glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::mat4 view = glm::lookAt(eye, center, up);
		const glm::mat4 projection = glm::ortho(-extent, extent, -extent, extent, 0.05f, extent * 4.5f);
		return projection * view;
	}
}
