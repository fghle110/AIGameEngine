#pragma once

#include "Aegis/Core/Base.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <string>
#include <vector>

namespace aegis
{
	class Window;
	class VulkanRenderContext;
	class VulkanSwapchain;

	struct RendererSpecification
	{
		Window* Window = nullptr;
		bool VSync = true;
		bool EnableValidation = true;
		bool EnableDebugNames = true;
	};

	// Owns the graphics device and the swapchain and drives the frame lifecycle:
	// BeginFrame opens a command list and clears the targets, engine systems record
	// their passes through GetCommandList(), EndFrame submits and presents.
	class Renderer final
	{
	public:
		explicit Renderer(const RendererSpecification& specification);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool IsInitialized() const { return m_Initialized; }

		bool BeginFrame(const glm::vec4& clearColor);
		void EndFrame();

		// Called from the window resize event; the swapchain is recreated lazily at
		// the next BeginFrame so no frame is recorded against a stale swapchain.
		void Resize(u32 width, u32 height);
		void SetVSync(bool enabled);

		// Captures the next finished frame to a PNG file (used by the AI command API
		// and by the editor self-test). The file is written during EndFrame.
		void RequestScreenshot(std::string filePath);
		bool HasPendingScreenshot() const { return !m_ScreenshotPath.empty(); }
		bool IsVSync() const { return m_VSync; }

		nvrhi::IDevice* GetDevice() const;
		nvrhi::ICommandList* GetCommandList() const;
		nvrhi::IFramebuffer* GetFramebuffer() const;
		VulkanRenderContext* GetContext() const;

		u32 GetWidth() const { return m_Width; }
		u32 GetHeight() const { return m_Height; }
		f32 GetAspectRatio() const;
		const std::string& GetGpuName() const;

	private:
		void createDepthTexture();
		void createFramebuffers();
		void destroyFramebuffers();
		bool recreateSwapchainIfNeeded();

		RendererSpecification m_Specification;

		Scope<VulkanRenderContext> m_Context;
		Scope<VulkanSwapchain> m_Swapchain;

		nvrhi::TextureHandle m_DepthTexture;
		std::vector<nvrhi::FramebufferHandle> m_Framebuffers;
		nvrhi::CommandListHandle m_CommandList;

		u32 m_Width = 0;
		u32 m_Height = 0;
		u32 m_ImageIndex = 0;
		bool m_VSync = true;
		bool m_FrameActive = false;
		bool m_Initialized = false;
		std::string m_GpuName;
		std::string m_ScreenshotPath;
		nvrhi::ITexture* m_CurrentColorTarget = nullptr;

		bool captureScreenshot(nvrhi::ITexture* colorTarget, const std::string& filePath);
	};
}
