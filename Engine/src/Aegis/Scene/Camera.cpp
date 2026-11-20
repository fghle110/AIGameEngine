#include "Aegis/Scene/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace aegis
{
	void Camera::SetViewportSize(u32 width, u32 height)
	{
		m_ViewportWidth = width == 0 ? 1 : width;
		m_ViewportHeight = height == 0 ? 1 : height;
	}

	f32 Camera::GetAspectRatio() const
	{
		return static_cast<f32>(m_ViewportWidth) / static_cast<f32>(m_ViewportHeight);
	}

	glm::mat4 Camera::ComputeView(const TransformComponent& transform)
	{
		const glm::vec3 eye = transform.WorldPosition;
		const glm::quat orientation = glm::normalize(transform.WorldRotation);
		const glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
		const glm::vec3 up = orientation * glm::vec3(0.0f, 1.0f, 0.0f);
		return glm::lookAt(eye, eye + forward, up);
	}

	glm::mat4 Camera::ComputeProjection(const CameraComponent& camera, f32 aspectRatio)
	{
		const f32 aspect = aspectRatio > 0.0f ? aspectRatio : 1.0f;
		if (camera.Projection == CameraProjection::Perspective)
		{
			return glm::perspective(glm::radians(camera.FieldOfView), aspect, camera.NearClip, camera.FarClip);
		}
		const f32 halfHeight = camera.OrthographicSize;
		const f32 halfWidth = halfHeight * aspect;
		return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, camera.NearClip, camera.FarClip);
	}

	glm::mat4 Camera::ComputeViewProjection(const TransformComponent& transform, const CameraComponent& camera) const
	{
		// Vulkan clip space: Y points down, depth range is [0, 1].
		glm::mat4 projection = ComputeProjection(camera, GetAspectRatio());
		projection[1][1] *= -1.0f;
		return projection * ComputeView(transform);
	}

	void Camera::ComputeRay(const TransformComponent& transform, const CameraComponent& camera, f32 aspectRatio,
		const glm::vec2& viewportPosition, glm::vec3& outOrigin, glm::vec3& outDirection)
	{
		const f32 aspect = aspectRatio > 0.0f ? aspectRatio : 1.0f;
		const glm::vec3 eye = transform.WorldPosition;
		const glm::quat orientation = glm::normalize(transform.WorldRotation);

		if (camera.Projection == CameraProjection::Orthographic)
		{
			const f32 halfHeight = camera.OrthographicSize;
			const f32 halfWidth = halfHeight * aspect;
			const f32 x = (viewportPosition.x * 2.0f - 1.0f) * halfWidth;
			const f32 y = (1.0f - viewportPosition.y * 2.0f) * halfHeight;
			outOrigin = eye + orientation * glm::vec3(x, y, 0.0f);
			outDirection = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
			return;
		}

		const f32 tanHalfFov = std::tan(glm::radians(camera.FieldOfView) * 0.5f);
		const f32 x = (viewportPosition.x * 2.0f - 1.0f) * tanHalfFov * aspect;
		const f32 y = (1.0f - viewportPosition.y * 2.0f) * tanHalfFov;
		outOrigin = eye;
		outDirection = glm::normalize(orientation * glm::vec3(x, y, -1.0f));
	}

	glm::vec3 Camera::WorldToViewport(const glm::mat4& viewProjection, const glm::vec3& worldPosition)
	{
		const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
		if (clip.w <= 0.0001f)
		{
			return { 0.0f, 0.0f, clip.w };
		}
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		return { (ndc.x * 0.5f + 0.5f), (0.5f - ndc.y * 0.5f), ndc.z };
	}

	glm::vec3 Camera::GetForward(const TransformComponent& transform)
	{
		return glm::normalize(transform.WorldRotation) * glm::vec3(0.0f, 0.0f, -1.0f);
	}

	glm::vec3 Camera::GetRight(const TransformComponent& transform)
	{
		return glm::normalize(transform.WorldRotation) * glm::vec3(1.0f, 0.0f, 0.0f);
	}

	glm::vec3 Camera::GetUp(const TransformComponent& transform)
	{
		return glm::normalize(transform.WorldRotation) * glm::vec3(0.0f, 1.0f, 0.0f);
	}
}
