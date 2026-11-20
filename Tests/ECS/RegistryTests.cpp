#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/ECS/Registry.h"

#include <string>

using namespace aegis;
using namespace aegis::ecs;

namespace
{
	struct Position
	{
		float X = 0.0f;
		float Y = 0.0f;
	};

	struct Velocity
	{
		float X = 0.0f;
	};

	struct Health
	{
		int Value = 100;
	};
}

TEST_CASE("Registry: entities get unique, valid handles")
{
	Registry registry;
	const EntityHandle a = registry.CreateEntity();
	const EntityHandle b = registry.CreateEntity();

	CHECK(a != b);
	CHECK(a.IsValid());
	CHECK(registry.IsAlive(a));
	CHECK(registry.IsAlive(b));
	CHECK(registry.GetEntityCount() == 2);
}

TEST_CASE("Registry: destroy invalidates the handle and reuses the slot with a new generation")
{
	Registry registry;
	const EntityHandle first = registry.CreateEntity();
	registry.DestroyEntity(first);

	CHECK_FALSE(registry.IsAlive(first));
	CHECK(registry.GetEntityCount() == 0);

	const EntityHandle second = registry.CreateEntity();
	CHECK(second.Index == first.Index);
	CHECK(second.Generation != first.Generation);
	CHECK(registry.IsAlive(second));
	CHECK_FALSE(registry.IsAlive(first)); // stale handle stays dead
}

TEST_CASE("Registry: component add, get, has and remove")
{
	Registry registry;
	const EntityHandle entity = registry.CreateEntity();

	CHECK_FALSE(registry.HasComponent<Position>(entity));
	Position& position = registry.AddComponent<Position>(entity, Position{ 1.0f, 2.0f });
	CHECK(position.X == 1.0f);
	CHECK(registry.HasComponent<Position>(entity));
	CHECK(registry.GetComponent<Position>(entity).Y == 2.0f);
	CHECK(registry.GetComponentCount<Position>() == 1);

	registry.RemoveComponent<Position>(entity);
	CHECK_FALSE(registry.HasComponent<Position>(entity));
	CHECK(registry.TryGetComponent<Position>(entity) == nullptr);
	CHECK(registry.GetComponentCount<Position>() == 0);
}

TEST_CASE("Registry: destroying an entity removes all of its components")
{
	Registry registry;
	const EntityHandle entity = registry.CreateEntity();
	registry.AddComponent<Position>(entity);
	registry.AddComponent<Velocity>(entity);

	registry.DestroyEntity(entity);

	CHECK(registry.GetComponentCount<Position>() == 0);
	CHECK(registry.GetComponentCount<Velocity>() == 0);
}

TEST_CASE("Registry: dense pool keeps remaining components readable after a swap-remove")
{
	Registry registry;
	const EntityHandle a = registry.CreateEntity();
	const EntityHandle b = registry.CreateEntity();
	const EntityHandle c = registry.CreateEntity();

	registry.AddComponent<Position>(a, Position{ 1.0f, 0.0f });
	registry.AddComponent<Position>(b, Position{ 2.0f, 0.0f });
	registry.AddComponent<Position>(c, Position{ 3.0f, 0.0f });

	registry.RemoveComponent<Position>(b);

	CHECK(registry.GetComponent<Position>(a).X == 1.0f);
	CHECK(registry.GetComponent<Position>(c).X == 3.0f);
	CHECK(registry.GetComponentCount<Position>() == 2);
}

TEST_CASE("Registry: Each visits only entities holding every requested component")
{
	Registry registry;

	const EntityHandle both = registry.CreateEntity();
	registry.AddComponent<Position>(both, Position{ 1.0f, 1.0f });
	registry.AddComponent<Velocity>(both, Velocity{ 2.0f });

	const EntityHandle onlyPosition = registry.CreateEntity();
	registry.AddComponent<Position>(onlyPosition, Position{ 3.0f, 3.0f });

	const EntityHandle onlyVelocity = registry.CreateEntity();
	registry.AddComponent<Velocity>(onlyVelocity, Velocity{ 4.0f });

	int visited = 0;
	registry.Each<Position, Velocity>([&](EntityHandle, Position& position, Velocity& velocity)
	{
		++visited;
		CHECK(position.X == 1.0f);
		CHECK(velocity.X == 2.0f);
	});
	CHECK(visited == 1);

	int positions = 0;
	registry.Each<Position>([&](EntityHandle, Position&)
	{
		++positions;
	});
	CHECK(positions == 2);
}

TEST_CASE("Registry: Each exposes the generation-correct handle")
{
	Registry registry;
	const EntityHandle entity = registry.CreateEntity();
	registry.AddComponent<Position>(entity);

	EntityHandle observed;
	registry.Each<Position>([&](EntityHandle handle, Position&)
	{
		observed = handle;
	});
	CHECK(observed == entity);
}

TEST_CASE("Registry: clear removes entities and components")
{
	Registry registry;
	const EntityHandle entity = registry.CreateEntity();
	registry.AddComponent<Health>(entity);

	registry.Clear();

	CHECK(registry.GetEntityCount() == 0);
	CHECK(registry.GetComponentCount<Health>() == 0);
	CHECK_FALSE(registry.IsAlive(entity));
}

TEST_CASE("Registry: destroying a dead handle is a no-op")
{
	Registry registry;
	const EntityHandle entity = registry.CreateEntity();
	registry.DestroyEntity(entity);
	registry.DestroyEntity(entity);
	CHECK(registry.GetEntityCount() == 0);
}

TEST_CASE("Registry: many entities stay consistent")
{
	Registry registry;
	std::vector<EntityHandle> entities;
	for (int i = 0; i < 2048; ++i)
	{
		const EntityHandle handle = registry.CreateEntity();
		registry.AddComponent<Health>(handle, Health{ i });
		entities.push_back(handle);
	}
	CHECK(registry.GetEntityCount() == 2048);
	CHECK(registry.GetComponentCount<Health>() == 2048);

	// Remove every other entity and verify the survivors kept their values.
	for (usize i = 0; i < entities.size(); i += 2)
	{
		registry.DestroyEntity(entities[i]);
	}
	CHECK(registry.GetEntityCount() == 1024);
	for (usize i = 1; i < entities.size(); i += 2)
	{
		CHECK(registry.GetComponent<Health>(entities[i]).Value == static_cast<int>(i));
	}
}
