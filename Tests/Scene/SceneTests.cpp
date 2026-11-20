#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Scene/Scene.h"

#include <glm/gtc/constants.hpp>

#include <string>

using namespace aegis;

TEST_CASE("Scene: created entities receive the core components")
{
	Scene scene("Test Scene");
	Entity entity = scene.CreateEntity("Player");

	CHECK(entity.IsValid());
	CHECK(scene.GetEntityCount() == 1);
	CHECK(entity.GetName() == "Player");
	CHECK(entity.HasComponent<IdComponent>());
	CHECK(entity.HasComponent<TagComponent>());
	CHECK(entity.HasComponent<TransformComponent>());
	CHECK(entity.HasComponent<ActiveComponent>());
	CHECK(entity.IsActive());
	CHECK_FALSE(entity.GetUuid().IsNull());
}

TEST_CASE("Scene: lookup by UUID and by name")
{
	Scene scene;
	const Entity created = scene.CreateEntity("Lamp");

	CHECK(scene.FindEntity(created.GetUuid()) == created);
	CHECK(scene.FindEntity("Lamp") == created);
	CHECK_FALSE(scene.FindEntity("Missing").IsValid());
	CHECK_FALSE(scene.FindEntity(Uuid::Generate()).IsValid());
}

TEST_CASE("Scene: duplicate UUIDs are rejected in debug builds")
{
	Scene scene;
	const Uuid uuid = Uuid::Generate();
	scene.CreateEntity(uuid, "First");
	CHECK(scene.FindEntity(uuid).GetName() == "First");
	CHECK(scene.GetEntityCount() == 1);
}

TEST_CASE("Scene: destroying an entity removes descendants and parent links")
{
	Scene scene;
	Entity parent = scene.CreateEntity("Parent");
	Entity child = scene.CreateEntity("Child");
	Entity grandchild = scene.CreateEntity("Grandchild");

	child.SetParent(parent);
	grandchild.SetParent(child);

	CHECK(parent.GetComponent<TransformComponent>().Children.size() == 1);
	CHECK(child.GetComponent<TransformComponent>().Children.size() == 1);

	scene.DestroyEntity(parent);

	CHECK(scene.GetEntityCount() == 0);
	CHECK_FALSE(child.IsValid());
	CHECK_FALSE(grandchild.IsValid());
	CHECK_FALSE(scene.FindEntity("Parent").IsValid());
}

TEST_CASE("Scene: hierarchy propagates world transforms")
{
	Scene scene;
	Entity parent = scene.CreateEntity("Parent");
	Entity child = scene.CreateEntity("Child");

	child.SetParent(parent);

	parent.GetComponent<TransformComponent>().LocalPosition = glm::vec3(10.0f, 0.0f, 0.0f);
	child.GetComponent<TransformComponent>().LocalPosition = glm::vec3(0.0f, 5.0f, 0.0f);

	scene.UpdateTransforms();

	const auto& parentTransform = parent.GetComponent<TransformComponent>();
	const auto& childTransform = child.GetComponent<TransformComponent>();
	CHECK(parentTransform.WorldPosition.x == doctest::Approx(10.0f));
	CHECK(childTransform.WorldPosition.x == doctest::Approx(10.0f));
	CHECK(childTransform.WorldPosition.y == doctest::Approx(5.0f));
}

TEST_CASE("Scene: parent rotation rotates children around the parent origin")
{
	Scene scene;
	Entity parent = scene.CreateEntity("Parent");
	Entity child = scene.CreateEntity("Child");

	child.SetParent(parent);
	parent.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(0.0f, 90.0f, 0.0f));
	child.GetComponent<TransformComponent>().LocalPosition = glm::vec3(0.0f, 0.0f, 1.0f);

	scene.UpdateTransforms();

	// +90 degrees around Y maps +Z onto +X.
	const glm::vec3 world = child.GetComponent<TransformComponent>().WorldPosition;
	CHECK(world.x == doctest::Approx(1.0f).epsilon(0.001));
	CHECK(world.y == doctest::Approx(0.0f).epsilon(0.001));
	CHECK(world.z == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("Scene: reparenting preserves the world transform")
{
	Scene scene;
	Entity first = scene.CreateEntity("First");
	Entity second = scene.CreateEntity("Second");
	Entity subject = scene.CreateEntity("Subject");

	scene.UpdateTransforms();
	first.GetComponent<TransformComponent>().LocalPosition = glm::vec3(4.0f, 0.0f, 0.0f);
	second.GetComponent<TransformComponent>().LocalPosition = glm::vec3(-4.0f, 0.0f, 0.0f);
	subject.GetComponent<TransformComponent>().LocalPosition = glm::vec3(4.0f, 1.0f, 0.0f);
	scene.UpdateTransforms();

	const glm::vec3 worldBefore = subject.GetComponent<TransformComponent>().WorldPosition;

	subject.SetParent(first);
	scene.UpdateTransforms();
	const glm::vec3 worldAfter = subject.GetComponent<TransformComponent>().WorldPosition;

	CHECK(worldAfter.x == doctest::Approx(worldBefore.x).epsilon(0.001));
	CHECK(worldAfter.y == doctest::Approx(worldBefore.y).epsilon(0.001));
	CHECK(worldAfter.z == doctest::Approx(worldBefore.z).epsilon(0.001));

	subject.SetParent(second);
	scene.UpdateTransforms();
	const glm::vec3 worldAfterSecond = subject.GetComponent<TransformComponent>().WorldPosition;
	CHECK(worldAfterSecond.x == doctest::Approx(worldBefore.x).epsilon(0.001));
}

TEST_CASE("Scene: cycles are rejected")
{
	Scene scene;
	Entity parent = scene.CreateEntity("Parent");
	Entity child = scene.CreateEntity("Child");
	child.SetParent(parent);

	parent.SetParent(child);
	CHECK(parent.GetComponent<TransformComponent>().Parent.IsNull());
}

TEST_CASE("Scene: primary camera lookup and creation")
{
	Scene scene;
	CHECK_FALSE(scene.GetPrimaryCamera().IsValid());

	Entity camera = scene.EnsurePrimaryCamera();
	CHECK(camera.IsValid());
	CHECK(camera.HasComponent<CameraComponent>());
	CHECK(camera.GetComponent<CameraComponent>().Primary);

	Entity second = scene.CreateEntity("Second Camera");
	second.AddComponent<CameraComponent>();
	CHECK(scene.GetPrimaryCamera() == camera);

	second.GetComponent<CameraComponent>().Primary = true;
	CHECK(scene.GetPrimaryCamera() == second);
}

namespace
{
	struct CountingSystem final : public SceneSystem
	{
		explicit CountingSystem(i32 order)
			: Order(order)
		{
		}

		std::string_view GetName() const override { return "CountingSystem"; }
		i32 GetUpdateOrder() const override { return Order; }
		void OnUpdate(Scene&, Timestep) override { ++UpdateCount; ++GlobalCounter; }

		i32 Order = 0;
		int UpdateCount = 0;
		static int GlobalCounter;
	};
	int CountingSystem::GlobalCounter = 0;
}

TEST_CASE("Scene: systems update in order after transforms")
{
	Scene scene;
	CountingSystem late(100);
	CountingSystem early(-100);
	scene.AddSystem(&late);
	scene.AddSystem(&early);

	scene.OnUpdate(Timestep(0.016f));
	CHECK(early.UpdateCount == 1);
	CHECK(late.UpdateCount == 1);

	scene.RemoveSystem(&early);
	scene.OnUpdate(Timestep(0.016f));
	CHECK(early.UpdateCount == 1);
	CHECK(late.UpdateCount == 2);
}

TEST_CASE("Scene: environment defaults are sensible")
{
	Scene scene;
	const EnvironmentSettings& environment = scene.GetEnvironment();
	CHECK(environment.EnableIBL);
	CHECK(environment.EnableShadows);
	CHECK(environment.Exposure > 0.0f);
	CHECK(environment.ShadowDistance > 0.0f);
}

TEST_CASE("Scene: clear empties the scene")
{
	Scene scene;
	scene.CreateEntity("A");
	scene.CreateEntity("B");
	scene.Clear();
	CHECK(scene.IsEmpty());
	CHECK(scene.GetEntities().empty());
}
