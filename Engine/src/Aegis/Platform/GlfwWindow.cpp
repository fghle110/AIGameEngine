#include "Aegis/Core/Window.h"

#include "Aegis/Core/Input.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Events/ApplicationEvent.h"
#include "Aegis/Events/KeyEvent.h"
#include "Aegis/Events/MouseEvent.h"

#include <volk.h>

#include <GLFW/glfw3.h>

namespace aegis
{
	namespace
	{
		u32 s_GlfwWindowCount = 0;

		bool s_GlfwInitialized = false;

		// GLFW reports errors through a callback; log once per failure.
		void GlfwErrorCallback(int error, const char* description)
		{
			AE_CORE_ERROR("GLFW error {}: {}", error, description);
		}

		void EnsureGlfwInitialized()
		{
			if (s_GlfwInitialized)
			{
				return;
			}
			glfwSetErrorCallback(GlfwErrorCallback);
			const int result = glfwInit();
			AE_ASSERT(result == GLFW_TRUE, "Could not initialize GLFW");
			if (result != GLFW_TRUE)
			{
				AE_CORE_CRITICAL("Failed to initialize GLFW");
				return;
			}
			s_GlfwInitialized = true;
		}

		KeyCode ToKeyCode(int key)
		{
			return static_cast<KeyCode>(key);
		}

		MouseCode ToMouseCode(int button)
		{
			return static_cast<MouseCode>(button);
		}
	}

	class GlfwWindow final : public Window
	{
	public:
		explicit GlfwWindow(const WindowProps& props)
		{
			Init(props);
		}

		~GlfwWindow() override
		{
			Shutdown();
		}

		void OnUpdate() override
		{
			glfwPollEvents();
		}

		u32 GetWidth() const override { return m_Data.Width; }
		u32 GetHeight() const override { return m_Data.Height; }
		f32 GetAspectRatio() const override
		{
			return m_Data.Height == 0 ? 1.0f : static_cast<f32>(m_Data.Width) / static_cast<f32>(m_Data.Height);
		}

		void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
		void SetVSync(bool enabled) override { m_Data.VSync = enabled; }
		bool IsVSync() const override { return m_Data.VSync; }

		void SetTitle(const std::string& title) override
		{
			glfwSetWindowTitle(m_Window, title.c_str());
		}

		void SetShouldClose(bool shouldClose) override
		{
			glfwSetWindowShouldClose(m_Window, shouldClose ? GLFW_TRUE : GLFW_FALSE);
		}

		bool ShouldClose() const override { return glfwWindowShouldClose(m_Window) != 0; }
		void Maximize() override { glfwMaximizeWindow(m_Window); }
		void Restore() override { glfwRestoreWindow(m_Window); }
		bool IsMinimized() const override { return glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) != 0; }
		bool IsFocused() const override { return glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != 0; }
		void* GetNativeWindow() const override { return m_Window; }

	private:
		struct WindowData
		{
			std::string Title;
			u32 Width = 0;
			u32 Height = 0;
			bool VSync = true;
			EventCallbackFn EventCallback;
		};

		void Init(const WindowProps& props)
		{
			m_Data.Title = props.Title;
			m_Data.Width = props.Width;
			m_Data.Height = props.Height;
			m_Data.VSync = props.VSync;

			EnsureGlfwInitialized();

			// The renderer owns the Vulkan instance/device: GLFW must not create a
			// client API context.
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_DECORATED, props.Decorated ? GLFW_TRUE : GLFW_FALSE);
			glfwWindowHint(GLFW_RESIZABLE, props.Resizable ? GLFW_TRUE : GLFW_FALSE);

			m_Window = glfwCreateWindow(static_cast<int>(props.Width), static_cast<int>(props.Height), props.Title.c_str(),
				nullptr, nullptr);
			if (m_Window == nullptr)
			{
				AE_CORE_CRITICAL("Failed to create GLFW window '{}' ({}x{})", props.Title, props.Width, props.Height);
				return;
			}

			if (props.X >= 0 && props.Y >= 0)
			{
				glfwSetWindowPos(m_Window, props.X, props.Y);
			}
			else
			{
				CenterOnPrimaryMonitor(props.Width, props.Height);
			}

			++s_GlfwWindowCount;
			glfwSetWindowUserPointer(m_Window, &m_Data);

			Input::Init(m_Window);
			SetCallbacks();

			AE_CORE_INFO("Created window '{}' ({}x{})", m_Data.Title, m_Data.Width, m_Data.Height);
		}

		void CenterOnPrimaryMonitor(u32 width, u32 height)
		{
			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			if (monitor == nullptr)
			{
				return;
			}
			const GLFWvidmode* mode = glfwGetVideoMode(monitor);
			if (mode == nullptr)
			{
				return;
			}
			int monitorX = 0;
			int monitorY = 0;
			glfwGetMonitorPos(monitor, &monitorX, &monitorY);
			const int x = monitorX + (mode->width - static_cast<int>(width)) / 2;
			const int y = monitorY + (mode->height - static_cast<int>(height)) / 2;
			glfwSetWindowPos(m_Window, x, y);
		}

		void Shutdown()
		{
			if (m_Window == nullptr)
			{
				return;
			}
			glfwDestroyWindow(m_Window);
			m_Window = nullptr;
			--s_GlfwWindowCount;
			if (s_GlfwWindowCount == 0 && s_GlfwInitialized)
			{
				glfwTerminate();
				s_GlfwInitialized = false;
			}
		}

		void SetCallbacks()
		{
			glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
			{
				auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
				WindowCloseEvent event;
				data->EventCallback(event);
			});

			glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
			{
				auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
				data->Width = static_cast<u32>(width);
				data->Height = static_cast<u32>(height);
				WindowResizeEvent event(static_cast<u32>(width), static_cast<u32>(height));
				data->EventCallback(event);
			});

			glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused)
			{
				auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
				if (focused != 0)
				{
					WindowFocusEvent event;
					data->EventCallback(event);
				}
				else
				{
					WindowLostFocusEvent event;
					data->EventCallback(event);
				}
			});

			glfwSetWindowPosCallback(m_Window, [](GLFWwindow* window, int x, int y)
			{
				auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
				WindowMovedEvent event(x, y);
				data->EventCallback(event);
			});

			glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
			{
				(void)scancode;
				(void)mods;
				auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
				switch (action)
				{
					case GLFW_PRESS:
					{
						KeyPressedEvent event(ToKeyCode(key), false);
						data->EventCallback(event);
						break;
					}
					case GLFW_REPEAT:
					{
						KeyPressedEvent event(ToKeyCode(key), true);
						data->EventCallback(event);
						break;
					}
					case GLFW_RELEASE:
					{
						KeyReleasedEvent event(ToKeyCode(key));
						data->EventCallback(event);
						break;
					}
					default: break;
				}
			});

			glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int codepoint)
			{
				auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
				KeyTypedEvent event(static_cast<KeyCode>(static_cast<i32>(codepoint)));
				data->EventCallback(event);
			});

			glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
			{
				(void)mods;
				auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
				if (action == GLFW_PRESS)
				{
					MouseButtonPressedEvent event(ToMouseCode(button));
					data->EventCallback(event);
				}
				else if (action == GLFW_RELEASE)
				{
					MouseButtonReleasedEvent event(ToMouseCode(button));
					data->EventCallback(event);
				}
			});

			glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
			{
				auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
				MouseScrolledEvent event(static_cast<f32>(xOffset), static_cast<f32>(yOffset));
				data->EventCallback(event);
			});

			glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y)
			{
				auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
				MouseMovedEvent event(static_cast<f32>(x), static_cast<f32>(y));
				data->EventCallback(event);
			});
		}

		GLFWwindow* m_Window = nullptr;
		WindowData m_Data;
	};

	Scope<Window> Window::Create(const WindowProps& props)
	{
		return CreateScope<GlfwWindow>(props);
	}
}
