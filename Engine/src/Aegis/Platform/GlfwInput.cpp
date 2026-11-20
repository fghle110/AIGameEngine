#include "Aegis/Core/Input.h"

#include "Aegis/Core/Window.h"

#include <GLFW/glfw3.h>

namespace aegis
{
	namespace
	{
		GLFWwindow* s_Window = nullptr;
		CursorMode s_CursorMode = CursorMode::Normal;
	}

	void Input::Init(void* nativeWindow)
	{
		s_Window = static_cast<GLFWwindow*>(nativeWindow);
		s_CursorMode = CursorMode::Normal;
	}

	void Input::Shutdown()
	{
		s_Window = nullptr;
	}

	bool Input::IsKeyPressed(KeyCode key)
	{
		if (s_Window == nullptr)
		{
			return false;
		}
		const int state = glfwGetKey(s_Window, static_cast<int>(key));
		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	bool Input::IsMouseButtonPressed(MouseCode button)
	{
		if (s_Window == nullptr)
		{
			return false;
		}
		return glfwGetMouseButton(s_Window, static_cast<int>(button)) == GLFW_PRESS;
	}

	std::pair<f32, f32> Input::GetMousePosition()
	{
		if (s_Window == nullptr)
		{
			return { 0.0f, 0.0f };
		}
		double x = 0.0;
		double y = 0.0;
		glfwGetCursorPos(s_Window, &x, &y);
		return { static_cast<f32>(x), static_cast<f32>(y) };
	}

	f32 Input::GetMouseX()
	{
		return GetMousePosition().first;
	}

	f32 Input::GetMouseY()
	{
		return GetMousePosition().second;
	}

	void Input::SetCursorMode(CursorMode mode)
	{
		if (s_Window == nullptr || s_CursorMode == mode)
		{
			return;
		}
		s_CursorMode = mode;
		glfwSetInputMode(s_Window, GLFW_CURSOR, static_cast<int>(mode));
		if (mode == CursorMode::Disabled && glfwRawMouseMotionSupported() != 0)
		{
			glfwSetInputMode(s_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
		}
	}

	CursorMode Input::GetCursorMode()
	{
		return s_CursorMode;
	}

	bool Input::IsShiftDown()
	{
		return IsKeyPressed(KeyCode::LeftShift) || IsKeyPressed(KeyCode::RightShift);
	}

	bool Input::IsControlDown()
	{
		return IsKeyPressed(KeyCode::LeftControl) || IsKeyPressed(KeyCode::RightControl);
	}

	bool Input::IsAltDown()
	{
		return IsKeyPressed(KeyCode::LeftAlt) || IsKeyPressed(KeyCode::RightAlt);
	}
}
