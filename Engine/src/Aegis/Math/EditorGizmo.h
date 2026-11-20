#pragma once

#include "Aegis/Core/Base.h"

#include <glm/glm.hpp>

#include <array>
#include <optional>

namespace aegis
{
	// One axis handle of a translation gizmo, projected into viewport pixels.
	struct ScreenGizmoAxis
	{
		glm::vec3 Direction{ 0.0f };
		glm::vec2 ScreenOrigin{ 0.0f };
		glm::vec2 ScreenEnd{ 0.0f };
		bool Visible = false;
	};

	// Screen-space projection of a translation gizmo plus the drag math that turns
	// a mouse position into a movement constrained to one world axis. The editor
	// draws and drives the handles with it; the pure functions are unit tested
	// without a window or a GPU.
	class TranslationGizmo
	{
	public:
		// Builds the three axis handles around a world-space origin. Handle length
		// scales with the camera distance so the gizmo keeps a stable screen size.
		static TranslationGizmo Build(const glm::mat4& viewProjection, const glm::vec3& origin,
			const glm::vec3& cameraPosition, const glm::vec2& viewportSize, f32 handleScale = 0.16f);

		[[nodiscard]] const glm::vec3& GetOrigin() const { return m_Origin; }
		[[nodiscard]] glm::vec2 GetScreenOrigin() const { return m_ScreenOrigin; }
		[[nodiscard]] f32 GetAxisLength() const { return m_AxisLength; }
		[[nodiscard]] const ScreenGizmoAxis& GetAxis(usize index) const;

		// Closest axis whose projected segment is within threshold pixels of the
		// point; -1 when none is close enough.
		[[nodiscard]] i32 HitTestAxis(const glm::vec2& screenPoint, f32 thresholdPixels = 9.0f) const;
		// The centre handle moves the object on the camera-facing plane.
		[[nodiscard]] bool HitTestCenter(const glm::vec2& screenPoint, f32 sizePixels = 12.0f) const;

		// World position to viewport pixels (origin top-left, y down). False when the
		// point is behind the camera.
		static bool Project(const glm::mat4& viewProjection, const glm::vec3& world,
			const glm::vec2& viewportSize, glm::vec2& outScreen);

		static std::optional<glm::vec3> IntersectPlane(const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
			const glm::vec3& planePoint, const glm::vec3& planeNormal);

		// Projects a free delta onto one axis (0 = X, 1 = Y, 2 = Z); a negative axis
		// keeps the delta untouched (centre handle).
		static glm::vec3 Constrain(const glm::vec3& delta, i32 axis);

	private:
		glm::vec3 m_Origin{ 0.0f };
		glm::vec2 m_ScreenOrigin{ 0.0f };
		f32 m_AxisLength = 1.0f;
		std::array<ScreenGizmoAxis, 3> m_Axes{};
	};
}
