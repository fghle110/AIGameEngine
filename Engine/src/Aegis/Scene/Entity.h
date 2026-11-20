#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/Uuid.h"
#include "Aegis/ECS/Registry.h"

#include <string>
#include <string_view>

namespace aegis
{
	namespace ecs
	{
		class Registry;
	}

	class Scene;

	// Lightweight, copyable handle to an entity inside a Scene. The handle is
	// validated on every structural operation, so destroyed entities cannot be
	// resurrected by stale handles.
	class Entity
	{
	public:
		Entity() = default;
		Entity(ecs::EntityHandle handle, Scene* scene);

		bool IsValid() const;
		explicit operator bool() const { return IsValid(); }

		Uuid GetUuid() const;
		const std::string& GetName() const;
		void SetName(const std::string& name);

		bool IsActive() const;
		void SetActive(bool active);

		Scene* GetScene() const { return m_Scene; }
		ecs::EntityHandle GetHandle() const { return m_Handle; }

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args);

		template<typename T>
		bool HasComponent() const;

		template<typename T>
		T& GetComponent();

		template<typename T>
		const T& GetComponent() const;

		template<typename T>
		T* TryGetComponent();

		template<typename T>
		const T* TryGetComponent() const;

		template<typename T>
		void RemoveComponent();

		// Hierarchy. `SetParent` also updates both transforms' UUID lists.
		void SetParent(Entity parent);
		void ClearParent();
		Entity GetParent() const;
		std::vector<Entity> GetChildren() const;

		bool operator==(const Entity& other) const;
		bool operator!=(const Entity& other) const { return !(*this == other); }

		ecs::Registry& GetRegistry() const;

	private:
		ecs::EntityHandle m_Handle;
		Scene* m_Scene = nullptr;
	};

	template<typename T, typename... Args>
	T& Entity::AddComponent(Args&&... args)
	{
		AE_ASSERT(IsValid(), "Cannot add a component to an invalid entity");
		return GetRegistry().AddComponent<T>(m_Handle, std::forward<Args>(args)...);
	}

	template<typename T>
	bool Entity::HasComponent() const
	{
		return IsValid() && GetRegistry().HasComponent<T>(m_Handle);
	}

	template<typename T>
	T& Entity::GetComponent()
	{
		AE_ASSERT(IsValid(), "Cannot get a component from an invalid entity");
		return GetRegistry().GetComponent<T>(m_Handle);
	}

	template<typename T>
	const T& Entity::GetComponent() const
	{
		AE_ASSERT(IsValid(), "Cannot get a component from an invalid entity");
		return GetRegistry().GetComponent<T>(m_Handle);
	}

	template<typename T>
	T* Entity::TryGetComponent()
	{
		return IsValid() ? GetRegistry().TryGetComponent<T>(m_Handle) : nullptr;
	}

	template<typename T>
	const T* Entity::TryGetComponent() const
	{
		return IsValid() ? GetRegistry().TryGetComponent<T>(m_Handle) : nullptr;
	}

	template<typename T>
	void Entity::RemoveComponent()
	{
		if (IsValid())
		{
			GetRegistry().RemoveComponent<T>(m_Handle);
		}
	}
}
