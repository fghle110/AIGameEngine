#pragma once

#include "Aegis/Core/Base.h"

#include <functional>
#include <string>

namespace aegis
{
	class Event;

	struct WindowProps
	{
		std::string Title = "Aegis";
		u32 Width = 1600;
		u32 Height = 900;
		bool VSync = true;
		bool Decorated = true;
		bool Resizable = true;
		i32 X = -1;
		i32 Y = -1;
	};

	// Platform window. The implementation uses GLFW on every platform and always
	// creates a Vulkan-compatible (no client API) window.
	class Window
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		virtual ~Window() = default;

		virtual void OnUpdate() = 0;

		virtual u32 GetWidth() const = 0;
		virtual u32 GetHeight() const = 0;
		virtual f32 GetAspectRatio() const = 0;

		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual void SetTitle(const std::string& title) = 0;
		virtual void SetShouldClose(bool shouldClose) = 0;
		virtual bool ShouldClose() const = 0;
		virtual void Maximize() = 0;
		virtual void Restore() = 0;
		virtual bool IsMinimized() const = 0;
		virtual bool IsFocused() const = 0;

		// Returns the native GLFWwindow*. Kept as void* so that engine headers do
		// not leak the platform toolkit.
		virtual void* GetNativeWindow() const = 0;

		static Scope<Window> Create(const WindowProps& props = {});
	};
}
