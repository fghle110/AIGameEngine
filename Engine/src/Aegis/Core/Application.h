#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/LayerStack.h"
#include "Aegis/Core/Time.h"
#include "Aegis/Core/Window.h"

#include <chrono>
#include <string>

namespace aegis
{
	class Event;

	struct ApplicationSpecification
	{
		std::string Name = "Aegis";
		// Absolute or relative to the process working directory. When empty the
		// current working directory is kept.
		std::string WorkingDirectory;
		u32 WindowWidth = 1600;
		u32 WindowHeight = 900;
		bool VSync = true;
		bool CreateWindow = true;
	};

	// Owns the window, the layer stack and the main loop. Both the editor and the
	// exported runtime are Applications; the difference is which layers they push.
	class Application
	{
	public:
		explicit Application(const ApplicationSpecification& specification = {});
		virtual ~Application();

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		void Run();
		void Close();

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);

		Window& GetWindow();
		const ApplicationSpecification& GetSpecification() const { return m_Specification; }

		f32 GetFramesPerSecond() const { return m_FramesPerSecond; }
		f32 GetAverageFrameTime() const { return m_AverageFrameTime; }

		static Application* Get();

	protected:
		virtual void OnEvent(Event& event);

	private:
		bool OnWindowClose(Event& event);
		void UpdateFrameStats(f32 frameTime);

		ApplicationSpecification m_Specification;
		Scope<Window> m_Window;
		LayerStack m_LayerStack;

		bool m_Running = true;
		bool m_Minimized = false;

		std::chrono::steady_clock::time_point m_LastFrameTime;

		// Rolling frame statistics (updated 4x per second).
		f32 m_FramesPerSecond = 0.0f;
		f32 m_AverageFrameTime = 0.0f;
		f32 m_FrameAccumulator = 0.0f;
		u32 m_FrameSamples = 0;
	};
}
