#pragma once

// A compact sparse-set ECS registry. Entities are index/generation handles;
// components live in contiguous dense arrays per type, which keeps iteration
// fast and stable while supporting the small entity edits an editor performs.

#include "Aegis/Core/Base.h"

#include <typeindex>
#include <unordered_map>
#include <vector>

namespace aegis::ecs
{
	inline constexpr u32 c_InvalidEntityIndex = 0xFFFFFFFFu;

	struct EntityHandle
	{
		u32 Index = c_InvalidEntityIndex;
		u32 Generation = 0;

		bool IsValid() const { return Index != c_InvalidEntityIndex; }
		bool operator==(const EntityHandle& other) const = default;
	};

	struct EntityHandleHash
	{
		usize operator()(const EntityHandle& handle) const noexcept
		{
			return (static_cast<usize>(handle.Generation) << 32) ^ static_cast<usize>(handle.Index);
		}
	};

	// Dense storage for one component type.
	template<typename T>
	class ComponentPool
	{
	public:
		void Add(u32 entityIndex, T component)
		{
			if (entityIndex >= m_Sparse.size())
			{
				m_Sparse.resize(static_cast<usize>(entityIndex) + 1, 0);
			}
			AE_ASSERT(m_Sparse[entityIndex] == 0, "Component already exists for entity index {}", entityIndex);

			m_Sparse[entityIndex] = static_cast<u32>(m_DenseEntities.size()) + 1;
			m_DenseEntities.push_back(entityIndex);
			m_DenseComponents.push_back(std::move(component));
		}

		void Remove(u32 entityIndex)
		{
			if (entityIndex >= m_Sparse.size() || m_Sparse[entityIndex] == 0)
			{
				return;
			}
			const u32 denseIndex = m_Sparse[entityIndex] - 1;
			const u32 lastDense = static_cast<u32>(m_DenseEntities.size()) - 1;
			if (denseIndex != lastDense)
			{
				m_DenseEntities[denseIndex] = m_DenseEntities[lastDense];
				m_DenseComponents[denseIndex] = std::move(m_DenseComponents[lastDense]);
				m_Sparse[m_DenseEntities[denseIndex]] = denseIndex + 1;
			}
			m_DenseEntities.pop_back();
			m_DenseComponents.pop_back();
			m_Sparse[entityIndex] = 0;
		}

		bool Has(u32 entityIndex) const
		{
			return entityIndex < m_Sparse.size() && m_Sparse[entityIndex] != 0;
		}

		T* TryGet(u32 entityIndex)
		{
			return Has(entityIndex) ? &m_DenseComponents[m_Sparse[entityIndex] - 1] : nullptr;
		}

		const T* TryGet(u32 entityIndex) const
		{
			return Has(entityIndex) ? &m_DenseComponents[m_Sparse[entityIndex] - 1] : nullptr;
		}

		T& Get(u32 entityIndex)
		{
			T* component = TryGet(entityIndex);
			AE_ASSERT(component != nullptr, "Component does not exist for entity index {}", entityIndex);
			return *component;
		}

		const T& Get(u32 entityIndex) const
		{
			const T* component = TryGet(entityIndex);
			AE_ASSERT(component != nullptr, "Component does not exist for entity index {}", entityIndex);
			return *component;
		}

		usize Size() const { return m_DenseComponents.size(); }
		bool IsEmpty() const { return m_DenseComponents.empty(); }

		u32 EntityIndexAt(usize denseIndex) const { return m_DenseEntities[denseIndex]; }
		T& ComponentAt(usize denseIndex) { return m_DenseComponents[denseIndex]; }
		const T& ComponentAt(usize denseIndex) const { return m_DenseComponents[denseIndex]; }

		void Clear()
		{
			m_Sparse.clear();
			m_DenseEntities.clear();
			m_DenseComponents.clear();
		}

	private:
		// sparse[entityIndex] == denseIndex + 1 (0 means "no component").
		std::vector<u32> m_Sparse;
		std::vector<u32> m_DenseEntities;
		std::vector<T> m_DenseComponents;
	};

	class IComponentStorage
	{
	public:
		virtual ~IComponentStorage() = default;
		virtual void Remove(u32 entityIndex) = 0;
		virtual usize Size() const = 0;
		virtual void Clear() = 0;
	};

	template<typename T>
	class ComponentStorage final : public IComponentStorage
	{
	public:
		void Remove(u32 entityIndex) override { m_Pool.Remove(entityIndex); }
		usize Size() const override { return m_Pool.Size(); }
		void Clear() override { m_Pool.Clear(); }

		ComponentPool<T> m_Pool;
	};

	class Registry
	{
	public:
		Registry() = default;
		~Registry() = default;

		Registry(const Registry&) = delete;
		Registry& operator=(const Registry&) = delete;

		EntityHandle CreateEntity()
		{
			u32 index;
			if (!m_FreeIndices.empty())
			{
				index = m_FreeIndices.back();
				m_FreeIndices.pop_back();
			}
			else
			{
				index = static_cast<u32>(m_Generations.size());
				m_Generations.push_back(0);
				m_Alive.push_back(false);
			}

			// Bumping the generation invalidates stale handles from a previous life.
			++m_Generations[index];
			m_Alive[index] = true;
			++m_EntityCount;

			return { index, m_Generations[index] };
		}

		void DestroyEntity(EntityHandle handle)
		{
			if (!IsAlive(handle))
			{
				return;
			}
			for (auto& [type, storage] : m_Storages)
			{
				storage->Remove(handle.Index);
			}
			m_Alive[handle.Index] = false;
			m_FreeIndices.push_back(handle.Index);
			--m_EntityCount;
		}

		bool IsAlive(EntityHandle handle) const
		{
			return handle.IsValid() && handle.Index < m_Generations.size() && m_Alive[handle.Index] &&
				   m_Generations[handle.Index] == handle.Generation;
		}

		usize GetEntityCount() const { return m_EntityCount; }
		usize GetEntityCapacity() const { return m_Generations.size(); }

		void Clear()
		{
			for (auto& [type, storage] : m_Storages)
			{
				storage->Clear();
			}
			m_Generations.clear();
			m_Alive.clear();
			m_FreeIndices.clear();
			m_EntityCount = 0;
		}

		template<typename T, typename... Args>
		T& AddComponent(EntityHandle handle, Args&&... args)
		{
			AE_ASSERT(IsAlive(handle), "Cannot add a component to a dead entity");
			ComponentPool<T>& pool = GetPool<T>();
			pool.Add(handle.Index, T{ std::forward<Args>(args)... });
			return pool.Get(handle.Index);
		}

		template<typename T>
		bool HasComponent(EntityHandle handle) const
		{
			const ComponentPool<T>* pool = FindPool<T>();
			return pool != nullptr && pool->Has(handle.Index);
		}

		template<typename T>
		T* TryGetComponent(EntityHandle handle)
		{
			ComponentPool<T>& pool = GetPool<T>();
			return pool.TryGet(handle.Index);
		}

		template<typename T>
		const T* TryGetComponent(EntityHandle handle) const
		{
			const ComponentPool<T>* pool = FindPool<T>();
			return pool != nullptr ? pool->TryGet(handle.Index) : nullptr;
		}

		template<typename T>
		T& GetComponent(EntityHandle handle)
		{
			return GetPool<T>().Get(handle.Index);
		}

		template<typename T>
		const T& GetComponent(EntityHandle handle) const
		{
			return GetPool<T>().Get(handle.Index);
		}

		template<typename T>
		void RemoveComponent(EntityHandle handle)
		{
			ComponentPool<T>* pool = FindPool<T>();
			if (pool != nullptr)
			{
				pool->Remove(handle.Index);
			}
		}

		template<typename T>
		ComponentPool<T>& GetPool()
		{
			const std::type_index type(typeid(T));
			auto it = m_Storages.find(type);
			if (it == m_Storages.end())
			{
				auto storage = CreateScope<ComponentStorage<T>>();
				auto* raw = storage.get();
				m_Storages.emplace(type, std::move(storage));
				return raw->m_Pool;
			}
			return static_cast<ComponentStorage<T>*>(it->second.get())->m_Pool;
		}

		template<typename T>
		const ComponentPool<T>& GetPool() const
		{
			const ComponentPool<T>* pool = FindPool<T>();
			AE_ASSERT(pool != nullptr, "Component pool was not created");
			return *pool;
		}

		template<typename T>
		usize GetComponentCount() const
		{
			const ComponentPool<T>* pool = FindPool<T>();
			return pool != nullptr ? pool->Size() : 0;
		}

		// Calls func(EntityHandle, First&, Rest&...) for every entity that has all
		// listed components. The pool of `First` drives the iteration, so put the
		// rarest component first. Structural changes during iteration are not
		// allowed.
		template<typename First, typename... Rest, typename F>
		void Each(F&& func)
		{
			ComponentPool<First>& pool = GetPool<First>();
			const usize count = pool.Size();
			for (usize i = 0; i < count; ++i)
			{
				const u32 index = pool.EntityIndexAt(i);
				const EntityHandle handle{ index, m_Generations[index] };
				if (((HasComponent<Rest>(handle)) && ...))
				{
					func(handle, pool.ComponentAt(i), GetComponent<Rest>(handle)...);
				}
			}
		}

		template<typename T>
		usize Count() const
		{
			return GetComponentCount<T>();
		}

	private:
		template<typename T>
		ComponentPool<T>* FindPool()
		{
			const auto it = m_Storages.find(std::type_index(typeid(T)));
			return it == m_Storages.end() ? nullptr : &static_cast<ComponentStorage<T>*>(it->second.get())->m_Pool;
		}

		template<typename T>
		const ComponentPool<T>* FindPool() const
		{
			const auto it = m_Storages.find(std::type_index(typeid(T)));
			return it == m_Storages.end() ? nullptr : &static_cast<const ComponentStorage<T>*>(it->second.get())->m_Pool;
		}

		std::unordered_map<std::type_index, Scope<IComponentStorage>> m_Storages;
		std::vector<u32> m_Generations;
		std::vector<bool> m_Alive;
		std::vector<u32> m_FreeIndices;
		usize m_EntityCount = 0;
	};
}
