#include "Editor/EditorCamera.h"

#include "Aegis/Core/Input.h"
#include "Aegis/Core/KeyCodes.h"
#include "Aegis/Events/Event.h"

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

namespace aegis
{
	namespace
	{
		glm::vec3 DirectionFromAngles(f32 yawDegrees, f32 pitchDegrees)
		{
			const f32 yaw = glm::radians(yawDegrees);
			const f32 pitch = glm::radians(pitchDegrees);
			return glm::normalize(glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
				-std::cos(pitch) * std::cos(yaw)));
		}
	}

	EditorCamera::EditorCamera() = default;

	void EditorCamera::SetRotation(f32 yawDegrees, f32 pitchDegrees)
	{
		m_Yaw = yawDegrees;
		m_Pitch = glm::clamp(pitchDegrees, -89.0f, 89.0f);
	}

	glm::vec3 EditorCamera::GetForward() const
	{
		return DirectionFromAngles(m_Yaw, m_Pitch);
	}

	glm::vec3 EditorCamera::GetRight() const
	{
		return glm::normalize(glm::cross(GetForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
	}

	glm::vec3 EditorCamera::GetUp() const
	{
		return glm::normalize(glm::cross(GetRight(), GetForward()));
	}

	glm::mat4 EditorCamera::GetViewMatrix() const
	{
		return glm::lookAt(m_Position, m_Position + GetForward(), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::mat4 EditorCamera::GetProjectionMatrix(f32 aspectRatio) const
	{
		// NVRHI's Vulkan backend uses a DX-style (y-up NDC) viewport; the standard
		// projection with a [0, 1] depth range fits it directly.
		return glm::perspective(glm::radians(m_FieldOfView), aspectRatio > 0.0f ? aspectRatio : 1.0f, m_NearClip,
			m_FarClip);
	}

	glm::mat4 EditorCamera::GetViewProjection(f32 aspectRatio) const
	{
		return GetProjectionMatrix(aspectRatio) * GetViewMatrix();
	}

	void EditorCamera::OnUpdate(Timestep timestep, bool viewportHovered)
	{
		const f32 deltaSeconds = timestep.GetSeconds();
		if (deltaSeconds <= 0.0f)
		{
			return;
		}

		ImGuiIO& io = ImGui::GetIO();

		// Looking around requires the right mouse button over the viewport.
		const bool looking = viewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
		if (looking)
		{
			const ImVec2 mouseDelta = io.MouseDelta;
			m_Yaw += mouseDelta.x * 0.15f;
			m_Pitch = glm::clamp(m_Pitch - mouseDelta.y * 0.15f, -89.0f, 89.0f);
		}

		glm::vec3 wishDirection(0.0f);
		if (viewportHovered)
		{
			const glm::vec3 forward = GetForward();
			const glm::vec3 right = GetRight();
			if (Input::IsKeyPressed(KeyCode::W)) wishDirection += forward;
			if (Input::IsKeyPressed(KeyCode::S)) wishDirection -= forward;
			if (Input::IsKeyPressed(KeyCode::D)) wishDirection += right;
			if (Input::IsKeyPressed(KeyCode::A)) wishDirection -= right;
			if (Input::IsKeyPressed(KeyCode::E)) wishDirection += glm::vec3(0.0f, 1.0f, 0.0f);
			if (Input::IsKeyPressed(KeyCode::Q)) wishDirection -= glm::vec3(0.0f, 1.0f, 0.0f);
		}

		if (glm::length(wishDirection) > 0.0001f)
		{
			wishDirection = glm::normalize(wishDirection);
		}

		const f32 speed = m_MoveSpeed * (Input::IsShiftDown() ? 4.0f : 1.0f);
		const glm::vec3 targetVelocity = wishDirection * speed;
		const f32 blend = glm::clamp(deltaSeconds * m_VelocityDamping, 0.0f, 1.0f);
		m_Velocity = glm::mix(m_Velocity, targetVelocity, blend);
		m_Position += m_Velocity * deltaSeconds;

		// Scroll over the viewport changes the fly speed.
		if (viewportHovered && io.MouseWheel != 0.0f)
		{
			SetMoveSpeed(m_MoveSpeed * (io.MouseWheel > 0.0f ? 1.15f : 0.87f));
		}
	}

	void EditorCamera::OnEvent(Event& event)
	{
		(void)event;
	}

	void EditorCamera::Focus(const glm::vec3& point, f32 distance)
	{
		m_Position = point - GetForward() * distance;
	}
}
