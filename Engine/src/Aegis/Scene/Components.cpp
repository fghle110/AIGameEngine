#include "Aegis/Scene/Components.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

namespace aegis
{
	glm::mat4 TransformComponent::GetLocalTransform() const
	{
		return glm::translate(glm::mat4(1.0f), LocalPosition) * glm::toMat4(LocalRotation) *
			   glm::scale(glm::mat4(1.0f), LocalScale);
	}

	glm::mat4 TransformComponent::GetWorldTransform() const
	{
		return glm::translate(glm::mat4(1.0f), WorldPosition) * glm::toMat4(WorldRotation) *
			   glm::scale(glm::mat4(1.0f), WorldScale);
	}

	glm::vec3 TransformComponent::GetLocalEulerAngles() const
	{
		return glm::degrees(glm::eulerAngles(LocalRotation));
	}

	void TransformComponent::SetLocalEulerAngles(const glm::vec3& degrees)
	{
		LocalRotation = glm::normalize(glm::quat(glm::radians(degrees)));
	}
}
