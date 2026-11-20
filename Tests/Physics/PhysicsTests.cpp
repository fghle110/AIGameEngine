#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Physics/PhysicsWorld.h"
#include "Aegis/Scene/Scene.h"

#include <glm/glm.hpp>

#include <cmath>
#include <string>
#include <thread>
#include <vector>

using namespace aegis;

namespace
{
constexpr float c_FixedStep = 1.0f / 60.0f;

// Owns a scene with the physics world registered as a scene system, so every
// test drives the real Scene::OnUpdate path (transforms, systems, transforms).
struct PhysicsScene
{
	Scene Scene;
	PhysicsWorld Physics;

	PhysicsScene()
	{
		Scene.AddSystem(&Physics);
	}

	~PhysicsScene()
	{
		Scene.RemoveSystem(&Physics);
	}
};

Entity
CreateBox(Scene& scene, const std::string& name, BodyType type, const glm::vec3& position, const glm::vec3& halfExtents)
{
	Entity entity = scene.CreateEntity(name);
	entity.GetComponent<TransformComponent>().LocalPosition = position;

	RigidBodyComponent& rigidBody = entity.AddComponent<RigidBodyComponent>();
	rigidBody.Type = type;

	ColliderComponent& collider = entity.AddComponent<ColliderComponent>();
	collider.Shape = ColliderShape::Box;
	collider.HalfExtents = halfExtents;
	return entity;
}

// 20 x 1 x 20 slab whose top face sits at topY.
Entity CreateFloor(Scene& scene, float topY = 0.0f)
{
	const glm::vec3 halfExtents(10.0f, 0.5f, 10.0f);
	return CreateBox(scene, "Floor", BodyType::Static, glm::vec3(0.0f, topY - halfExtents.y, 0.0f), halfExtents);
}

void Simulate(Scene& scene, int steps)
{
	for (int step = 0; step < steps; ++step)
	{
		scene.OnUpdate(Timestep(c_FixedStep));
	}
}

// Builds the same scene twice from scratch and returns the simulated positions
// after 60 fixed steps.
std::vector<glm::vec3> RunDeterminismScene()
{
	Scene scene("Determinism");
	PhysicsWorld physics;
	REQUIRE(physics.Initialize());
	scene.AddSystem(&physics);

	CreateFloor(scene);

	std::vector<Entity> bodies;
	for (int index = 0; index < 3; ++index)
	{
		const float offset = static_cast<float>(index);
		Entity body = CreateBox(scene,
		                        "Falling " + std::to_string(index),
		                        BodyType::Dynamic,
		                        glm::vec3(offset * 1.25f, 3.0f + offset * 0.5f, 0.0f),
		                        glm::vec3(0.4f));
		body.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(0.0f, offset * 25.0f, 0.0f));
		bodies.push_back(body);
	}

	Simulate(scene, 60);

	std::vector<glm::vec3> positions;
	positions.reserve(bodies.size());
	for (Entity body : bodies)
	{
		positions.push_back(body.GetComponent<TransformComponent>().LocalPosition);
	}
	return positions;
}
} // namespace

TEST_CASE("Physics: initialization exposes the documented system contract")
{
	PhysicsWorld world;
	CHECK_FALSE(world.IsInitialized());
	CHECK(world.GetName() == "Physics");
	CHECK(world.GetUpdateOrder() == -100);
	CHECK(world.GetBodyCount() == 0);

	// An uninitialized world ignores every operation instead of touching Jolt.
	Scene scene("Scene");
	Uuid hitEntity = Uuid::Generate();
	glm::vec3 hitPoint(1.0f, 2.0f, 3.0f);
	CHECK_FALSE(
	    world.Raycast(scene, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 10.0f, hitEntity, hitPoint));
	CHECK(hitEntity.IsNull());
	CHECK(hitPoint == glm::vec3(0.0f, 1.0f, 0.0f));
	world.Step(Timestep(c_FixedStep));
	world.OnUpdate(scene, Timestep(c_FixedStep));
	world.Rebuild(scene);
	glm::vec3 velocity(1.0f, 1.0f, 1.0f);
	CHECK_FALSE(world.GetLinearVelocity(Uuid::Generate(), velocity));
	CHECK(velocity == glm::vec3(0.0f));
	world.Shutdown();
	CHECK_FALSE(world.IsInitialized());

	REQUIRE(world.Initialize());
	CHECK(world.IsInitialized());
	CHECK(world.GetBodyCount() == 0);
	CHECK(world.GetGravity().y == doctest::Approx(-9.81f));

	// Idempotent: a second call keeps the live world.
	CHECK(world.Initialize());
	CHECK(world.IsInitialized());

	world.SetGravity(glm::vec3(0.0f, -3.5f, 0.0f));
	CHECK(world.GetGravity().y == doctest::Approx(-3.5f));

	world.Shutdown();
	CHECK_FALSE(world.IsInitialized());
	CHECK(world.GetBodyCount() == 0);
	// Gravity survives Shutdown so a world can be reconfigured before Initialize.
	CHECK(world.GetGravity().y == doctest::Approx(-3.5f));

	// Jolt's global registration is reference counted, so a world can restart.
	REQUIRE(world.Initialize());
	CHECK(world.IsInitialized());
	CHECK(world.GetGravity().y == doctest::Approx(-9.81f));
	world.Shutdown();
	CHECK_FALSE(world.IsInitialized());
}

TEST_CASE("Physics: a dynamic box falls and comes to rest on the static floor")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());

	CreateFloor(fixture.Scene);
	Entity box = CreateBox(fixture.Scene, "Box", BodyType::Dynamic, glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(0.5f));

	// One update reconciles the scene into bodies.
	Simulate(fixture.Scene, 1);
	REQUIRE(fixture.Physics.GetBodyCount() == 2);

	Simulate(fixture.Scene, 239);

	const TransformComponent& transform = box.GetComponent<TransformComponent>();
	CHECK(transform.LocalPosition.y < 1.0f);
	CHECK(transform.LocalPosition.y > 0.45f);
	CHECK(transform.LocalPosition.y < 0.55f);
	CHECK(std::abs(transform.LocalPosition.x) < 0.01f);
	CHECK(std::abs(transform.LocalPosition.z) < 0.01f);

	glm::vec3 velocity(1.0f);
	REQUIRE(fixture.Physics.GetLinearVelocity(box.GetUuid(), velocity));
	CHECK(glm::length(velocity) < 0.05f);

	// The box has settled: another 30 steps do not move it noticeably.
	const glm::vec3 settled = transform.LocalPosition;
	Simulate(fixture.Scene, 30);
	CHECK(glm::length(box.GetComponent<TransformComponent>().LocalPosition - settled) < 0.001f);
}

TEST_CASE("Physics: kinematic bodies ignore gravity and follow their transform")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());

	Entity platform =
	    CreateBox(fixture.Scene, "Platform", BodyType::Kinematic, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f));

	Simulate(fixture.Scene, 60);
	CHECK(platform.GetComponent<TransformComponent>().LocalPosition.y == doctest::Approx(5.0f).epsilon(1e-5f));

	// Authored motion is pushed into the body before the next step.
	platform.GetComponent<TransformComponent>().LocalPosition = glm::vec3(3.0f, 5.0f, 0.0f);
	Simulate(fixture.Scene, 1);

	Uuid hitEntity;
	glm::vec3 hitPoint(0.0f);
	REQUIRE(fixture.Physics.Raycast(fixture.Scene,
	                                glm::vec3(3.0f, 10.0f, 0.0f),
	                                glm::vec3(0.0f, -1.0f, 0.0f),
	                                20.0f,
	                                hitEntity,
	                                hitPoint));
	CHECK(hitEntity == platform.GetUuid());
	CHECK(hitPoint.y == doctest::Approx(6.0f).epsilon(1e-4f));
}

TEST_CASE("Physics: destroying entities and removing components destroys bodies")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());

	Entity floorEntity = CreateFloor(fixture.Scene);
	Entity box = CreateBox(fixture.Scene, "Box", BodyType::Dynamic, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.5f));

	Simulate(fixture.Scene, 5);
	REQUIRE(fixture.Physics.GetBodyCount() == 2);

	fixture.Scene.DestroyEntity(box);
	Simulate(fixture.Scene, 1);
	CHECK(fixture.Physics.GetBodyCount() == 1);
	CHECK(fixture.Physics.IsInitialized());

	fixture.Scene.DestroyEntity(floorEntity);
	Simulate(fixture.Scene, 1);
	CHECK(fixture.Physics.GetBodyCount() == 0);
	CHECK(fixture.Physics.IsInitialized());

	// Removing one component is enough to retire the body.
	Entity sphere = fixture.Scene.CreateEntity("Sphere");
	sphere.GetComponent<TransformComponent>().LocalPosition = glm::vec3(0.0f, 1.0f, 0.0f);
	sphere.AddComponent<RigidBodyComponent>();
	sphere.AddComponent<ColliderComponent>();
	Simulate(fixture.Scene, 1);
	REQUIRE(fixture.Physics.GetBodyCount() == 1);

	sphere.RemoveComponent<ColliderComponent>();
	Simulate(fixture.Scene, 1);
	CHECK(fixture.Physics.GetBodyCount() == 0);

	// A world without bodies must keep stepping without errors.
	Simulate(fixture.Scene, 10);
	CHECK(fixture.Physics.IsInitialized());
}

TEST_CASE("Physics: raycast reports the closest entity and the hit point")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());

	Entity floorEntity = CreateFloor(fixture.Scene);
	Entity wall = CreateBox(fixture.Scene, "Wall", BodyType::Static, glm::vec3(0.0f, 3.0f, -5.0f), glm::vec3(1.0f));
	Simulate(fixture.Scene, 1);

	Uuid hitEntity = Uuid::Generate();
	glm::vec3 hitPoint(0.0f);
	REQUIRE(fixture.Physics.Raycast(fixture.Scene,
	                                glm::vec3(0.0f, 5.0f, 0.0f),
	                                glm::vec3(0.0f, -1.0f, 0.0f),
	                                20.0f,
	                                hitEntity,
	                                hitPoint));
	CHECK(hitEntity == floorEntity.GetUuid());
	CHECK(std::abs(hitPoint.x) < 1e-3f);
	CHECK(std::abs(hitPoint.y) < 1e-3f);
	CHECK(std::abs(hitPoint.z) < 1e-3f);

	// Sideways ray hits the wall's front face at z = -4.
	REQUIRE(fixture.Physics.Raycast(fixture.Scene,
	                                glm::vec3(0.0f, 3.0f, 0.0f),
	                                glm::vec3(0.0f, 0.0f, -2.0f),
	                                10.0f,
	                                hitEntity,
	                                hitPoint));
	CHECK(hitEntity == wall.GetUuid());
	CHECK(hitPoint.z == doctest::Approx(-4.0f).epsilon(1e-4f));

	// maxDistance bounds the ray: the floor is five metres away.
	CHECK_FALSE(fixture.Physics.Raycast(fixture.Scene,
	                                    glm::vec3(0.0f, 5.0f, 0.0f),
	                                    glm::vec3(0.0f, -1.0f, 0.0f),
	                                    4.0f,
	                                    hitEntity,
	                                    hitPoint));
	CHECK(hitEntity.IsNull());

	// Missing and degenerate rays report no hit.
	CHECK_FALSE(fixture.Physics.Raycast(fixture.Scene,
	                                    glm::vec3(0.0f, 5.0f, 0.0f),
	                                    glm::vec3(0.0f, 1.0f, 0.0f),
	                                    10.0f,
	                                    hitEntity,
	                                    hitPoint));
	CHECK_FALSE(fixture.Physics.Raycast(fixture.Scene,
	                                    glm::vec3(0.0f, 5.0f, 0.0f),
	                                    glm::vec3(0.0f, 0.0f, 0.0f),
	                                    10.0f,
	                                    hitEntity,
	                                    hitPoint));
	CHECK_FALSE(fixture.Physics.Raycast(fixture.Scene,
	                                    glm::vec3(0.0f, 5.0f, 0.0f),
	                                    glm::vec3(0.0f, -1.0f, 0.0f),
	                                    0.0f,
	                                    hitEntity,
	                                    hitPoint));
}

TEST_CASE("Physics: trigger volumes report overlapping entities on the main thread")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());

	Entity trigger =
	    CreateBox(fixture.Scene, "Trigger", BodyType::Static, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f));
	trigger.GetComponent<ColliderComponent>().IsTrigger = true;

	Entity faller =
	    CreateBox(fixture.Scene, "Faller", BodyType::Dynamic, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.25f));

	const std::thread::id mainThread = std::this_thread::get_id();
	std::thread::id callbackThread;
	int triggerCount = 0;
	Uuid firstEntity;
	Uuid secondEntity;

	fixture.Physics.SetTriggerCallback(
	    [&](Entity first, Entity second)
	    {
		    ++triggerCount;
		    callbackThread = std::this_thread::get_id();
		    firstEntity = first.GetUuid();
		    secondEntity = second.GetUuid();
	    });

	Simulate(fixture.Scene, 180);

	REQUIRE(triggerCount > 0);
	CHECK(callbackThread == mainThread);
	CHECK(((firstEntity == trigger.GetUuid() && secondEntity == faller.GetUuid()) ||
	       (firstEntity == faller.GetUuid() && secondEntity == trigger.GetUuid())));

	// A trigger does not collide: the faller passes through the volume.
	CHECK(faller.GetComponent<TransformComponent>().LocalPosition.y < 0.0f);
}

TEST_CASE("Physics: identical scenes produce identical simulation results")
{
	const std::vector<glm::vec3> first = RunDeterminismScene();
	const std::vector<glm::vec3> second = RunDeterminismScene();

	REQUIRE(first.size() == 3);
	REQUIRE(second.size() == first.size());

	for (usize index = 0; index < first.size(); ++index)
	{
		CHECK(std::abs(first[index].x - second[index].x) <= 1e-4f);
		CHECK(std::abs(first[index].y - second[index].y) <= 1e-4f);
		CHECK(std::abs(first[index].z - second[index].z) <= 1e-4f);

		// The runs have to be real simulations, not untouched bodies.
		CHECK(first[index].y < 1.0f);
		CHECK(second[index].y < 1.0f);
	}
}

TEST_CASE("Physics: entities without a rigid body or collider are ignored")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());

	Entity bare = fixture.Scene.CreateEntity("Bare");
	Entity rigidOnly = fixture.Scene.CreateEntity("Rigid Only");
	rigidOnly.AddComponent<RigidBodyComponent>();
	Entity colliderOnly = fixture.Scene.CreateEntity("Collider Only");
	colliderOnly.AddComponent<ColliderComponent>();

	Entity inactive =
	    CreateBox(fixture.Scene, "Inactive", BodyType::Dynamic, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.5f));
	inactive.SetActive(false);

	Simulate(fixture.Scene, 30);

	CHECK(fixture.Physics.GetBodyCount() == 0);
	CHECK(bare.IsValid());
	CHECK(inactive.GetComponent<TransformComponent>().LocalPosition.y == doctest::Approx(3.0f).epsilon(1e-5f));

	// Activating the entity brings its body into the world on the next update.
	inactive.SetActive(true);
	Simulate(fixture.Scene, 1);
	CHECK(fixture.Physics.GetBodyCount() == 1);
}

TEST_CASE("Physics: static-only scenes stay stable for 120 steps")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());

	CreateFloor(fixture.Scene);
	Entity first = CreateBox(fixture.Scene, "First", BodyType::Static, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f));
	Entity second = CreateBox(fixture.Scene, "Second", BodyType::Static, glm::vec3(5.0f, 2.0f, 0.0f), glm::vec3(1.0f));

	Simulate(fixture.Scene, 1);
	REQUIRE(fixture.Physics.GetBodyCount() == 3);

	const glm::vec3 firstPosition = first.GetComponent<TransformComponent>().LocalPosition;
	const glm::vec3 secondPosition = second.GetComponent<TransformComponent>().LocalPosition;

	Simulate(fixture.Scene, 120);

	CHECK(fixture.Physics.IsInitialized());
	CHECK(fixture.Physics.GetBodyCount() == 3);
	CHECK(glm::length(first.GetComponent<TransformComponent>().LocalPosition - firstPosition) <= 1e-5f);
	CHECK(glm::length(second.GetComponent<TransformComponent>().LocalPosition - secondPosition) <= 1e-5f);

	// Static bodies still resolve rays after the long run.
	Uuid hitEntity;
	glm::vec3 hitPoint(0.0f);
	REQUIRE(fixture.Physics.Raycast(fixture.Scene,
	                                glm::vec3(0.0f, 10.0f, 0.0f),
	                                glm::vec3(0.0f, -1.0f, 0.0f),
	                                30.0f,
	                                hitEntity,
	                                hitPoint));
	CHECK(hitEntity == first.GetUuid());
	CHECK(hitPoint.y == doctest::Approx(3.0f).epsilon(1e-4f));
}

TEST_CASE("Physics: a dynamic body in a hierarchy keeps its parent relative transform")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());
	CreateFloor(fixture.Scene);

	Entity parent = fixture.Scene.CreateEntity("Parent");
	parent.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(0.0f, 90.0f, 0.0f));

	Entity child = fixture.Scene.CreateEntity("Child");
	child.SetParent(parent);
	child.GetComponent<TransformComponent>().LocalPosition = glm::vec3(2.0f, 5.0f, 0.0f);

	RigidBodyComponent& rigidBody = child.AddComponent<RigidBodyComponent>();
	rigidBody.Type = BodyType::Dynamic;
	child.AddComponent<ColliderComponent>().HalfExtents = glm::vec3(0.5f);

	// +90 degrees around Y sends the local +X offset to world -Z.
	Simulate(fixture.Scene, 1);
	REQUIRE(fixture.Physics.GetBodyCount() == 2);
	const glm::vec3 worldAtStart = child.GetComponent<TransformComponent>().WorldPosition;
	CHECK(std::abs(worldAtStart.x) < 1e-3f);
	CHECK(worldAtStart.z == doctest::Approx(-2.0f).epsilon(1e-3f));

	Simulate(fixture.Scene, 119);

	const TransformComponent& transform = child.GetComponent<TransformComponent>();
	CHECK(transform.LocalPosition.x == doctest::Approx(2.0f).epsilon(0.01f));
	CHECK(std::abs(transform.LocalPosition.y - 0.5f) < 0.05f);
	CHECK(std::abs(transform.LocalPosition.z) < 0.02f);
	CHECK(transform.WorldPosition.z == doctest::Approx(-2.0f).epsilon(0.01f));
}

TEST_CASE("Physics: changing the rigid body type rebuilds the body")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());
	CreateFloor(fixture.Scene);

	Entity box = CreateBox(fixture.Scene, "Box", BodyType::Static, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.5f));

	Simulate(fixture.Scene, 1);
	REQUIRE(fixture.Physics.GetBodyCount() == 2);

	// A static body must not fall.
	Simulate(fixture.Scene, 30);
	CHECK(box.GetComponent<TransformComponent>().LocalPosition.y == doctest::Approx(3.0f).epsilon(1e-5f));

	// Switching to dynamic recreates the body and gravity takes over.
	box.GetComponent<RigidBodyComponent>().Type = BodyType::Dynamic;
	Simulate(fixture.Scene, 1);
	CHECK(fixture.Physics.GetBodyCount() == 2);

	Simulate(fixture.Scene, 59);
	CHECK(box.GetComponent<TransformComponent>().LocalPosition.y < 2.9f);
}

TEST_CASE("Physics: Rebuild recreates bodies from the current scene state")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());
	CreateFloor(fixture.Scene);

	Entity box = CreateBox(fixture.Scene, "Box", BodyType::Dynamic, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.5f));
	Simulate(fixture.Scene, 30);
	CHECK(box.GetComponent<TransformComponent>().LocalPosition.y < 3.0f);

	// Teleport through the transform; Rebuild is the supported way to move a
	// dynamic body, because its transform normally belongs to the simulation.
	box.GetComponent<TransformComponent>().LocalPosition = glm::vec3(0.0f, 8.0f, 0.0f);
	fixture.Physics.Rebuild(fixture.Scene);
	CHECK(fixture.Physics.GetBodyCount() == 2);

	glm::vec3 velocity(1.0f);
	REQUIRE(fixture.Physics.GetLinearVelocity(box.GetUuid(), velocity));
	CHECK(glm::length(velocity) < 1e-4f);

	Simulate(fixture.Scene, 1);
	const float height = box.GetComponent<TransformComponent>().LocalPosition.y;
	CHECK(height < 8.0f);
	CHECK(height > 7.95f);
}

TEST_CASE("Physics: UseGravity and SetGravity are applied to dynamic bodies")
{
	PhysicsScene fixture;
	REQUIRE(fixture.Physics.Initialize());
	CHECK(fixture.Physics.GetGravity().y == doctest::Approx(-9.81f));

	CreateFloor(fixture.Scene);
	Entity floating =
	    CreateBox(fixture.Scene, "Floating", BodyType::Dynamic, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.5f));
	floating.GetComponent<RigidBodyComponent>().UseGravity = false;

	// High above the floor so the body stays free during the test.
	Entity falling =
	    CreateBox(fixture.Scene, "Falling", BodyType::Dynamic, glm::vec3(2.0f, 20.0f, 0.0f), glm::vec3(0.5f));

	Simulate(fixture.Scene, 30);
	CHECK(floating.GetComponent<TransformComponent>().LocalPosition.y == doctest::Approx(3.0f).epsilon(1e-5f));
	CHECK(falling.GetComponent<TransformComponent>().LocalPosition.y < 19.9f);

	glm::vec3 velocity(0.0f);
	REQUIRE(fixture.Physics.GetLinearVelocity(falling.GetUuid(), velocity));
	CHECK(velocity.y < 0.0f);
	const float velocityBefore = velocity.y;

	// Gravity changes apply to the live world immediately.
	fixture.Physics.SetGravity(glm::vec3(0.0f, 2.0f, 0.0f));
	CHECK(fixture.Physics.GetGravity().y == doctest::Approx(2.0f));

	Simulate(fixture.Scene, 30);
	REQUIRE(fixture.Physics.GetLinearVelocity(falling.GetUuid(), velocity));
	CHECK(velocity.y > velocityBefore + 0.5f);
	CHECK(fixture.Physics.GetBodyCount() == 3);
}
