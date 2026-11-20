#include "Aegis/Scene/Scene.h"

#include "Aegis/Core/Log.h"

#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace aegis
{
	Scene::Scene()
		: Scene("Untitled Scene")
	{
	}

	Scene::Scene(std::string name)
		: m_Uuid(Uuid::Generate())
		, m_Name(std::move(name))
	{
	}

	Scene::~Scene() = default;

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntity(Uuid::Generate(), name);
	}

	Entity Scene::CreateEntity(const Uuid& uuid, const std::string& name)
	{
		AE_ASSERT(!uuid.IsNull(), "Entities must have a valid UUID");
		AE_ASSERT(m_UuidToEntity.find(uuid) == m_UuidToEntity.end(), "Duplicate entity UUID {}",
			uuid.ToString());

		const ecs::EntityHandle handle = m_Registry.CreateEntity();
		m_Registry.AddComponent<IdComponent>(handle, IdComponent{ uuid });
		m_Registry.AddComponent<TagComponent>(handle, TagComponent{ name });
		m_Registry.AddComponent<TransformComponent>(handle, TransformComponent{});
		m_Registry.AddComponent<ActiveComponent>(handle, ActiveComponent{});
		m_UuidToEntity.emplace(uuid, handle);

		return Entity(handle, this);
	}

	void Scene::DestroyEntity(Entity entity)
	{
		if (!entity.IsValid() || entity.GetScene() != this)
		{
			return;
		}

		// Destroy descendants first so no child is left with a dangling parent.
		const Uuid uuid = entity.GetUuid();
		if (const TransformComponent* transform = entity.TryGetComponent<TransformComponent>())
		{
			const std::vector<Uuid> children = transform->Children;
			for (const Uuid& childUuid : children)
			{
				Entity child = FindEntity(childUuid);
				if (child.IsValid())
				{
					DestroyEntity(child);
				}
			}
		}

		if (const TransformComponent* transform = entity.TryGetComponent<TransformComponent>())
		{
			if (!transform->Parent.IsNull())
			{
				Entity parent = FindEntity(transform->Parent);
				if (parent.IsValid())
				{
					if (TransformComponent* parentTransform = parent.TryGetComponent<TransformComponent>())
					{
						auto& children = parentTransform->Children;
						children.erase(std::remove(children.begin(), children.end(), uuid), children.end());
					}
				}
			}
		}

		unregisterEntity(entity.GetHandle(), uuid);
		m_Registry.DestroyEntity(entity.GetHandle());
	}

	Entity Scene::FindEntity(const Uuid& uuid) const
	{
		const auto it = m_UuidToEntity.find(uuid);
		if (it == m_UuidToEntity.end())
		{
			return Entity();
		}
		return Entity(it->second, const_cast<Scene*>(this));
	}

	Entity Scene::FindEntity(std::string_view name) const
	{
		Entity result;
		ecs::Registry& registry = const_cast<ecs::Registry&>(m_Registry);
		registry.Each<TagComponent>([&](ecs::EntityHandle handle, TagComponent& tag)
		{
			if (result.IsValid())
			{
				return;
			}
			if (tag.Tag == name)
			{
				result = Entity(handle, const_cast<Scene*>(this));
			}
		});
		return result;
	}

	std::vector<Entity> Scene::GetEntities() const
	{
		std::vector<Entity> entities;
		entities.reserve(m_Registry.GetEntityCount());
		ecs::Registry& registry = const_cast<ecs::Registry&>(m_Registry);
		registry.Each<IdComponent>([&](ecs::EntityHandle handle, IdComponent&)
		{
			entities.emplace_back(handle, const_cast<Scene*>(this));
		});
		return entities;
	}

	void Scene::Clear()
	{
		m_Registry.Clear();
		m_UuidToEntity.clear();
	}

	Entity Scene::GetPrimaryCamera() const
	{
		Entity primary;
		Entity firstCamera;
		ecs::Registry& registry = const_cast<ecs::Registry&>(m_Registry);
		registry.Each<CameraComponent>([&](ecs::EntityHandle handle, CameraComponent& camera)
		{
			Entity entity(handle, const_cast<Scene*>(this));
			if (!entity.IsActive())
			{
				return;
			}
			if (!firstCamera.IsValid())
			{
				firstCamera = entity;
			}
			if (camera.Primary)
			{
				primary = entity;
			}
		});
		return primary.IsValid() ? primary : firstCamera;
	}

	Entity Scene::EnsurePrimaryCamera()
	{
		Entity camera = GetPrimaryCamera();
		if (camera.IsValid())
		{
			camera.GetComponent<CameraComponent>().Primary = true;
			return camera;
		}

		camera = CreateEntity("Camera");
		CameraComponent cameraComponent;
		cameraComponent.Primary = true;
		camera.AddComponent<CameraComponent>(cameraComponent);

		TransformComponent& transform = camera.GetComponent<TransformComponent>();
		transform.LocalPosition = glm::vec3(0.0f, 2.0f, 8.0f);
		transform.SetLocalEulerAngles(glm::vec3(-10.0f, 0.0f, 0.0f));
		return camera;
	}

	void Scene::OnUpdate(Timestep timestep)
	{
		UpdateTransforms();
		for (SceneSystem* system : m_Systems)
		{
			if (system != nullptr)
			{
				system->OnUpdate(*this, timestep);
			}
		}
		UpdateTransforms();
	}

	void Scene::UpdateTransforms()
	{
		std::unordered_set<Uuid, UuidHash> visited;
		visited.reserve(m_Registry.GetEntityCapacity());

		std::function<void(Entity, const glm::mat4&)> updateHierarchy = [&](Entity entity,
			const glm::mat4& parentWorld)
		{
			const Uuid uuid = entity.GetUuid();
			if (!visited.insert(uuid).second)
			{
				return;
			}

			TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
			if (transform == nullptr)
			{
				return;
			}

			const glm::mat4 world = parentWorld * transform->GetLocalTransform();

			glm::vec3 position = glm::vec3(0.0f);
			glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			glm::vec3 scale = glm::vec3(1.0f);
			glm::vec3 skew = glm::vec3(0.0f);
			glm::vec4 perspective = glm::vec4(0.0f);
			if (glm::decompose(world, scale, rotation, position, skew, perspective))
			{
				transform->WorldPosition = position;
				transform->WorldRotation = glm::normalize(rotation);
				transform->WorldScale = scale;
			}
			else
			{
				transform->WorldPosition = glm::vec3(world[3]);
				transform->WorldRotation = glm::normalize(transform->LocalRotation);
				transform->WorldScale = transform->LocalScale;
			}

			// Copy the child list: recursion may not mutate it, but a defensive copy
			// keeps the loop robust if a system edits the hierarchy later.
			const std::vector<Uuid> children = transform->Children;
			for (const Uuid& childUuid : children)
			{
				Entity child = FindEntity(childUuid);
				if (child.IsValid())
				{
					updateHierarchy(child, world);
				}
			}
		};

		const glm::mat4 identity(1.0f);
		for (Entity entity : GetEntities())
		{
			const TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
			if (transform == nullptr)
			{
				continue;
			}
			if (!transform->Parent.IsNull())
			{
				Entity parent = FindEntity(transform->Parent);
				if (parent.IsValid() && parent.HasComponent<TransformComponent>())
				{
					continue; // handled by the parent traversal
				}
			}
			updateHierarchy(entity, identity);
		}
	}

	void Scene::AddSystem(SceneSystem* system)
	{
		if (system == nullptr)
		{
			return;
		}
		const auto existing = std::find(m_Systems.begin(), m_Systems.end(), system);
		if (existing != m_Systems.end())
		{
			return;
		}
		m_Systems.push_back(system);
		std::stable_sort(m_Systems.begin(), m_Systems.end(), [](SceneSystem* a, SceneSystem* b)
		{
			return a->GetUpdateOrder() < b->GetUpdateOrder();
		});
	}

	void Scene::RemoveSystem(SceneSystem* system)
	{
		m_Systems.erase(std::remove(m_Systems.begin(), m_Systems.end(), system), m_Systems.end());
	}

	void Scene::registerEntity(ecs::EntityHandle handle, const Uuid& uuid)
	{
		m_UuidToEntity[uuid] = handle;
	}

	void Scene::unregisterEntity(ecs::EntityHandle handle, const Uuid& uuid)
	{
		const auto it = m_UuidToEntity.find(uuid);
		if (it != m_UuidToEntity.end() && it->second == handle)
		{
			m_UuidToEntity.erase(it);
		}
	}
}
