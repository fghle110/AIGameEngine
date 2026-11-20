#include "Aegis/Core/Application.h"

#include "Aegis/Core/Config.h"
#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Layer.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Events/ApplicationEvent.h"
#include "Aegis/Events/Event.h"

#include <thread>

namespace aegis
{
	namespace
	{
		Application* s_Application = nullptr;
	}

	Application::Application(const ApplicationSpecification& specification)
		: m_Specification(specification)
	{
		AE_ASSERT(s_Application == nullptr, "Only one Application may exist per process");
		s_Application = this;

		Log::Init("logs");

		if (!m_Specification.WorkingDirectory.empty())
		{
			const auto directory = FileSystem::FromUtf8(m_Specification.WorkingDirectory);
			if (!FileSystem::SetCurrentWorkingDirectory(directory))
			{
				AE_CORE_ERROR("Could not change working directory to '{}'", m_Specification.WorkingDirectory);
			}
		}

		AE_CORE_INFO("Starting {} ({})", m_Specification.Name, c_EngineVersion);

		if (m_Specification.CreateWindow)
		{
			WindowProps props;
			props.Title = m_Specification.Name;
			props.Width = m_Specification.WindowWidth;
			props.Height = m_Specification.WindowHeight;
			props.VSync = m_Specification.VSync;
			m_Window = Window::Create(props);
			m_Window->SetEventCallback(AE_BIND_EVENT_FN(Application::OnEvent));
		}

		m_LastFrameTime = std::chrono::steady_clock::now();
	}

	Application::~Application()
	{
		AE_CORE_INFO("Shutting down {}", m_Specification.Name);
		m_LayerStack.Clear();
		s_Application = nullptr;
		Log::Shutdown();
	}

	void Application::Run()
	{
		m_LastFrameTime = std::chrono::steady_clock::now();

		while (m_Running)
		{
			const auto now = std::chrono::steady_clock::now();
			const f32 frameSeconds = std::chrono::duration<f32>(now - m_LastFrameTime).count();
			m_LastFrameTime = now;
			const Timestep timestep(frameSeconds);

			UpdateFrameStats(frameSeconds);

			if (m_Window)
			{
				m_Window->OnUpdate();

				if (m_Window->ShouldClose())
				{
					Close();
					continue;
				}

				m_Minimized = m_Window->IsMinimized();
			}

			if (!m_Minimized)
			{
				for (Layer* layer : m_LayerStack)
				{
					layer->OnUpdate(timestep);
				}
				for (Layer* layer : m_LayerStack)
				{
					layer->OnImGuiRender();
				}

				AppRenderEvent renderEvent;
				OnEvent(renderEvent);
			}
			else
			{
				// Avoid burning CPU while the window is minimized; the OS still
				// delivers window messages through GLFW polling.
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
			}
		}
	}

	void Application::Close()
	{
		m_Running = false;
	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
	}

	void Application::PushOverlay(Layer* overlay)
	{
		m_LayerStack.PushOverlay(overlay);
	}

	Window& Application::GetWindow()
	{
		AE_ASSERT(m_Window != nullptr, "This application was created without a window");
		return *m_Window;
	}

	void Application::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& e) { return OnWindowClose(e); });

		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			if (event.Handled)
			{
				break;
			}
			(*it)->OnEvent(event);
		}
	}

	bool Application::OnWindowClose(Event& event)
	{
		(void)event;
		Close();
		return true;
	}

	void Application::UpdateFrameStats(f32 frameTime)
	{
		m_FrameAccumulator += frameTime;
		++m_FrameSamples;
		if (m_FrameAccumulator >= 0.25f)
		{
			m_AverageFrameTime = m_FrameAccumulator / static_cast<f32>(m_FrameSamples);
			m_FramesPerSecond = m_AverageFrameTime > 0.0f ? 1.0f / m_AverageFrameTime : 0.0f;
			m_FrameAccumulator = 0.0f;
			m_FrameSamples = 0;
		}
	}

	Application* Application::Get()
	{
		return s_Application;
	}
}
