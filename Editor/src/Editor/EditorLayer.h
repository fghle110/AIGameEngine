#pragma once

#include "Aegis/Core/Layer.h"
#include "Aegis/Core/Uuid.h"
#include "Aegis/Scene/Scene.h"
#include "Editor/EditorCamera.h"

#include <glm/glm.hpp>

#include <string>

struct ImVec2;

namespace aegis
{
	class Renderer;
	class ImGuiRenderer;
	class Entity;

	// Command line driven automation used by the AI tooling and the self-test:
	// render a number of frames, optionally capture a screenshot, then quit.
	struct EditorAutomationOptions
	{
		i32 Frames = 0;
		std::string ScreenshotPath;
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

		Scope<Scene> m_Scene;
		Scope<Renderer> m_Renderer;
		Scope<ImGuiRenderer> m_ImGui;

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
