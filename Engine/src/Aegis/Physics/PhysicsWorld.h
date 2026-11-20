#pragma once

// Rigid-body simulation for the scene, backed by Jolt Physics.
//
// Jolt is linked PRIVATE into the engine library, so this header deliberately
// exposes no Jolt type: the simulation lives behind the pimpl at the bottom of
// the class. Engine and game code can include it without Jolt's include
// directories on the path.

#include "Aegis/Core/Base.h"
#include "Aegis/Core/Time.h"
#include "Aegis/Core/Uuid.h"
#include "Aegis/Scene/Entity.h"
#include "Aegis/Scene/SceneSystem.h"

#include <glm/glm.hpp>

#include <functional>
#include <string_view>

namespace aegis
{
class Scene;

// Keeps the simulation in sync with the scene: every active entity that has a
// TransformComponent, a RigidBodyComponent and a ColliderComponent owns one
// Jolt body. Register it with Scene::AddSystem; update order -100 runs it
// before scripting and audio.
class PhysicsWorld final : public SceneSystem
{
public:
	// Reports the two entities that started overlapping through a trigger
	// collider. Called on the main thread after the step that detected the
	// overlap, never from a physics worker thread.
	using TriggerCallback = std::function<void(Entity, Entity)>;

	PhysicsWorld();
	~PhysicsWorld() override;

	PhysicsWorld(const PhysicsWorld&) = delete;
	PhysicsWorld& operator=(const PhysicsWorld&) = delete;

	// Installs the global Jolt registration on first use and creates the
	// simulation. Idempotent: an initialized world is left untouched and true
	// is returned. Returns false only when the world cannot be created.
	bool Initialize(const glm::vec3& gravity = glm::vec3(0.0f, -9.81f, 0.0f));
	void Shutdown();
	[[nodiscard]] bool IsInitialized() const;

	// Advances the simulation by one frame, split into at most four collision
	// steps when the frame is longer than 1/60 s. Does nothing before
	// Initialize() or when the timestep is not positive. Trigger overlaps found
	// by a standalone step are discarded; use OnUpdate to receive them.
	void Step(Timestep timestep);

	// Reconciles the bodies with the scene, steps the world and writes the
	// dynamic bodies back into the scene's local transforms.
	void OnUpdate(Scene& scene, Timestep timestep) override;

	// Destroys every body and recreates it from the current scene state. Use it
	// after teleporting a dynamic body or changing a collider outside its
	// component fields.
	void Rebuild(Scene& scene);

	[[nodiscard]] std::string_view GetName() const override;
	[[nodiscard]] i32 GetUpdateOrder() const override;

	// Closest hit along a ray. The direction does not need to be normalized;
	// maxDistance limits the query. Returns false on a miss, before
	// Initialize(), for a zero direction or distance, and never reports a UUID
	// that no longer belongs to an entity in the scene.
	bool Raycast(Scene& scene,
	             const glm::vec3& origin,
	             const glm::vec3& direction,
	             f32 maxDistance,
	             Uuid& outEntity,
	             glm::vec3& outHitPoint) const;

	// Linear velocity of the body that belongs to entity, in world space.
	// Returns false when the entity has no body in this world.
	bool GetLinearVelocity(const Uuid& entity, glm::vec3& outVelocity) const;

	void SetGravity(const glm::vec3& gravity);
	[[nodiscard]] glm::vec3 GetGravity() const;

	void SetTriggerCallback(TriggerCallback callback);

	// Number of bodies currently owned by the world.
	[[nodiscard]] usize GetBodyCount() const;

private:
	struct Impl;

	Scope<Impl> m_Impl;
	glm::vec3 m_Gravity{ 0.0f, -9.81f, 0.0f };
	TriggerCallback m_TriggerCallback;
};
} // namespace aegis
