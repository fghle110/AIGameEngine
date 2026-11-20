#pragma once

namespace aegis
{
	enum class CursorMode : int
	{
		Normal = 0x00034001,  // GLFW_CURSOR_NORMAL
		Hidden = 0x00034002,  // GLFW_CURSOR_HIDDEN
		Disabled = 0x00034003 // GLFW_CURSOR_DISABLED
	};
}
