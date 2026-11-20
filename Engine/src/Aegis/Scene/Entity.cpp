#include "Aegis/Scene/Entity.h"

#include "Aegis/Core/Log.h"
#include "Aegis/Scene/Scene.h"

#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>

namespace aegis
{
	Entity::Entity(ecs::EntityHandle handle, Scene* scene)
		: m_Handle(handle)
		, m_Scene(scene)
	{
	}

	bool Entity::IsValid() const
	{
		return m_Scene != nullptr && m_Scene->GetRegistry().IsAlive(m_Handle);
	}

	ecs::Registry& Entity::GetRegistry() const
	{
		AE_ASSERT(m_Scene != nullptr, "Entity is not bound to a scene");
		return m_Scene->GetRegistry();
	}

	Uuid Entity::GetUuid() const
	{
		const IdComponent* id = TryGetComponent<IdComponent>();
		return id != nullptr ? id->Id : Uuid::Null();
	}

	const std::string& Entity::GetName() const
	{
		static const std::string s_Empty;
		const TagComponent* tag = TryGetComponent<TagComponent>();
		return tag != nullptr ? tag->Tag : s_Empty;
	}

	void Entity::SetName(const std::string& name)
	{
		if (TagComponent* tag = TryGetComponent<TagComponent>())
		{
			tag->Tag = name;
		}
	}

	bool Entity::IsActive() const
	{
		const ActiveComponent* active = TryGetComponent<ActiveComponent>();
		return active != nullptr ? active->Active : true;
	}

	void Entity::SetActive(bool active)
	{
		if (ActiveComponent* component = TryGetComponent<ActiveComponent>())
		{
			component->Active = active;
		}
	}

	void Entity::SetParent(Entity parent)
	{
		if (!IsValid())
		{
			return;
		}
		AE_ASSERT(m_Scene != nullptr, "Entity is not bound to a scene");

		TransformComponent* transform = TryGetComponent<TransformComponent>();
		if (transform == nullptr)
		{
			AE_CORE_WARN("Cannot set a parent on entity '{}': it has no transform", GetName());
			return;
		}
		if (parent == *this)
		{
			return;
		}

		const Uuid newParentUuid = parent.IsValid() ? parent.GetUuid() : Uuid::Null();
		if (!newParentUuid.IsNull())
		{
			if (!parent.HasComponent<TransformComponent>())
			{
				AE_CORE_WARN("Cannot parent entity '{}' to '{}': the parent has no transform", GetName(), parent.GetName());
				return;
			}

			// Reject cycles: walk up from the new parent and see if we meet ourselves.
			Uuid cursor = newParentUuid;
			for (int depth = 0; !cursor.IsNull() && depth < 4096; ++depth)
			{
				if (cursor == GetUuid())
				{
					AE_CORE_WARN("Refusing to parent entity '{}' to itself or to one of its descendants", GetName());
					return;
				}
				const Entity current = m_Scene->FindEntity(cursor);
				if (!current.IsValid() || !current.HasComponent<TransformComponent>())
				{
					break;
				}
				cursor = current.GetComponent<TransformComponent>().Parent;
			}
		}

		// Refresh cached world transforms first: the local transform is authoritative
		// and a stale world matrix would corrupt the relative transform below.
		m_Scene->UpdateTransforms();
		transform = TryGetComponent<TransformComponent>();
		if (transform == nullptr)
		{
			return;
		}

		// Preserve the current world transform across the reparenting.
		const glm::mat4 worldTransform = transform->GetWorldTransform();

		// Detach from the previous parent.
		Uuid oldParentUuid = transform->Parent;
		if (!oldParentUuid.IsNull())
		{
			Entity oldParent = m_Scene->FindEntity(oldParentUuid);
			if (oldParent.IsValid())
			{
				if (TransformComponent* oldParentTransform = oldParent.TryGetComponent<TransformComponent>())
				{
					auto& children = oldParentTransform->Children;
					const Uuid self = GetUuid();
					children.erase(std::remove(children.begin(), children.end(), self), children.end());
				}
			}
			transform->Parent = Uuid::Null();
		}

		transform->Parent = newParentUuid;
		glm::mat4 localTransform = worldTransform;
		if (!newParentUuid.IsNull())
		{
			TransformComponent& parentTransform = parent.GetComponent<TransformComponent>();
			parentTransform.Children.push_back(GetUuid());
			localTransform = glm::inverse(parentTransform.GetWorldTransform()) * worldTransform;
		}

		glm::vec3 position = glm::vec3(0.0f);
		glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 scale = glm::vec3(1.0f);
		glm::vec3 skew = glm::vec3(0.0f);
		glm::vec4 perspective = glm::vec4(0.0f);
		if (glm::decompose(localTransform, scale, rotation, position, skew, perspective))
		{
			transform->LocalPosition = position;
			transform->LocalRotation = glm::normalize(rotation);
			transform->LocalScale = scale;
		}
		else
		{
			AE_CORE_WARN("Could not decompose the local transform while reparenting '{}'", GetName());
		}

		transform->WorldPosition = position;
		transform->WorldRotation = transform->LocalRotation;
		transform->WorldScale = scale;
	}

	void Entity::ClearParent()
	{
		SetParent(Entity());
	}

	Entity Entity::GetParent() const
	{
		const TransformComponent* transform = TryGetComponent<TransformComponent>();
		if (transform == nullptr || transform->Parent.IsNull() || m_Scene == nullptr)
		{
			return Entity();
		}
		return m_Scene->FindEntity(transform->Parent);
	}

	std::vector<Entity> Entity::GetChildren() const
	{
		std::vector<Entity> children;
		const TransformComponent* transform = TryGetComponent<TransformComponent>();
		if (transform == nullptr || m_Scene == nullptr)
		{
			return children;
		}
		children.reserve(transform->Children.size());
		for (const Uuid& childUuid : transform->Children)
		{
			const Entity child = m_Scene->FindEntity(childUuid);
			if (child.IsValid())
			{
				children.push_back(child);
			}
		}
		return children;
	}

	bool Entity::operator==(const Entity& other) const
	{
		return m_Handle == other.m_Handle && m_Scene == other.m_Scene;
	}
}
