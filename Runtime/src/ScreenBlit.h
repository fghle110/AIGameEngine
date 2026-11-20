#pragma once

#include "Aegis/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <string>

namespace aegis
{
	// Fullscreen copy of a texture into the current framebuffer, used by the
	// runtime to present the scene renderer output on the swapchain.
	class ScreenBlit final
	{
	public:
		ScreenBlit() = default;
		~ScreenBlit();

		bool Initialize(nvrhi::IDevice* device, const std::string& shaderDirectory);
		void Shutdown();

		void Draw(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, nvrhi::ITexture* source);

	private:
		bool createPipeline(nvrhi::IFramebuffer* target);
		void ensureBindingSet(nvrhi::ITexture* source);

		nvrhi::IDevice* m_Device = nullptr;
		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;
		nvrhi::SamplerHandle m_Sampler;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::FramebufferInfo m_PipelineFramebufferInfo;
		nvrhi::ITexture* m_BoundSource = nullptr;
		bool m_Initialized = false;
	};
}
