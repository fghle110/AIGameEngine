#include "Aegis/Math/EditorGizmo.h"

#include <algorithm>
#include <cmath>

namespace aegis
{
	namespace
	{
		constexpr glm::vec3 c_AxisDirections[3] = {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f),
		};

		// Squared distance from a point to a segment; used for handle picking.
		f32 DistanceToSegmentSquared(const glm::vec2& point, const glm::vec2& a, const glm::vec2& b)
		{
			const glm::vec2 ab = b - a;
			const f32 lengthSquared = glm::dot(ab, ab);
			if (lengthSquared <= 1.0e-8f)
			{
				const glm::vec2 difference = point - a;
				return glm::dot(difference, difference);
			}
			const f32 t = glm::clamp(glm::dot(point - a, ab) / lengthSquared, 0.0f, 1.0f);
			const glm::vec2 difference = point - (a + ab * t);
			return glm::dot(difference, difference);
		}
	}

	bool TranslationGizmo::Project(const glm::mat4& viewProjection, const glm::vec3& world,
		const glm::vec2& viewportSize, glm::vec2& outScreen)
	{
		const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
		if (clip.w <= 1.0e-5f)
		{
			outScreen = glm::vec2(-1.0e6f);
			return false;
		}
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		outScreen = glm::vec2((ndc.x * 0.5f + 0.5f) * viewportSize.x, (0.5f - ndc.y * 0.5f) * viewportSize.y);
		return true;
	}

	TranslationGizmo TranslationGizmo::Build(const glm::mat4& viewProjection, const glm::vec3& origin,
		const glm::vec3& cameraPosition, const glm::vec2& viewportSize, f32 handleScale)
	{
		TranslationGizmo gizmo;
		gizmo.m_Origin = origin;
		gizmo.m_AxisLength = std::max(glm::length(cameraPosition - origin) * handleScale, 0.05f);

		glm::vec2 screenOrigin(0.0f);
		const bool originVisible = Project(viewProjection, origin, viewportSize, screenOrigin);
		gizmo.m_ScreenOrigin = screenOrigin;
		for (usize axis = 0; axis < 3; ++axis)
		{
			ScreenGizmoAxis& handle = gizmo.m_Axes[axis];
			handle.Direction = c_AxisDirections[axis];
			handle.ScreenOrigin = screenOrigin;
			handle.ScreenEnd = screenOrigin;
			const glm::vec3 tip = origin + handle.Direction * gizmo.m_AxisLength;
			glm::vec2 screenTip(0.0f);
			handle.Visible = originVisible && Project(viewProjection, tip, viewportSize, screenTip);
			if (handle.Visible)
			{
				handle.ScreenEnd = screenTip;
			}
		}
		return gizmo;
	}

	const ScreenGizmoAxis& TranslationGizmo::GetAxis(usize index) const
	{
		return m_Axes[std::min(index, static_cast<usize>(2))];
	}

	i32 TranslationGizmo::HitTestAxis(const glm::vec2& screenPoint, f32 thresholdPixels) const
	{
		i32 bestAxis = -1;
		f32 bestDistance = thresholdPixels * thresholdPixels;
		for (i32 axis = 0; axis < 3; ++axis)
		{
			const ScreenGizmoAxis& handle = m_Axes[static_cast<usize>(axis)];
			if (!handle.Visible)
			{
				continue;
			}
			const f32 distance = DistanceToSegmentSquared(screenPoint, handle.ScreenOrigin, handle.ScreenEnd);
			if (distance < bestDistance)
			{
				bestDistance = distance;
				bestAxis = axis;
			}
		}
		return bestAxis;
	}

	bool TranslationGizmo::HitTestCenter(const glm::vec2& screenPoint, f32 sizePixels) const
	{
		return std::abs(screenPoint.x - m_ScreenOrigin.x) <= sizePixels &&
			std::abs(screenPoint.y - m_ScreenOrigin.y) <= sizePixels;
	}

	std::optional<glm::vec3> TranslationGizmo::IntersectPlane(const glm::vec3& rayOrigin,
		const glm::vec3& rayDirection, const glm::vec3& planePoint, const glm::vec3& planeNormal)
	{
		const f32 denominator = glm::dot(rayDirection, planeNormal);
		if (std::abs(denominator) <= 1.0e-6f)
		{
			return std::nullopt;
		}
		const f32 distance = glm::dot(planePoint - rayOrigin, planeNormal) / denominator;
		if (distance < 0.0f)
		{
			return std::nullopt;
		}
		return rayOrigin + rayDirection * distance;
	}

	glm::vec3 TranslationGizmo::Constrain(const glm::vec3& delta, i32 axis)
	{
		switch (axis)
		{
			case 0: return glm::vec3(delta.x, 0.0f, 0.0f);
			case 1: return glm::vec3(0.0f, delta.y, 0.0f);
			case 2: return glm::vec3(0.0f, 0.0f, delta.z);
			default: return delta;
		}
	}
}
