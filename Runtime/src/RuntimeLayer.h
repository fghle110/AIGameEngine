#pragma once

#include "Aegis/Audio/AudioEngine.h"
#include "Aegis/Core/Layer.h"
#include "Aegis/Physics/PhysicsWorld.h"
#include "Aegis/Project/Project.h"
#include "Aegis/Renderer/SceneRenderer.h"
#include "Aegis/Scripting/LuaEngine.h"
#include "ScreenBlit.h"

#include <filesystem>
#include <string>

namespace aegis
{
	class Renderer;
	class Scene;

	// Headless-style verification for the exported player: render N frames,
	// optionally capture the last one to PNG, then quit.
	struct RuntimeAutomationOptions
	{
		i32 Frames = 0;
		std::string ScreenshotPath;
	};

	// The exported game player: loads a project, builds the world, runs physics,
	// scripting and audio, and presents the rendered frame on the swapchain.
	class RuntimeLayer final : public Layer
	{
	public:
		explicit RuntimeLayer(std::filesystem::path projectFile, RuntimeAutomationOptions automation = {});
		~RuntimeLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep timestep) override;
		void OnEvent(Event& event) override;

	private:
		bool loadProject();
		void renderFrame(Timestep timestep);

		std::filesystem::path m_ProjectFile;
		Project m_Project;
		Scope<Scene> m_Scene;
		Scope<Renderer> m_Renderer;
		Scope<SceneRenderer> m_SceneRenderer;
		Scope<ScreenBlit> m_ScreenBlit;

		PhysicsWorld m_Physics;
		AudioEngine m_Audio;
		LuaEngine m_Scripting;

		RuntimeAutomationOptions m_Automation;
		i32 m_RemainingFrames = 0;
		bool m_Ready = false;
	};
}
