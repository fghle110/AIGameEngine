#include "Aegis/Renderer/ImGuiRenderer.h"

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Core/Window.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace aegis
{
	namespace
	{
		constexpr usize c_InitialVertexBufferSize = 64 * 1024;
		constexpr usize c_InitialIndexBufferSize = 128 * 1024;
	}

	ImGuiRenderer::~ImGuiRenderer()
	{
		Shutdown();
	}

	bool ImGuiRenderer::Initialize(nvrhi::IDevice* device, Window* window, const std::string& shaderDirectory)
	{
		AE_ASSERT(device != nullptr, "ImGuiRenderer requires a device");
		AE_ASSERT(window != nullptr, "ImGuiRenderer requires a window");
		m_Device = device;
		m_Window = window;

		if (!m_ImGuiInitialized)
		{
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.ConfigWindowsMoveFromTitleBarOnly = true;
			io.IniFilename = "Saved/Layout/imgui.ini";

			ImGui::StyleColorsDark();
			ImGuiStyle& style = ImGui::GetStyle();
			style.WindowRounding = 4.0f;
			style.FrameRounding = 3.0f;
			style.GrabRounding = 3.0f;
			style.TabRounding = 3.0f;
			style.FramePadding = ImVec2(6.0f, 3.0f);
			style.WindowBorderSize = 1.0f;

			if (!ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window->GetNativeWindow()), true))
			{
				AE_CORE_CRITICAL("Could not initialize ImGui's GLFW backend");
				return false;
			}
			m_ImGuiInitialized = true;
		}

		if (!loadShaders(shaderDirectory))
		{
			return false;
		}

		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.visibility = nvrhi::ShaderType::All;
		layoutDesc.registerSpace = 0;
		layoutDesc.registerSpaceIsDescriptorSet = true;
		// NVRHI offsets sampler slots by 128 and constant buffers by 256 by default; the
		// GLSL below uses plain 0-based bindings, so the offsets are pinned to zero.
		layoutDesc.bindingOffsets = nvrhi::VulkanBindingOffsets{ 0, 0, 0, 0 };
		layoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Sampler(1),
		};

		nvrhi::BindingLayoutItem pushConstants{};
		pushConstants.setType(nvrhi::ResourceType::PushConstants).setSlot(0).setSize(sizeof(PushConstants));
		layoutDesc.bindings.push_back(pushConstants);

		m_BindingLayout = m_Device->createBindingLayout(layoutDesc);
		if (m_BindingLayout == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the ImGui binding layout");
			return false;
		}

		if (!createFontTexture())
		{
			return false;
		}

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_FontTexture));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(1, m_FontSampler));
		m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
		if (m_BindingSet == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the ImGui binding set");
			return false;
		}

		ensureVertexBuffer(c_InitialVertexBufferSize);
		ensureIndexBuffer(c_InitialIndexBufferSize);

		nvrhi::VertexAttributeDesc attributes[3] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(0)
				.setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD0").setFormat(nvrhi::Format::RG32_FLOAT)
				.setOffset(offsetof(ImDrawVert, uv)).setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA8_UNORM)
				.setOffset(offsetof(ImDrawVert, col)).setElementStride(sizeof(ImDrawVert)),
		};
		attributes[0].setBufferIndex(0);
		attributes[1].setBufferIndex(0);
		attributes[2].setBufferIndex(0);
		m_InputLayout = m_Device->createInputLayout(attributes, 3, m_VertexShader);
		if (m_InputLayout == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the ImGui input layout");
			return false;
		}

		m_Initialized = true;
		AE_CORE_INFO("Dear ImGui {} renderer ready", IMGUI_VERSION);
		return true;
	}

	void ImGuiRenderer::Shutdown()
	{
		if (m_ImGuiInitialized)
		{
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			m_ImGuiInitialized = false;
		}
		m_Pipeline = nullptr;
		m_BindingSet = nullptr;
		m_BindingLayout = nullptr;
		m_FontTexture = nullptr;
		m_FontSampler = nullptr;
		m_VertexBuffer = nullptr;
		m_IndexBuffer = nullptr;
		m_InputLayout = nullptr;
		m_VertexShader = nullptr;
		m_FragmentShader = nullptr;
		m_Initialized = false;
	}

	bool ImGuiRenderer::loadShaders(const std::string& shaderDirectory)
	{
		const auto directory = FileSystem::FromUtf8(shaderDirectory);

		std::vector<u8> vertexBytecode;
		if (!FileSystem::TryReadBinaryFile(directory / "ImGui.vert.spv", vertexBytecode))
		{
			AE_CORE_CRITICAL("Could not read shaders/ImGui.vert.spv");
			return false;
		}

		std::vector<u8> fragmentBytecode;
		if (!FileSystem::TryReadBinaryFile(directory / "ImGui.frag.spv", fragmentBytecode))
		{
			AE_CORE_CRITICAL("Could not read shaders/ImGui.frag.spv");
			return false;
		}

		nvrhi::ShaderDesc vertexDesc;
		vertexDesc.setShaderType(nvrhi::ShaderType::Vertex);
		vertexDesc.debugName = "ImGui.vert";
		m_VertexShader = m_Device->createShader(vertexDesc, vertexBytecode.data(), vertexBytecode.size());
		if (m_VertexShader == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the ImGui vertex shader");
			return false;
		}

		nvrhi::ShaderDesc pixelDesc;
		pixelDesc.setShaderType(nvrhi::ShaderType::Pixel);
		pixelDesc.debugName = "ImGui.frag";
		m_FragmentShader = m_Device->createShader(pixelDesc, fragmentBytecode.data(), fragmentBytecode.size());
		if (m_FragmentShader == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the ImGui pixel shader");
			return false;
		}
		return true;
	}

	bool ImGuiRenderer::createFontTexture()
	{
		ImGuiIO& io = ImGui::GetIO();
		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		if (pixels == nullptr || width <= 0 || height <= 0)
		{
			AE_CORE_CRITICAL("Could not build the ImGui font atlas");
			return false;
		}

		nvrhi::TextureDesc desc;
		desc.width = static_cast<u32>(width);
		desc.height = static_cast<u32>(height);
		desc.format = nvrhi::Format::RGBA8_UNORM;
		desc.dimension = nvrhi::TextureDimension::Texture2D;
		desc.isShaderResource = true;
		desc.isRenderTarget = false;
		desc.debugName = "ImGui font atlas";
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		m_FontTexture = m_Device->createTexture(desc);
		if (m_FontTexture == nullptr)
		{
			AE_CORE_CRITICAL("Could not create the ImGui font texture");
			return false;
		}

		nvrhi::CommandListHandle upload = m_Device->createCommandList();
		upload->open();
		upload->writeTexture(m_FontTexture, 0, 0, pixels, static_cast<usize>(width) * 4);
		upload->close();
		nvrhi::ICommandList* uploadRaw = upload;
		m_Device->executeCommandLists(&uploadRaw, 1);
		m_Device->waitForIdle();

		nvrhi::SamplerDesc samplerDesc;
		samplerDesc.setMinFilter(true);
		samplerDesc.setMagFilter(true);
		samplerDesc.setMipFilter(false);
		samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
		samplerDesc.setMaxAnisotropy(1.0f);
		m_FontSampler = m_Device->createSampler(samplerDesc);

		m_FontTextureId = static_cast<ImTextureID>(1);
		RegisterTexture(m_FontTextureId, m_FontTexture);
		io.Fonts->SetTexID(m_FontTextureId);
		return m_FontSampler != nullptr;
	}

	void ImGuiRenderer::ensureVertexBuffer(usize requiredBytes)
	{
		if (m_VertexBuffer != nullptr && m_VertexBufferSize >= requiredBytes)
		{
			return;
		}
		const usize newSize = std::max(requiredBytes, c_InitialVertexBufferSize);
		nvrhi::BufferDesc desc;
		desc.byteSize = newSize;
		desc.isVertexBuffer = true;
		desc.initialState = nvrhi::ResourceStates::VertexBuffer;
		desc.keepInitialState = true;
		desc.debugName = "ImGui vertex buffer";
		m_VertexBuffer = m_Device->createBuffer(desc);
		m_VertexBufferSize = newSize;
	}

	void ImGuiRenderer::ensureIndexBuffer(usize requiredBytes)
	{
		if (m_IndexBuffer != nullptr && m_IndexBufferSize >= requiredBytes)
		{
			return;
		}
		const usize newSize = std::max(requiredBytes, c_InitialIndexBufferSize);
		nvrhi::BufferDesc desc;
		desc.byteSize = newSize;
		desc.isIndexBuffer = true;
		desc.initialState = nvrhi::ResourceStates::IndexBuffer;
		desc.keepInitialState = true;
		desc.debugName = "ImGui index buffer";
		m_IndexBuffer = m_Device->createBuffer(desc);
		m_IndexBufferSize = newSize;
	}

	void ImGuiRenderer::RegisterTexture(ImTextureID id, nvrhi::ITexture* texture)
	{
		if (id == 0 || texture == nullptr || m_Device == nullptr)
		{
			return;
		}
		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, texture));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(1, m_FontSampler));
		nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
		if (bindingSet == nullptr)
		{
			AE_CORE_ERROR("Could not create an ImGui binding set for texture {}", static_cast<u64>(id));
			return;
		}
		m_TextureBindings[id] = std::move(bindingSet);
	}

	void ImGuiRenderer::UnregisterTexture(ImTextureID id)
	{
		if (id != 0 && id != m_FontTextureId)
		{
			m_TextureBindings.erase(id);
		}
	}

	void ImGuiRenderer::NewFrame()
	{
		if (!m_Initialized)
		{
			return;
		}
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		++m_FrameIndex;
	}

	bool ImGuiRenderer::createPipeline(nvrhi::IFramebuffer* framebuffer)
	{
		const nvrhi::FramebufferInfo framebufferInfo(framebuffer->getDesc());
		if (m_Pipeline != nullptr && m_PipelineFramebufferInfo == framebufferInfo)
		{
			return true;
		}

		nvrhi::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.setPrimType(nvrhi::PrimitiveType::TriangleList);
		pipelineDesc.setVertexShader(m_VertexShader);
		pipelineDesc.setPixelShader(m_FragmentShader);
		pipelineDesc.setInputLayout(m_InputLayout);
		pipelineDesc.bindingLayouts = { m_BindingLayout };

		nvrhi::RenderState& renderState = pipelineDesc.renderState;
		renderState.depthStencilState.depthTestEnable = false;
		renderState.depthStencilState.depthWriteEnable = false;
		renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		renderState.rasterState.scissorEnable = true;
		renderState.rasterState.fillMode = nvrhi::RasterFillMode::Solid;
		renderState.blendState.targets[0].blendEnable = true;
		renderState.blendState.targets[0].srcBlend = nvrhi::BlendFactor::SrcAlpha;
		renderState.blendState.targets[0].destBlend = nvrhi::BlendFactor::InvSrcAlpha;
		renderState.blendState.targets[0].blendOp = nvrhi::BlendOp::Add;
		renderState.blendState.targets[0].srcBlendAlpha = nvrhi::BlendFactor::One;
		renderState.blendState.targets[0].destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
		renderState.blendState.targets[0].blendOpAlpha = nvrhi::BlendOp::Add;

		m_Pipeline = m_Device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
		if (m_Pipeline == nullptr)
		{
			AE_CORE_ERROR("Could not create the ImGui graphics pipeline");
			return false;
		}
		m_PipelineFramebufferInfo = framebufferInfo;
		return true;
	}

	void ImGuiRenderer::Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, f32 width, f32 height)
	{
		if (!m_Initialized || commandList == nullptr || framebuffer == nullptr)
		{
			return;
		}

		ImDrawData* drawData = ImGui::GetDrawData();
		if (drawData == nullptr || drawData->TotalVtxCount == 0)
		{
			return;
		}
		if (!createPipeline(framebuffer))
		{
			return;
		}

		const usize vertexBytes = static_cast<usize>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
		const usize indexBytes = static_cast<usize>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
		ensureVertexBuffer(vertexBytes);
		ensureIndexBuffer(indexBytes);
		if (m_VertexBuffer == nullptr || m_IndexBuffer == nullptr)
		{
			AE_CORE_ERROR("ImGui buffers are unavailable; skipping the UI draw");
			return;
		}

		// Pack the draw data into one contiguous vertex/index buffer and remember the
		// offsets of each ImGui draw list.
		std::vector<u8> vertexData(vertexBytes);
		std::vector<u8> indexData(indexBytes);
		usize vertexOffsetBytes = 0;
		usize indexOffset = 0;
		for (ImDrawList* drawList : drawData->CmdLists)
		{
			std::memcpy(vertexData.data() + vertexOffsetBytes, drawList->VtxBuffer.Data,
				static_cast<usize>(drawList->VtxBuffer.Size) * sizeof(ImDrawVert));
			std::memcpy(indexData.data() + indexOffset * sizeof(ImDrawIdx), drawList->IdxBuffer.Data,
				static_cast<usize>(drawList->IdxBuffer.Size) * sizeof(ImDrawIdx));
			vertexOffsetBytes += static_cast<usize>(drawList->VtxBuffer.Size) * sizeof(ImDrawVert);
			indexOffset += static_cast<usize>(drawList->IdxBuffer.Size);
		}

		commandList->writeBuffer(m_VertexBuffer, vertexData.data(), vertexData.size());
		commandList->writeBuffer(m_IndexBuffer, indexData.data(), indexData.size());

		// NVRHI's Vulkan backend presents a DX-style viewport (y up in NDC), so the
		// pixel-to-clip mapping flips y and anchors the top-left pixel at +1.
		const PushConstants pushConstants = {
			{ 2.0f / drawData->DisplaySize.x, -2.0f / drawData->DisplaySize.y },
			{ -1.0f - drawData->DisplayPos.x * 2.0f / drawData->DisplaySize.x,
				1.0f + drawData->DisplayPos.y * 2.0f / drawData->DisplaySize.y },
		};

		usize vertexOffsetElements = 0;
		usize indexOffsetElements = 0;

		for (ImDrawList* drawList : drawData->CmdLists)
		{
			for (const ImDrawCmd& drawCommand : drawList->CmdBuffer)
			{
				if (drawCommand.ElemCount == 0)
				{
					continue;
				}
				if (drawCommand.UserCallback != nullptr)
				{
					if (drawCommand.UserCallback != ImDrawCallback_ResetRenderState)
					{
						drawCommand.UserCallback(drawList, &drawCommand);
					}
					continue;
				}

				const f32 clipX = drawCommand.ClipRect.x - drawData->DisplayPos.x;
				const f32 clipY = drawCommand.ClipRect.y - drawData->DisplayPos.y;
				const f32 clipRight = drawCommand.ClipRect.z - drawData->DisplayPos.x;
				const f32 clipBottom = drawCommand.ClipRect.w - drawData->DisplayPos.y;
				if (clipRight <= clipX || clipBottom <= clipY)
				{
					continue;
				}

				auto textureIt = m_TextureBindings.find(drawCommand.GetTexID());
				if (textureIt == m_TextureBindings.end())
				{
					textureIt = m_TextureBindings.find(m_FontTextureId);
				}

				nvrhi::GraphicsState state;
				state.pipeline = m_Pipeline;
				state.framebuffer = framebuffer;
				state.bindings = { textureIt != m_TextureBindings.end() ? textureIt->second.Get() : m_BindingSet.Get() };
				state.setViewport(nvrhi::ViewportState()
					.addViewport(nvrhi::Viewport(0.0f, width, 0.0f, height, 0.0f, 1.0f))
					.addScissorRect(nvrhi::Rect(static_cast<int>(clipX), static_cast<int>(clipRight),
						static_cast<int>(clipY), static_cast<int>(clipBottom))));

				nvrhi::VertexBufferBinding vertexBinding;
				vertexBinding.buffer = m_VertexBuffer;
				vertexBinding.slot = 0;
				vertexBinding.offset = (vertexOffsetElements + drawCommand.VtxOffset) * sizeof(ImDrawVert);
				state.addVertexBuffer(vertexBinding);

				nvrhi::IndexBufferBinding indexBinding;
				indexBinding.buffer = m_IndexBuffer;
				indexBinding.format = nvrhi::Format::R16_UINT;
				indexBinding.offset = (indexOffsetElements + drawCommand.IdxOffset) * sizeof(ImDrawIdx);
				state.setIndexBuffer(indexBinding);

				commandList->setGraphicsState(state);
				commandList->setPushConstants(&pushConstants, sizeof(pushConstants));
				commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(drawCommand.ElemCount));
			}
			vertexOffsetElements += static_cast<usize>(drawList->VtxBuffer.Size);
			indexOffsetElements += static_cast<usize>(drawList->IdxBuffer.Size);
		}
	}
}
