#pragma once

#include "Aegis/Core/Base.h"
#include "Aegis/Core/Json.h"
#include "Aegis/Core/Time.h"
#include "Aegis/Core/Uuid.h"
#include "Aegis/ECS/Registry.h"
#include "Aegis/Scene/Components.h"
#include "Aegis/Scene/Entity.h"
#include "Aegis/Scene/SceneSystem.h"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aegis
{
	// Renderer and post-process settings that belong to a scene rather than to an
	// entity. Serialised with the scene.
	struct EnvironmentSettings
	{
		glm::vec3 AmbientColor{ 0.06f, 0.07f, 0.09f };
		f32 AmbientIntensity = 1.0f;
		std::string HdriPath;
		f32 HdriIntensity = 1.0f;
		f32 Exposure = 1.0f;

		bool EnableIBL = true;
		bool EnableShadows = true;
		bool EnableSSAO = true;
		bool EnableBloom = true;
		bool EnableFXAA = true;

		f32 ShadowDistance = 60.0f;
		f32 SSAORadius = 0.6f;
		f32 BloomThreshold = 1.1f;
		f32 BloomIntensity = 0.35f;
	};

	class Scene
	{
	public:
		Scene();
		explicit Scene(std::string name);
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		const Uuid& GetUuid() const { return m_Uuid; }
		// Loading a persisted scene restores its identity; new scenes generate one.
		void SetUuid(const Uuid& uuid) { m_Uuid = uuid; }
		const std::string& GetName() const { return m_Name; }
		void SetName(std::string name) { m_Name = std::move(name); }

		Entity CreateEntity(const std::string& name = "Entity");
		Entity CreateEntity(const Uuid& uuid, const std::string& name);
		void DestroyEntity(Entity entity);

		Entity FindEntity(const Uuid& uuid) const;
		Entity FindEntity(std::string_view name) const;
		std::vector<Entity> GetEntities() const;
		usize GetEntityCount() const { return m_Registry.GetEntityCount(); }
		bool IsEmpty() const { return m_Registry.GetEntityCount() == 0; }
		void Clear();

		Entity GetPrimaryCamera() const;
		Entity EnsurePrimaryCamera();

		// Runs the transform hierarchy, then every registered system (physics,
		// scripting, audio) and refreshes world transforms once more.
		void OnUpdate(Timestep timestep);
		void UpdateTransforms();

		EnvironmentSettings& GetEnvironment() { return m_Environment; }
		const EnvironmentSettings& GetEnvironment() const { return m_Environment; }
		void SetEnvironment(const EnvironmentSettings& environment) { m_Environment = environment; }

		ecs::Registry& GetRegistry() { return m_Registry; }
		const ecs::Registry& GetRegistry() const { return m_Registry; }

		void AddSystem(SceneSystem* system);
		void RemoveSystem(SceneSystem* system);
		const std::vector<SceneSystem*>& GetSystems() const { return m_Systems; }

	private:
		friend class Entity;
		void registerEntity(ecs::EntityHandle handle, const Uuid& uuid);
		void unregisterEntity(ecs::EntityHandle handle, const Uuid& uuid);

		Uuid m_Uuid;
		std::string m_Name;
		ecs::Registry m_Registry;
		std::unordered_map<Uuid, ecs::EntityHandle, UuidHash> m_UuidToEntity;
		EnvironmentSettings m_Environment;
		std::vector<SceneSystem*> m_Systems;
	};
}
