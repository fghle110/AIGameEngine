#include "ScreenBlit.h"

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Log.h"

#include <vector>

namespace aegis
{
	ScreenBlit::~ScreenBlit()
	{
		Shutdown();
	}

	bool ScreenBlit::Initialize(nvrhi::IDevice* device, const std::string& shaderDirectory)
	{
		AE_ASSERT(device != nullptr, "ScreenBlit requires a device");
		m_Device = device;
		const auto directory = FileSystem::FromUtf8(shaderDirectory);

		std::vector<u8> vertexBytecode;
		std::vector<u8> fragmentBytecode;
		if (!FileSystem::TryReadBinaryFile(directory / "ScenePost.vert.spv", vertexBytecode) ||
			!FileSystem::TryReadBinaryFile(directory / "Blit.frag.spv", fragmentBytecode))
		{
			AE_CORE_ERROR("Could not read the blit shaders from '{}'", shaderDirectory);
			return false;
		}

		nvrhi::ShaderDesc vertexDesc;
		vertexDesc.setShaderType(nvrhi::ShaderType::Vertex);
		vertexDesc.debugName = "ScenePost.vert";
		m_VertexShader = m_Device->createShader(vertexDesc, vertexBytecode.data(), vertexBytecode.size());

		nvrhi::ShaderDesc fragmentDesc;
		fragmentDesc.setShaderType(nvrhi::ShaderType::Pixel);
		fragmentDesc.debugName = "Blit.frag";
		m_PixelShader = m_Device->createShader(fragmentDesc, fragmentBytecode.data(), fragmentBytecode.size());

		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.visibility = nvrhi::ShaderType::Pixel;
		layoutDesc.registerSpace = 0;
		layoutDesc.registerSpaceIsDescriptorSet = true;
		layoutDesc.bindings = { nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(1) };
		m_BindingLayout = m_Device->createBindingLayout(layoutDesc);

		nvrhi::SamplerDesc samplerDesc;
		samplerDesc.setMinFilter(true).setMagFilter(true).setMipFilter(false);
		samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
		m_Sampler = m_Device->createSampler(samplerDesc);

		if (m_VertexShader == nullptr || m_PixelShader == nullptr || m_BindingLayout == nullptr ||
			m_Sampler == nullptr)
		{
			AE_CORE_ERROR("Could not create the screen blit resources");
			return false;
		}

		m_Initialized = true;
		return true;
	}

	void ScreenBlit::Shutdown()
	{
		m_Pipeline = nullptr;
		m_BindingSet = nullptr;
		m_BindingLayout = nullptr;
		m_Sampler = nullptr;
		m_VertexShader = nullptr;
		m_PixelShader = nullptr;
		m_BoundSource = nullptr;
		m_Initialized = false;
	}

	bool ScreenBlit::createPipeline(nvrhi::IFramebuffer* target)
	{
		const nvrhi::FramebufferInfo framebufferInfo(target->getDesc());
		if (m_Pipeline != nullptr && m_PipelineFramebufferInfo == framebufferInfo)
		{
			return true;
		}

		nvrhi::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.setPrimType(nvrhi::PrimitiveType::TriangleList);
		pipelineDesc.setVertexShader(m_VertexShader);
		pipelineDesc.setPixelShader(m_PixelShader);
		pipelineDesc.bindingLayouts = { m_BindingLayout };
		pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
		pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
		pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;

		m_Pipeline = m_Device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
		if (m_Pipeline == nullptr)
		{
			AE_CORE_ERROR("Could not create the screen blit pipeline");
			return false;
		}
		m_PipelineFramebufferInfo = framebufferInfo;
		return true;
	}

	void ScreenBlit::ensureBindingSet(nvrhi::ITexture* source)
	{
		if (m_BindingSet != nullptr && m_BoundSource == source)
		{
			return;
		}
		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, source));
		bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(1, m_Sampler));
		m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_BoundSource = source;
	}

	void ScreenBlit::Draw(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, nvrhi::ITexture* source)
	{
		if (!m_Initialized || commandList == nullptr || target == nullptr || source == nullptr)
		{
			return;
		}
		if (!createPipeline(target))
		{
			return;
		}
		ensureBindingSet(source);
		if (m_BindingSet == nullptr)
		{
			return;
		}

		const nvrhi::FramebufferDesc& desc = target->getDesc();
		const f32 width = static_cast<f32>(desc.colorAttachments[0].texture->getDesc().width);
		const f32 height = static_cast<f32>(desc.colorAttachments[0].texture->getDesc().height);

		nvrhi::GraphicsState state;
		state.pipeline = m_Pipeline;
		state.framebuffer = target;
		state.bindings = { m_BindingSet };
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(0.0f, width, 0.0f, height, 0.0f, 1.0f));
		commandList->setGraphicsState(state);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
	}
}
