#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Scene/Components.h"

#include <glm/glm.hpp>

namespace aegis
{
	// Camera math shared by the renderer, the editor viewport and scripts.
	class Camera
	{
	public:
		Camera() = default;

		void SetViewportSize(u32 width, u32 height);
		u32 GetViewportWidth() const { return m_ViewportWidth; }
		u32 GetViewportHeight() const { return m_ViewportHeight; }
		f32 GetAspectRatio() const;

		static glm::mat4 ComputeView(const TransformComponent& transform);
		static glm::mat4 ComputeProjection(const CameraComponent& camera, f32 aspectRatio);
		glm::mat4 ComputeViewProjection(const TransformComponent& transform, const CameraComponent& camera) const;

		// Ray through a normalized viewport position (0..1, origin top-left).
		static void ComputeRay(const TransformComponent& transform, const CameraComponent& camera, f32 aspectRatio,
			const glm::vec2& viewportPosition, glm::vec3& outOrigin, glm::vec3& outDirection);

		// Projects a world position into normalized viewport space (0..1).
		static glm::vec3 WorldToViewport(const glm::mat4& viewProjection, const glm::vec3& worldPosition);

		static glm::vec3 GetForward(const TransformComponent& transform);
		static glm::vec3 GetRight(const TransformComponent& transform);
		static glm::vec3 GetUp(const TransformComponent& transform);

	private:
		u32 m_ViewportWidth = 1;
		u32 m_ViewportHeight = 1;
	};
}
