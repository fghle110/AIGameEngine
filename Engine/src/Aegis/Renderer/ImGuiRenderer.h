#pragma once

#include "Aegis/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <imgui.h>

#include <string>
#include <unordered_map>

namespace aegis
{
	class Window;

	// Dear ImGui renderer built directly on NVRHI (no Vulkan backend needed).
	// ImGui_ImplGlfw provides input/clipboard; this class owns the pipeline,
	// font atlas and per-frame vertex/index buffers.
	class ImGuiRenderer
	{
	public:
		ImGuiRenderer() = default;
		~ImGuiRenderer();

		ImGuiRenderer(const ImGuiRenderer&) = delete;
		ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

		bool Initialize(nvrhi::IDevice* device, Window* window, const std::string& shaderDirectory);
		void Shutdown();

		bool IsInitialized() const { return m_Initialized; }

		// Starts a new ImGui frame (platform new frame + ImGui::NewFrame).
		void NewFrame();

		// Records the ImGui draw data into the given command list and framebuffer.
		void Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, f32 width, f32 height);

		// Registers a texture that ImGui can reference by id (viewport targets,
		// icons, gizmo overlays). Ids must be non-zero and unique.
		void RegisterTexture(ImTextureID id, nvrhi::ITexture* texture);
		void UnregisterTexture(ImTextureID id);
		ImTextureID GetFontTextureId() const { return m_FontTextureId; }

	private:
		struct PushConstants
		{
			f32 Scale[2];
			f32 Translate[2];
		};

		bool loadShaders(const std::string& shaderDirectory);
		bool createPipeline(nvrhi::IFramebuffer* framebuffer);
		bool createFontTexture();
		void ensureVertexBuffer(usize requiredBytes);
		void ensureIndexBuffer(usize requiredBytes);

		nvrhi::IDevice* m_Device = nullptr;
		Window* m_Window = nullptr;

		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_FragmentShader;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;
		nvrhi::SamplerHandle m_FontSampler;
		nvrhi::TextureHandle m_FontTexture;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::FramebufferInfo m_PipelineFramebufferInfo;

		ImTextureID m_FontTextureId = 0;
		std::unordered_map<ImTextureID, nvrhi::BindingSetHandle> m_TextureBindings;

		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;
		usize m_VertexBufferSize = 0;
		usize m_IndexBufferSize = 0;
		u32 m_FrameIndex = 0;

		bool m_Initialized = false;
		bool m_ImGuiInitialized = false;
	};
}
