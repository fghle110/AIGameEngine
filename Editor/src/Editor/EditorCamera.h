#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/Time.h"

#include <glm/glm.hpp>

namespace aegis
{
	class Event;

	// Free-flying editor camera. Movement is driven by held keys while the
	// viewport is hovered; look direction follows right-mouse dragging.
	class EditorCamera
	{
	public:
		EditorCamera();

		void OnUpdate(Timestep timestep, bool viewportHovered);
		void OnEvent(Event& event);

		glm::mat4 GetViewMatrix() const;
		glm::mat4 GetProjectionMatrix(f32 aspectRatio) const;
		glm::mat4 GetViewProjection(f32 aspectRatio) const;

		glm::vec3 GetPosition() const { return m_Position; }
		glm::vec3 GetForward() const;
		glm::vec3 GetRight() const;
		glm::vec3 GetUp() const;

		f32 GetYawDegrees() const { return m_Yaw; }
		f32 GetPitchDegrees() const { return m_Pitch; }
		f32 GetMoveSpeed() const { return m_MoveSpeed; }
		f32 GetFieldOfView() const { return m_FieldOfView; }
		f32 GetNearClip() const { return m_NearClip; }
		f32 GetFarClip() const { return m_FarClip; }

		void SetPosition(const glm::vec3& position) { m_Position = position; }
		void SetRotation(f32 yawDegrees, f32 pitchDegrees);
		void SetFieldOfView(f32 degrees) { m_FieldOfView = glm::clamp(degrees, 10.0f, 120.0f); }
		void SetMoveSpeed(f32 speed) { m_MoveSpeed = glm::clamp(speed, 0.25f, 200.0f); }
		void Focus(const glm::vec3& point, f32 distance = 6.0f);

	private:
		glm::vec3 m_Position{ 0.0f, 3.0f, 8.0f };
		f32 m_Yaw = 0.0f;
		f32 m_Pitch = -18.0f;
		f32 m_MoveSpeed = 8.0f;
		f32 m_FieldOfView = 50.0f;
		f32 m_NearClip = 0.05f;
		f32 m_FarClip = 5000.0f;
		f32 m_VelocityDamping = 12.0f;
		glm::vec3 m_Velocity{ 0.0f };
	};
}
