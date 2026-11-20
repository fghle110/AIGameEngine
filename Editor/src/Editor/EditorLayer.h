#pragma once

#include "Aegis/Assets/AssetHandle.h"
#include "Aegis/Core/Layer.h"
#include "Aegis/Core/Uuid.h"
#include "Aegis/Scene/Scene.h"
#include "Aegis/Commands/HttpServer.h"
#include "Editor/EditorCamera.h"
#include "Editor/EditorCommands.h"

#include <glm/glm.hpp>

#include <string>

struct ImVec2;

namespace nvrhi
{
	class ICommandList;
}

namespace aegis
{
	class Renderer;
	class ImGuiRenderer;
	class SceneRenderer;
	class Entity;
	class PhysicsWorld;
	class AudioEngine;
	class LuaEngine;
	enum class PrimitiveType : u8;
	struct MaterialFactors;
	struct MaterialTextures;

	// Command line driven automation used by the AI tooling and the self-test:
	// render a number of frames, optionally capture a screenshot, then quit.
	struct EditorAutomationOptions
	{
		i32 Frames = 0;
		std::string ScreenshotPath;
		// Builds the primitive demo scene (plane, cubes, sphere, cylinder, a sun and a
		// point light) instead of the empty default scene, so a screenshot proves the
		// PBR, shadow and tonemap path without any external asset.
		bool DemoScene = false;
	};

	// The editor shell: dockspace, panels, menus and the frame that renders the
	// editor UI. All editor capabilities live behind the command bus; the panels
	// call the same commands the AI API does (wired up as they land).
	class EditorLayer final : public Layer
	{
	public:
		explicit EditorLayer(const EditorAutomationOptions& automation = {});
		~EditorLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep timestep) override;
		void OnImGuiRender() override;
		void OnEvent(Event& event) override;

		Scene& GetScene() { return *m_Scene; }
		Entity GetSelectedEntity();

	private:
		void createDefaultScene();
		void createDemoScene();
		AssetHandle createMaterialAsset(const MaterialFactors& factors, const MaterialTextures& textures,
			const char* name);
		AssetHandle createBuiltinMeshHandle(PrimitiveType type) const;
		Entity createMeshEntity(const char* name, PrimitiveType primitive, const glm::vec3& position,
			const glm::vec3& scale, const AssetHandle& material, const glm::vec3& rotationDegrees = glm::vec3(0.0f));
		void renderScene(nvrhi::ICommandList* commandList);
		glm::mat4 computeViewProjection(f32 aspectRatio, glm::vec3& outCameraPosition) const;
		u64 viewportTextureId() const { return m_ViewportTextureId; }
		void drawMenuBar();
		void drawDockspace();
		void buildDefaultLayout(u32 dockspaceId);
		void drawHierarchy();
		void drawEntityNode(Entity entity);
		void drawInspector();
		void drawTransform(Entity entity);
		void drawCameraComponent(Entity entity);
		void drawLightComponent(Entity entity);
		void drawMeshRendererComponent(Entity entity);
		void drawPhysicsComponents(Entity entity);
		void drawAudioComponents(Entity entity);
		void drawConsole();
		void drawStats();
		void drawAssetBrowser();
		void drawViewport();
		void drawViewportGrid(const glm::mat4& viewProjection, const glm::vec2& origin, const glm::vec2& size);
		void drawViewportGizmos(const glm::mat4& viewProjection, const glm::vec2& origin, const glm::vec2& size);
		void drawAddComponentPopup(Entity entity);
		void handleShortcuts();
		void drawAutomationFooter();

		// Command API: the same handlers back the UI and the AI HTTP server.
		void startCommandServer();
		void stopCommandServer();
		bool saveSceneToFile(const std::string& path, std::string* error);
		bool loadSceneFromFile(const std::string& path, std::string* error);
		void rebindSceneSystems();

		Scope<Scene> m_Scene;
		Scope<Renderer> m_Renderer;
		Scope<SceneRenderer> m_SceneRenderer;
		Scope<ImGuiRenderer> m_ImGui;

		Scope<PhysicsWorld> m_Physics;
		Scope<AudioEngine> m_Audio;
		Scope<LuaEngine> m_Scripting;

		HttpServer m_CommandServer;
		EditorCommandContext m_CommandContext;
		Scene* m_BoundScene = nullptr;
		bool m_CommandServerStarted = false;

		// The viewport image always uses texture id 2 (id 1 is ImGui's font atlas) and
		// is re-registered when the scene renderer recreates its output texture.
		u64 m_ViewportTextureId = 2;
		u32 m_RegisteredViewportWidth = 0;
		u32 m_RegisteredViewportHeight = 0;
		bool m_UseSceneCamera = false;

		EditorCamera m_EditorCamera;
		EditorAutomationOptions m_Automation;
		i32 m_RemainingFrames = 0;

		Uuid m_SelectedEntity = Uuid::Null();
		std::string m_ScenePath;
		std::string m_AssetDirectory;
		std::string m_ConsoleFilter;

		glm::vec2 m_ViewportSize{ 1280.0f, 720.0f };
		bool m_ViewportHovered = false;
		bool m_ViewportFocused = false;

		bool m_ShowStats = true;
		bool m_ShowConsole = true;
		bool m_ShowAssetBrowser = true;
		bool m_ShowImGuiDemo = false;
		bool m_ShowAbout = false;
		bool m_ResetLayoutRequested = false;
		bool m_ShowIcons = true;

		// Per-panel filter state.
		bool m_ConsoleShowTrace = false;
		bool m_ConsoleShowDebug = true;
		bool m_ConsoleShowInfo = true;
		bool m_ConsoleShowWarn = true;
		bool m_ConsoleShowError = true;
		bool m_ConsoleAutoScroll = true;
	};
}
