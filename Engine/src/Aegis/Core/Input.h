#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/CursorMode.h"
#include "Aegis/Core/KeyCodes.h"

#include <utility>

namespace aegis
{
	// Input state queried through GLFW. Events are the preferred way to react to
	// discrete input; polling is for continuous actions (held keys, mouse look).
	class Input
	{
	public:
		static void Init(void* nativeWindow);
		static void Shutdown();

		static bool IsKeyPressed(KeyCode key);
		static bool IsMouseButtonPressed(MouseCode button);

		static std::pair<f32, f32> GetMousePosition();
		static f32 GetMouseX();
		static f32 GetMouseY();

		static void SetCursorMode(CursorMode mode);
		static CursorMode GetCursorMode();

		static bool IsShiftDown();
		static bool IsControlDown();
		static bool IsAltDown();
	};
}
