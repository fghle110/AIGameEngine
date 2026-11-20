#include "RuntimeLayer.h"

#include "Aegis/Core/Application.h"
#include "Aegis/Core/Config.h"
#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Input.h"
#include "Aegis/Core/KeyCodes.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Events/KeyEvent.h"
#include "Aegis/Renderer/Renderer.h"
#include "Aegis/Scene/Camera.h"
#include "Aegis/Scene/Scene.h"
#include "Aegis/Serialization/SceneSerializer.h"

#include <glm/glm.hpp>

namespace aegis
{
	RuntimeLayer::RuntimeLayer(std::filesystem::path projectFile, RuntimeAutomationOptions automation)
		: Layer("Runtime"),
		  m_ProjectFile(std::move(projectFile)),
		  m_Automation(std::move(automation)),
		  m_RemainingFrames(automation.Frames)
	{
	}

	RuntimeLayer::~RuntimeLayer() = default;

	bool RuntimeLayer::loadProject()
	{
		std::string error;
		if (!Project::Load(m_ProjectFile, m_Project, &error))
		{
			AE_CORE_CRITICAL("Could not load the project: {}", error);
			return false;
		}

		m_Scene = CreateScope<Scene>();
		if (!SceneSerializer::Load(*m_Scene, m_Project.GetStartScenePath(), &error))
		{
			AE_CORE_CRITICAL("Could not load the start scene: {}", error);
			return false;
		}

		m_Physics.Initialize();
		m_Audio.Initialize();
		m_Scripting.SetScriptRoot(m_Project.RootDirectory / "Scripts");
		m_Scripting.SetAudioEngine(&m_Audio);
		m_Scripting.SetPhysicsWorld(&m_Physics);
		m_Scripting.Initialize(*m_Scene);

		m_Scene->AddSystem(&m_Physics);
		m_Scene->AddSystem(&m_Scripting);
		m_Scene->AddSystem(&m_Audio);

		AE_CORE_INFO("Runtime loaded project '{}' ({}) with {} entities", m_Project.Name,
			FileSystem::ToUtf8(m_ProjectFile), m_Scene->GetEntityCount());
		return true;
	}

	void RuntimeLayer::OnAttach()
	{
		Application* application = Application::Get();
		AE_ASSERT(application != nullptr, "The runtime must run inside an Application");

		if (!loadProject())
		{
			application->Close();
			return;
		}

		Window& window = application->GetWindow();
		RendererSpecification rendererSpecification;
		rendererSpecification.Window = &window;
		rendererSpecification.VSync = window.IsVSync();
		rendererSpecification.EnableValidation = false;
		m_Renderer = CreateScope<Renderer>(rendererSpecification);
		if (!m_Renderer->IsInitialized())
		{
			AE_CORE_CRITICAL("The runtime requires a working renderer");
			application->Close();
			return;
		}

		m_SceneRenderer = CreateScope<SceneRenderer>();
		if (!m_SceneRenderer->Initialize(m_Renderer->GetDevice()))
		{
			AE_CORE_CRITICAL("The runtime requires the scene renderer");
			application->Close();
			return;
		}
		m_SceneRenderer->Resize(m_Renderer->GetWidth(), m_Renderer->GetHeight());

		m_ScreenBlit = CreateScope<ScreenBlit>();
		const std::string shaderDirectory = FileSystem::ToUtf8(FileSystem::GetExecutableDirectory() / "shaders");
		if (!m_ScreenBlit->Initialize(m_Renderer->GetDevice(), shaderDirectory))
		{
			AE_CORE_CRITICAL("The runtime requires the screen blit pass");
			application->Close();
			return;
		}

		m_Ready = true;
	}

	void RuntimeLayer::OnDetach()
	{
		if (m_Scene != nullptr)
		{
			m_Scene->RemoveSystem(&m_Physics);
			m_Scene->RemoveSystem(&m_Scripting);
			m_Scene->RemoveSystem(&m_Audio);
		}
		m_Scripting.Shutdown();
		m_Audio.Shutdown();
		m_Physics.Shutdown();
		m_ScreenBlit = nullptr;
		m_SceneRenderer = nullptr;
		m_Renderer = nullptr;
		m_Scene = nullptr;
	}

	void RuntimeLayer::OnUpdate(Timestep timestep)
	{
		if (!m_Ready)
		{
			return;
		}

		if (Input::IsKeyPressed(KeyCode::Escape))
		{
			Application::Get()->Close();
			return;
		}

		m_Scene->OnUpdate(timestep);
		renderFrame(timestep);
	}

	void RuntimeLayer::renderFrame(Timestep timestep)
	{
		(void)timestep;
		Window& window = Application::Get()->GetWindow();
		if (m_SceneRenderer->GetWidth() != window.GetWidth() || m_SceneRenderer->GetHeight() != window.GetHeight())
		{
			m_SceneRenderer->Resize(window.GetWidth(), window.GetHeight());
		}

		glm::mat4 viewProjection(1.0f);
		glm::vec3 cameraPosition(0.0f, 2.0f, 8.0f);
		glm::vec4 clearColor(0.02f, 0.02f, 0.03f, 1.0f);
		if (Entity cameraEntity = m_Scene->GetPrimaryCamera(); cameraEntity.IsValid())
		{
			const auto& camera = cameraEntity.GetComponent<CameraComponent>();
			const auto& transform = cameraEntity.GetComponent<TransformComponent>();
			Camera viewport;
			viewport.SetViewportSize(window.GetWidth(), window.GetHeight());
			viewProjection = viewport.ComputeViewProjection(transform, camera);
			cameraPosition = transform.WorldPosition;
			clearColor = camera.ClearColor;
		}

		const float shadowDistance = m_Scene->GetEnvironment().ShadowDistance;
		const glm::mat4 lightViewProjection =
			SceneRenderer::ComputeDirectionalShadowMatrix(*m_Scene, cameraPosition, shadowDistance);

		if (!m_Renderer->BeginFrame(clearColor))
		{
			return;
		}
		nvrhi::ICommandList* commandList = m_Renderer->GetCommandList();
		m_SceneRenderer->Render(commandList, *m_Scene, viewProjection, cameraPosition, lightViewProjection,
			m_Scene->GetEnvironment().EnableShadows);
		m_ScreenBlit->Draw(commandList, m_Renderer->GetFramebuffer(), m_SceneRenderer->GetFinalColor());

		if (!m_Automation.ScreenshotPath.empty() && m_RemainingFrames == 1)
		{
			m_Renderer->RequestScreenshot(m_Automation.ScreenshotPath);
		}
		m_Renderer->EndFrame();

		if (m_RemainingFrames > 0)
		{
			--m_RemainingFrames;
			if (m_RemainingFrames == 0)
			{
				AE_CORE_INFO("Runtime automation finished after {} frames", m_Automation.Frames);
				Application::Get()->Close();
			}
		}
	}

	void RuntimeLayer::OnEvent(Event& event)
	{
		if (event.GetEventType() == EventType::WindowResize)
		{
			event.Handled = true;
		}
	}
}
