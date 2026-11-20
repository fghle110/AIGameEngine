#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Project/Project.h"
#include "Aegis/Scene/Scene.h"
#include "Aegis/Serialization/SceneSerializer.h"

using namespace aegis;

TEST_CASE("Feature project: manifest and scene load with every component")
{
	const auto projectRoot = FileSystem::FromUtf8(AE_TEST_SOURCE_DIR) / "Assets" / "Projects" / "FeatureTest";
	REQUIRE(FileSystem::IsDirectory(projectRoot));

	Project project;
	std::string error;
	REQUIRE(Project::Load(projectRoot / "FeatureTest.aeproj", project, &error));
	CHECK(project.Name == "FeatureTest");
	CHECK(project.StartScene == "Scenes/FeatureTest.aescene");

	Scene scene;
	REQUIRE_MESSAGE(SceneSerializer::Load(scene, project.GetStartScenePath(), &error), error);
	CHECK(scene.GetEntityCount() >= 11);

	// Every component type used by the engine must appear in the feature scene.
	const ecs::Registry& registry = scene.GetRegistry();
	CHECK(registry.GetComponentCount<CameraComponent>() == 1);
	CHECK(registry.GetComponentCount<LightComponent>() == 3);
	CHECK(registry.GetComponentCount<MeshRendererComponent>() >= 5);
	CHECK(registry.GetComponentCount<RigidBodyComponent>() >= 4);
	CHECK(registry.GetComponentCount<ColliderComponent>() >= 4);
	CHECK(registry.GetComponentCount<LuaScriptComponent>() == 1);
	CHECK(registry.GetComponentCount<AudioSourceComponent>() == 1);
	CHECK(registry.GetComponentCount<AudioListenerComponent>() == 1);
	CHECK(registry.GetComponentCount<FlyCameraComponent>() == 1);

	CHECK(scene.GetPrimaryCamera().IsValid());
	CHECK(scene.FindEntity("Rig Child").GetParent().GetName() == "Camera Rig");
	CHECK(scene.FindEntity("Sun").GetComponent<LightComponent>().Type == LightType::Directional);
	CHECK(scene.FindEntity("Sun").GetComponent<LightComponent>().CastShadows);

	const Entity cube = scene.FindEntity("Falling Cube");
	REQUIRE(cube.IsValid());
	CHECK(cube.GetComponent<RigidBodyComponent>().Type == BodyType::Dynamic);
	CHECK(cube.GetComponent<LuaScriptComponent>().Properties.GetNumber("speed") > 0.0);

	scene.UpdateTransforms();
	CHECK(scene.FindEntity("Rig Child").GetComponent<TransformComponent>().WorldPosition.y > 0.0f);
}
