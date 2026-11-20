#pragma once

#include "Aegis/Assets/AssetHandle.h"
#include "Aegis/Assets/EnvironmentMap.h"
#include "Aegis/Core/Base.h"
#include "Aegis/Renderer/AssetCache.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <array>
#include <string>
#include <unordered_set>
#include <vector>

namespace aegis
{
	class Scene;

	// The single shadow map shared by the directional light, and the number of point
	// and spot lights the forward shader can evaluate at once.
	inline constexpr u32 c_ShadowMapResolution = 2048;
	inline constexpr u32 c_MaxSceneLights = 8;

	// std140 mirrors of the blocks in Engine/Shaders/Scene*Constants.glsl. Only
	// mat4/vec4/uvec4 members, so the C++ layout matches the GLSL layout exactly.
	struct SceneLightConstants
	{
		glm::vec4 PositionRange{ 0.0f };
		glm::vec4 ColorIntensity{ 0.0f };
		glm::vec4 DirectionType{ 0.0f };
		glm::vec4 SpotAngles{ 0.0f };
	};

	struct SceneObjectConstants
	{
		glm::mat4 Model{ 1.0f };
		glm::vec4 BaseColorFactor{ 1.0f };
		glm::vec4 MetallicRoughnessNormalOcclusion{ 0.0f };
		glm::vec4 Emissive{ 0.0f };
	};

	struct SceneFrameConstants
	{
		glm::mat4 ViewProjection{ 1.0f };
		glm::mat4 InvViewProjection{ 1.0f };
		glm::mat4 LightViewProjection{ 1.0f };
		glm::vec4 CameraPosition{ 0.0f };
		glm::vec4 Ambient{ 0.0f };
		glm::vec4 SunDirection{ 0.0f };
		glm::vec4 SunColor{ 0.0f };
		glm::vec4 PostProcess{ 1.0f };
		// x = image-based lighting active, y = specular mip count, z = HDRI intensity, w = unused
		glm::vec4 Environment{ 0.0f };
		glm::uvec4 LightCounts{ 0u };
		std::array<SceneLightConstants, c_MaxSceneLights> Lights{};
	};

	// Push constant block of the tonemap pass.
	struct ScenePostConstants
	{
		glm::vec4 PostProcess{ 1.0f };
	};

	// Push constant block of the SSAO pass: x = radius, y = bias, z = strength, w = unused.
	struct SceneSsaoConstants
	{
		glm::vec4 Params{ 0.6f, 0.025f, 1.0f, 0.0f };
	};

	// Push constant block of the SSAO blur pass: xy = source texel size.
	struct SceneBlurConstants
	{
		glm::vec4 TexelSize{ 0.0f };
	};

	static_assert(sizeof(SceneLightConstants) == 64, "SceneLightConstants must match the std140 layout");
	static_assert(sizeof(SceneObjectConstants) == 112, "SceneObjectConstants must match the std140 layout");
	static_assert(sizeof(SceneFrameConstants) == 816, "SceneFrameConstants must match the std140 layout");
	static_assert(sizeof(ScenePostConstants) == 16, "ScenePostConstants must match the std140 layout");
	static_assert(sizeof(SceneSsaoConstants) == 16, "SceneSsaoConstants must match the std140 layout");
	static_assert(sizeof(SceneBlurConstants) == 16, "SceneBlurConstants must match the std140 layout");

	struct SceneRendererStats
	{
		u32 VisibleMeshes = 0;
		u32 DrawCalls = 0;
		u32 ShadowDrawCalls = 0;
		u32 PointLights = 0;
		u32 Triangles = 0;
	};

	// Offscreen HDR scene renderer: a shadow map pass, a forward Cook-Torrance PBR
	// pass into an RGBA16F target, and a post pass that applies exposure and ACES
	// tonemapping into an sRGB texture an ImGui viewport can display.
	//
	// Render() records every pass into the command list it is given; the caller owns
	// submission so the editor can draw its UI into the same submission.
	class SceneRenderer final
	{
	public:
		SceneRenderer() = default;
		~SceneRenderer();

		SceneRenderer(const SceneRenderer&) = delete;
		SceneRenderer& operator=(const SceneRenderer&) = delete;

		bool Initialize(nvrhi::IDevice* device);
		void Shutdown();
		bool IsInitialized() const { return m_Initialized; }

		// Recreates the render targets; pipelines are sized independently and survive.
		void Resize(u32 width, u32 height);

		void Render(nvrhi::ICommandList* commandList, Scene& scene, const glm::mat4& viewProjection,
			const glm::vec3& cameraPosition, const glm::mat4& lightViewProjection, bool renderShadows);

		nvrhi::ITexture* GetFinalColor() const { return m_FinalColor.Get(); }
		u32 GetWidth() const { return m_Width; }
		u32 GetHeight() const { return m_Height; }

		AssetCache& GetAssetCache() { return m_AssetCache; }
		const AssetCache& GetAssetCache() const { return m_AssetCache; }

		// Image-based lighting state for the current environment.
		bool IsEnvironmentActive() const { return m_EnvironmentActive; }
		bool IsSsaoActive() const { return m_SsaoActive; }
		u32 GetEnvironmentMipCount() const { return m_EnvironmentMap.SpecularMipCount; }
		const EnvironmentMap& GetEnvironmentMap() const { return m_EnvironmentMap; }
		const SceneRendererStats& GetStats() const { return m_Stats; }

		// Builds the light-space matrix for the scene's directional light, fitted to a
		// box of shadowDistance around the camera. Scenes without a sun fall back to a
		// fixed direction so Render() still has a usable matrix.
		static glm::mat4 ComputeDirectionalShadowMatrix(const Scene& scene, const glm::vec3& cameraPosition,
			f32 shadowDistance);

	private:
		struct DrawItem
		{
			const AssetCache::CachedMesh* Mesh = nullptr;
			const Material* Material = nullptr;
			glm::mat4 Model{ 1.0f };
			u32 SubmeshIndex = 0;
			bool CastShadows = true;
			bool ReceiveShadows = true;
		};

		bool createBindingLayouts();
		bool loadShaders();
		bool createPipelines();
		bool createShadowResources();
		bool createFrameResources();
		void releaseFrameResources();

		bool createEnvironmentResources();
		bool createSsaoResources();
		void createSsaoTargets();
		void recordDepthPrepass(nvrhi::ICommandList* commandList);
		void recordSsaoPass(nvrhi::ICommandList* commandList);
		void recordSsaoBlur(nvrhi::ICommandList* commandList);
		nvrhi::TextureHandle createCubeTexture(u32 size, u32 mips, const std::string& debugName);
		void uploadEnvironment(const EnvironmentMap& environment, nvrhi::ICommandList* commandList);
		void rebuildFrameBindingSet();
		void updateEnvironment(Scene& scene, nvrhi::ICommandList* commandList);

		void gatherDrawItems(Scene& scene, const glm::vec3& cameraPosition);
		void fillFrameConstants(Scene& scene, const glm::mat4& viewProjection, const glm::vec3& cameraPosition,
			const glm::mat4& lightViewProjection, bool renderShadows);
		void recordShadowPass(nvrhi::ICommandList* commandList);
		void recordSkyPass(nvrhi::ICommandList* commandList);
		void recordForwardPass(nvrhi::ICommandList* commandList);
		void recordPostPass(nvrhi::ICommandList* commandList);

		nvrhi::IDevice* m_Device = nullptr;
		AssetCache m_AssetCache;

		nvrhi::BindingLayoutHandle m_FrameBindingLayout;
		nvrhi::BindingLayoutHandle m_MaterialBindingLayout;
		nvrhi::BindingLayoutHandle m_DepthBindingLayout;
		nvrhi::BindingLayoutHandle m_PostBindingLayout;
		nvrhi::BindingLayoutHandle m_SsaoBindingLayout;
		nvrhi::BindingLayoutHandle m_BlurBindingLayout;

		nvrhi::ShaderHandle m_PbrVertexShader;
		nvrhi::ShaderHandle m_PbrPixelShader;
		nvrhi::ShaderHandle m_DepthVertexShader;
		nvrhi::ShaderHandle m_FullscreenVertexShader;
		nvrhi::ShaderHandle m_TonemapPixelShader;
		nvrhi::ShaderHandle m_SsaoPixelShader;
		nvrhi::ShaderHandle m_SsaoBlurPixelShader;
		nvrhi::ShaderHandle m_SkyPixelShader;

		nvrhi::InputLayoutHandle m_SceneInputLayout;
		nvrhi::InputLayoutHandle m_DepthInputLayout;

		nvrhi::GraphicsPipelineHandle m_ShadowPipeline;
		nvrhi::GraphicsPipelineHandle m_PbrPipeline;
		nvrhi::GraphicsPipelineHandle m_TonemapPipeline;
		nvrhi::GraphicsPipelineHandle m_PrepassPipeline;
		nvrhi::GraphicsPipelineHandle m_SsaoPipeline;
		nvrhi::GraphicsPipelineHandle m_SsaoBlurPipeline;
		nvrhi::GraphicsPipelineHandle m_SkyPipeline;

		nvrhi::SamplerHandle m_ShadowSampler;
		nvrhi::SamplerHandle m_AoSampler;
		nvrhi::SamplerHandle m_EnvironmentSampler;
		nvrhi::SamplerHandle m_HdrSampler;

		// Image-based lighting: precomputed cubes + BRDF LUT, swapped when the scene
		// environment changes. Fallback textures keep the bindings valid when IBL is off.
		EnvironmentMap m_EnvironmentMap;
		std::string m_EnvironmentSource;
		nvrhi::TextureHandle m_IrradianceCube;
		nvrhi::TextureHandle m_SpecularCube;
		nvrhi::TextureHandle m_BrdfLut;
		bool m_EnvironmentActive = false;
		nvrhi::BufferHandle m_FrameConstantBuffer;
		nvrhi::BufferHandle m_DepthConstantBuffer;
		nvrhi::BufferHandle m_CameraDepthConstantBuffer;
		nvrhi::BindingSetHandle m_FrameBindingSet;
		nvrhi::BindingSetHandle m_DepthBindingSet;
		nvrhi::BindingSetHandle m_CameraDepthBindingSet;
		nvrhi::BindingSetHandle m_SsaoBindingSet;
		nvrhi::BindingSetHandle m_BlurBindingSet;
		nvrhi::BindingSetHandle m_PostBindingSet;

		nvrhi::TextureHandle m_HdrColor;
		nvrhi::TextureHandle m_DepthTexture;
		nvrhi::TextureHandle m_AoTexture;
		nvrhi::TextureHandle m_AoBlurTexture;
		nvrhi::TextureHandle m_AoFallback;
		nvrhi::TextureHandle m_FinalColor;
		nvrhi::TextureHandle m_ShadowMap;

		nvrhi::FramebufferHandle m_SceneFramebuffer;
		nvrhi::FramebufferHandle m_ShadowFramebuffer;
		nvrhi::FramebufferHandle m_PostFramebuffer;
		nvrhi::FramebufferHandle m_PrepassFramebuffer;
		nvrhi::FramebufferHandle m_AoFramebuffer;
		nvrhi::FramebufferHandle m_AoBlurFramebuffer;

		std::vector<DrawItem> m_DrawItems;
		// Entity names already reported for a missing mesh, so the warning appears once.
		std::unordered_set<std::string> m_MissingMeshWarnings;
		SceneFrameConstants m_FrameConstants;
		SceneSsaoConstants m_SsaoConstants;
		SceneRendererStats m_Stats;

		u32 m_Width = 1;
		u32 m_Height = 1;
		bool m_SsaoActive = false;
		bool m_Initialized = false;
	};
}
