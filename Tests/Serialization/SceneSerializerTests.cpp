#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Serialization/SceneSerializer.h"

#include <glm/glm.hpp>

#include <string>
#include <utility>

using namespace aegis;

namespace
{
	AssetHandle MakeAsset(AssetType type)
	{
		AssetHandle handle;
		handle.Id = Uuid::Generate();
		handle.Type = type;
		return handle;
	}

	FileSystem::Path SerializationDirectory()
	{
		const FileSystem::Path directory = FileSystem::FromUtf8("TestAssets/Serialization");
		FileSystem::DeleteDirectory(directory);
		FileSystem::CreateDirectories(directory);
		return directory;
	}

	void CheckVec3(const glm::vec3& actual, const glm::vec3& expected)
	{
		CHECK(actual.x == doctest::Approx(expected.x).epsilon(0.0001));
		CHECK(actual.y == doctest::Approx(expected.y).epsilon(0.0001));
		CHECK(actual.z == doctest::Approx(expected.z).epsilon(0.0001));
	}

	void CheckVec4(const glm::vec4& actual, const glm::vec4& expected)
	{
		CHECK(actual.x == doctest::Approx(expected.x).epsilon(0.0001));
		CHECK(actual.y == doctest::Approx(expected.y).epsilon(0.0001));
		CHECK(actual.z == doctest::Approx(expected.z).epsilon(0.0001));
		CHECK(actual.w == doctest::Approx(expected.w).epsilon(0.0001));
	}

	void CheckQuat(const glm::quat& actual, const glm::quat& expected)
	{
		CHECK(actual.w == doctest::Approx(expected.w).epsilon(0.0001));
		CHECK(actual.x == doctest::Approx(expected.x).epsilon(0.0001));
		CHECK(actual.y == doctest::Approx(expected.y).epsilon(0.0001));
		CHECK(actual.z == doctest::Approx(expected.z).epsilon(0.0001));
	}

	void CheckHandle(const AssetHandle& actual, const AssetHandle& expected)
	{
		CHECK(actual.Id == expected.Id);
		CHECK(actual.Type == expected.Type);
	}

	void CheckEnvironment(const EnvironmentSettings& actual, const EnvironmentSettings& expected)
	{
		CheckVec3(actual.AmbientColor, expected.AmbientColor);
		CHECK(actual.AmbientIntensity == doctest::Approx(expected.AmbientIntensity).epsilon(0.0001));
		CHECK(actual.HdriPath == expected.HdriPath);
		CHECK(actual.HdriIntensity == doctest::Approx(expected.HdriIntensity).epsilon(0.0001));
		CHECK(actual.Exposure == doctest::Approx(expected.Exposure).epsilon(0.0001));
		CHECK(actual.EnableIBL == expected.EnableIBL);
		CHECK(actual.EnableShadows == expected.EnableShadows);
		CHECK(actual.EnableSSAO == expected.EnableSSAO);
		CHECK(actual.EnableBloom == expected.EnableBloom);
		CHECK(actual.EnableFXAA == expected.EnableFXAA);
		CHECK(actual.ShadowDistance == doctest::Approx(expected.ShadowDistance).epsilon(0.0001));
		CHECK(actual.SSAORadius == doctest::Approx(expected.SSAORadius).epsilon(0.0001));
		CHECK(actual.BloomThreshold == doctest::Approx(expected.BloomThreshold).epsilon(0.0001));
		CHECK(actual.BloomIntensity == doctest::Approx(expected.BloomIntensity).epsilon(0.0001));
	}

	EnvironmentSettings MakeTunedEnvironment()
	{
		EnvironmentSettings environment;
		environment.AmbientColor = glm::vec3(0.11f, 0.22f, 0.33f);
		environment.AmbientIntensity = 1.75f;
		environment.HdriPath = "Textures/Studio.hdr";
		environment.HdriIntensity = 0.85f;
		environment.Exposure = 1.4f;
		environment.EnableIBL = false;
		environment.EnableShadows = false;
		environment.EnableSSAO = false;
		environment.EnableBloom = false;
		environment.EnableFXAA = false;
		environment.ShadowDistance = 75.5f;
		environment.SSAORadius = 0.9f;
		environment.BloomThreshold = 1.3f;
		environment.BloomIntensity = 0.6f;
		return environment;
	}

	struct FullSceneHandles
	{
		Entity Root;
		Entity Child;
		Entity Grandchild;
		Entity Camera;
		Entity Sun;
		Entity Cube;
	};

	// Builds a scene that uses every component and a two-level hierarchy.
	FullSceneHandles BuildFullScene(Scene& scene)
	{
		scene.SetEnvironment(MakeTunedEnvironment());

		FullSceneHandles handles;
		handles.Root = scene.CreateEntity("Root");
		handles.Child = scene.CreateEntity("Child");
		handles.Grandchild = scene.CreateEntity("Grandchild");
		handles.Camera = scene.CreateEntity("Main Camera");
		handles.Sun = scene.CreateEntity("Sun");
		handles.Cube = scene.CreateEntity("Cube");

		// Parent first: SetParent preserves the world transform, so the
		// authoritative local values are assigned afterwards.
		handles.Child.SetParent(handles.Root);
		handles.Grandchild.SetParent(handles.Child);

		handles.Root.GetComponent<TransformComponent>().LocalPosition = glm::vec3(10.0f, 0.0f, 0.0f);
		handles.Child.GetComponent<TransformComponent>().LocalPosition = glm::vec3(0.0f, 5.0f, 0.0f);
		handles.Grandchild.GetComponent<TransformComponent>().LocalPosition = glm::vec3(0.0f, 0.0f, 2.0f);
		handles.Grandchild.GetComponent<TransformComponent>().LocalScale = glm::vec3(0.5f, 0.5f, 0.5f);
		handles.Camera.GetComponent<TransformComponent>().LocalPosition = glm::vec3(0.0f, 3.0f, -8.0f);
		handles.Sun.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-50.0f, 30.0f, 0.0f));
		handles.Cube.GetComponent<TransformComponent>().LocalPosition = glm::vec3(1.5f, 0.5f, -2.0f);

		handles.Grandchild.SetActive(false);

		CameraComponent camera;
		camera.Projection = CameraProjection::Orthographic;
		camera.FieldOfView = 58.5f;
		camera.NearClip = 0.25f;
		camera.FarClip = 750.0f;
		camera.OrthographicSize = 12.5f;
		camera.Primary = true;
		camera.ClearColor = glm::vec4(0.1f, 0.2f, 0.3f, 0.95f);
		handles.Camera.AddComponent<CameraComponent>(camera);

		AudioListenerComponent listener;
		listener.Primary = true;
		handles.Camera.AddComponent<AudioListenerComponent>(listener);

		FlyCameraComponent flyCamera;
		flyCamera.Active = true;
		flyCamera.MoveSpeed = 20.0f;
		flyCamera.FastMultiplier = 6.0f;
		flyCamera.LookSensitivity = 0.2f;
		flyCamera.SpeedDamping = 15.0f;
		flyCamera.Velocity = glm::vec3(0.5f, -1.5f, 2.5f);
		handles.Camera.AddComponent<FlyCameraComponent>(flyCamera);

		LightComponent light;
		light.Type = LightType::Directional;
		light.Color = glm::vec3(1.0f, 0.95f, 0.8f);
		light.Intensity = 2.5f;
		light.Range = 30.0f;
		light.InnerConeDegrees = 15.0f;
		light.OuterConeDegrees = 40.0f;
		light.CastShadows = false;
		handles.Sun.AddComponent<LightComponent>(light);

		MeshRendererComponent renderer;
		renderer.Mesh = MakeAsset(AssetType::Mesh);
		renderer.Materials.push_back(MakeAsset(AssetType::Material));
		renderer.Materials.push_back(MakeAsset(AssetType::Material));
		renderer.Visible = false;
		renderer.CastShadows = false;
		renderer.ReceiveShadows = false;
		handles.Cube.AddComponent<MeshRendererComponent>(renderer);

		RigidBodyComponent body;
		body.Type = BodyType::Dynamic;
		body.Mass = 12.5f;
		body.Friction = 0.7f;
		body.Restitution = 0.2f;
		body.LinearDamping = 0.1f;
		body.AngularDamping = 0.2f;
		body.UseGravity = false;
		handles.Cube.AddComponent<RigidBodyComponent>(body);

		ColliderComponent collider;
		collider.Shape = ColliderShape::Box;
		collider.HalfExtents = glm::vec3(0.5f, 1.0f, 1.5f);
		collider.Radius = 0.75f;
		collider.HalfHeight = 1.25f;
		collider.Offset = glm::vec3(0.1f, 0.2f, 0.3f);
		collider.IsTrigger = true;
		handles.Cube.AddComponent<ColliderComponent>(collider);

		LuaScriptComponent script;
		script.Script = MakeAsset(AssetType::Script);
		script.Enabled = false;
		script.Properties = JsonValue::MakeObject();
		script.Properties.Set("speed", 3.5f);
		script.Properties.Set("label", "turret");
		JsonValue nested = JsonValue::MakeObject();
		nested.Set("enabled", true);
		nested.Set("count", 2);
		script.Properties.Set("nested", std::move(nested));
		handles.Cube.AddComponent<LuaScriptComponent>(script);

		AudioSourceComponent audioSource;
		audioSource.Clip = MakeAsset(AssetType::Audio);
		audioSource.Volume = 0.6f;
		audioSource.Pitch = 1.2f;
		audioSource.Loop = true;
		audioSource.PlayOnStart = false;
		audioSource.Spatial = false;
		audioSource.MinDistance = 2.5f;
		audioSource.MaxDistance = 55.0f;
		handles.Cube.AddComponent<AudioSourceComponent>(audioSource);

		scene.UpdateTransforms();
		return handles;
	}

	JsonValue Vec3Json(f32 x, f32 y, f32 z)
	{
		JsonValue array = JsonValue::MakeArray();
		array.Push(x);
		array.Push(y);
		array.Push(z);
		return array;
	}

	JsonValue MinimalDocument()
	{
		JsonValue document = JsonValue::MakeObject();
		document.Set("version", 1);

		JsonValue sceneObject = JsonValue::MakeObject();
		sceneObject.Set("uuid", Uuid::Generate().ToString());
		sceneObject.Set("name", "Minimal Scene");
		sceneObject.Set("environment", JsonValue::MakeObject());
		document.Set("scene", std::move(sceneObject));
		document.Set("entities", JsonValue::MakeArray());
		return document;
	}

	JsonValue MinimalEntity(const Uuid& uuid, const std::string& name)
	{
		JsonValue entity = JsonValue::MakeObject();
		entity.Set("uuid", uuid.ToString());
		entity.Set("name", name);
		entity.Set("active", true);
		return entity;
	}
}

TEST_CASE("SceneSerializer: scene, hierarchy and environment survive a round trip")
{
	Scene source("Serialization Round Trip");
	BuildFullScene(source);

	const JsonValue document = SceneSerializer::Serialize(source);
	REQUIRE(document.GetInt("version") == 1);
	REQUIRE(document.Find("scene") != nullptr);
	REQUIRE(document.Find("entities") != nullptr);
	CHECK(document["entities"].GetSize() == source.GetEntityCount());

	const JsonValue& rootTransform = document["entities"][0]["transform"];
	CHECK(rootTransform.Contains("position"));
	CHECK(rootTransform.Contains("rotation"));
	CHECK(rootTransform.Contains("scale"));
	CHECK(rootTransform.Contains("parent"));
	CHECK(rootTransform.Contains("children"));
	CHECK_FALSE(rootTransform.Contains("worldPosition"));

	Scene loaded("Placeholder");
	std::string error;
	REQUIRE(SceneSerializer::Deserialize(loaded, document, &error));
	CHECK(error.empty());

	CHECK(loaded.GetUuid() == source.GetUuid());
	CHECK(loaded.GetName() == source.GetName());
	CHECK(loaded.GetEntityCount() == source.GetEntityCount());
	CheckEnvironment(loaded.GetEnvironment(), source.GetEnvironment());

	CHECK(loaded.GetRegistry().GetComponentCount<IdComponent>() == source.GetEntityCount());
	CHECK(loaded.GetRegistry().GetComponentCount<TagComponent>() == source.GetEntityCount());
	CHECK(loaded.GetRegistry().GetComponentCount<ActiveComponent>() == source.GetEntityCount());
	CHECK(loaded.GetRegistry().GetComponentCount<TransformComponent>() == source.GetEntityCount());
	CHECK(loaded.GetRegistry().GetComponentCount<CameraComponent>() == 1);
	CHECK(loaded.GetRegistry().GetComponentCount<LightComponent>() == 1);
	CHECK(loaded.GetRegistry().GetComponentCount<MeshRendererComponent>() == 1);
	CHECK(loaded.GetRegistry().GetComponentCount<RigidBodyComponent>() == 1);
	CHECK(loaded.GetRegistry().GetComponentCount<ColliderComponent>() == 1);
	CHECK(loaded.GetRegistry().GetComponentCount<LuaScriptComponent>() == 1);
	CHECK(loaded.GetRegistry().GetComponentCount<AudioSourceComponent>() == 1);
	CHECK(loaded.GetRegistry().GetComponentCount<AudioListenerComponent>() == 1);
	CHECK(loaded.GetRegistry().GetComponentCount<FlyCameraComponent>() == 1);

	const Entity sourceRoot = source.FindEntity("Root");
	const Entity loadedRoot = loaded.FindEntity("Root");
	REQUIRE(sourceRoot.IsValid());
	REQUIRE(loadedRoot.IsValid());
	CHECK(loadedRoot.GetUuid() == sourceRoot.GetUuid());
	CHECK(loadedRoot.GetName() == "Root");
	CHECK(loadedRoot.GetUuid() == loaded.FindEntity("Root").GetUuid());

	const TransformComponent& loadedRootTransform = loadedRoot.GetComponent<TransformComponent>();
	CHECK(loadedRootTransform.Parent.IsNull());
	REQUIRE(loadedRootTransform.Children.size() == 1);
	CHECK(loadedRootTransform.Children[0] == loaded.FindEntity("Child").GetUuid());
	CheckVec3(loadedRootTransform.LocalPosition, glm::vec3(10.0f, 0.0f, 0.0f));
	CheckQuat(loadedRootTransform.LocalRotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
	CheckVec3(loadedRootTransform.LocalScale, glm::vec3(1.0f));

	const Entity loadedChild = loaded.FindEntity("Child");
	REQUIRE(loadedChild.IsValid());
	const TransformComponent& childTransform = loadedChild.GetComponent<TransformComponent>();
	CHECK(childTransform.Parent == loadedRoot.GetUuid());
	REQUIRE(childTransform.Children.size() == 1);
	CHECK(childTransform.Children[0] == loaded.FindEntity("Grandchild").GetUuid());
	CheckVec3(childTransform.LocalPosition, glm::vec3(0.0f, 5.0f, 0.0f));

	const Entity loadedGrandchild = loaded.FindEntity("Grandchild");
	REQUIRE(loadedGrandchild.IsValid());
	CHECK_FALSE(loadedGrandchild.IsActive());
	CHECK(loadedGrandchild.GetComponent<TransformComponent>().Parent == loadedChild.GetUuid());

	// World transforms are derived on load, never stored in the document.
	CheckVec3(childTransform.WorldPosition, glm::vec3(10.0f, 5.0f, 0.0f));
	CheckVec3(loadedGrandchild.GetComponent<TransformComponent>().WorldPosition, glm::vec3(10.0f, 5.0f, 2.0f));
	CheckVec3(loadedGrandchild.GetComponent<TransformComponent>().WorldScale, glm::vec3(0.5f));
}

TEST_CASE("SceneSerializer: every component survives a round trip")
{
	Scene source("Components Round Trip");
	BuildFullScene(source);

	Scene loaded;
	std::string error;
	REQUIRE(SceneSerializer::Deserialize(loaded, SceneSerializer::Serialize(source), &error));
	CHECK(error.empty());

	const Entity sourceCamera = source.FindEntity("Main Camera");
	const Entity loadedCamera = loaded.FindEntity("Main Camera");
	REQUIRE(sourceCamera.IsValid());
	REQUIRE(loadedCamera.IsValid());

	REQUIRE(loadedCamera.HasComponent<CameraComponent>());
	const CameraComponent& camera = loadedCamera.GetComponent<CameraComponent>();
	const CameraComponent& expectedCamera = sourceCamera.GetComponent<CameraComponent>();
	CHECK(camera.Projection == expectedCamera.Projection);
	CHECK(camera.Projection == CameraProjection::Orthographic);
	CHECK(camera.FieldOfView == doctest::Approx(expectedCamera.FieldOfView).epsilon(0.0001));
	CHECK(camera.NearClip == doctest::Approx(expectedCamera.NearClip).epsilon(0.0001));
	CHECK(camera.FarClip == doctest::Approx(expectedCamera.FarClip).epsilon(0.0001));
	CHECK(camera.OrthographicSize == doctest::Approx(expectedCamera.OrthographicSize).epsilon(0.0001));
	CHECK(camera.Primary == expectedCamera.Primary);
	CheckVec4(camera.ClearColor, expectedCamera.ClearColor);

	REQUIRE(loadedCamera.HasComponent<AudioListenerComponent>());
	CHECK(loadedCamera.GetComponent<AudioListenerComponent>().Primary);

	REQUIRE(loadedCamera.HasComponent<FlyCameraComponent>());
	const FlyCameraComponent& flyCamera = loadedCamera.GetComponent<FlyCameraComponent>();
	const FlyCameraComponent& expectedFlyCamera = sourceCamera.GetComponent<FlyCameraComponent>();
	CHECK(flyCamera.Active == expectedFlyCamera.Active);
	CHECK(flyCamera.MoveSpeed == doctest::Approx(expectedFlyCamera.MoveSpeed).epsilon(0.0001));
	CHECK(flyCamera.FastMultiplier == doctest::Approx(expectedFlyCamera.FastMultiplier).epsilon(0.0001));
	CHECK(flyCamera.LookSensitivity == doctest::Approx(expectedFlyCamera.LookSensitivity).epsilon(0.0001));
	CHECK(flyCamera.SpeedDamping == doctest::Approx(expectedFlyCamera.SpeedDamping).epsilon(0.0001));
	CheckVec3(flyCamera.Velocity, expectedFlyCamera.Velocity);

	const Entity sourceSun = source.FindEntity("Sun");
	const Entity loadedSun = loaded.FindEntity("Sun");
	REQUIRE(loadedSun.IsValid());
	REQUIRE(loadedSun.HasComponent<LightComponent>());
	const LightComponent& light = loadedSun.GetComponent<LightComponent>();
	const LightComponent& expectedLight = sourceSun.GetComponent<LightComponent>();
	CHECK(light.Type == expectedLight.Type);
	CHECK(light.Type == LightType::Directional);
	CheckVec3(light.Color, expectedLight.Color);
	CHECK(light.Intensity == doctest::Approx(expectedLight.Intensity).epsilon(0.0001));
	CHECK(light.Range == doctest::Approx(expectedLight.Range).epsilon(0.0001));
	CHECK(light.InnerConeDegrees == doctest::Approx(expectedLight.InnerConeDegrees).epsilon(0.0001));
	CHECK(light.OuterConeDegrees == doctest::Approx(expectedLight.OuterConeDegrees).epsilon(0.0001));
	CHECK(light.CastShadows == expectedLight.CastShadows);
	CHECK_FALSE(light.CastShadows);
	CheckQuat(loadedSun.GetComponent<TransformComponent>().LocalRotation,
		sourceSun.GetComponent<TransformComponent>().LocalRotation);

	const Entity sourceCube = source.FindEntity("Cube");
	const Entity loadedCube = loaded.FindEntity("Cube");
	REQUIRE(loadedCube.IsValid());

	REQUIRE(loadedCube.HasComponent<MeshRendererComponent>());
	const MeshRendererComponent& renderer = loadedCube.GetComponent<MeshRendererComponent>();
	const MeshRendererComponent& expectedRenderer = sourceCube.GetComponent<MeshRendererComponent>();
	CheckHandle(renderer.Mesh, expectedRenderer.Mesh);
	REQUIRE(renderer.Materials.size() == 2);
	CheckHandle(renderer.Materials[0], expectedRenderer.Materials[0]);
	CheckHandle(renderer.Materials[1], expectedRenderer.Materials[1]);
	CHECK(renderer.Materials[0].Id != renderer.Materials[1].Id);
	CHECK(renderer.Visible == expectedRenderer.Visible);
	CHECK(renderer.CastShadows == expectedRenderer.CastShadows);
	CHECK(renderer.ReceiveShadows == expectedRenderer.ReceiveShadows);
	CHECK_FALSE(renderer.Visible);

	REQUIRE(loadedCube.HasComponent<RigidBodyComponent>());
	const RigidBodyComponent& body = loadedCube.GetComponent<RigidBodyComponent>();
	const RigidBodyComponent& expectedBody = sourceCube.GetComponent<RigidBodyComponent>();
	CHECK(body.Type == expectedBody.Type);
	CHECK(body.Type == BodyType::Dynamic);
	CHECK(body.Mass == doctest::Approx(expectedBody.Mass).epsilon(0.0001));
	CHECK(body.Friction == doctest::Approx(expectedBody.Friction).epsilon(0.0001));
	CHECK(body.Restitution == doctest::Approx(expectedBody.Restitution).epsilon(0.0001));
	CHECK(body.LinearDamping == doctest::Approx(expectedBody.LinearDamping).epsilon(0.0001));
	CHECK(body.AngularDamping == doctest::Approx(expectedBody.AngularDamping).epsilon(0.0001));
	CHECK(body.UseGravity == expectedBody.UseGravity);
	CHECK_FALSE(body.UseGravity);

	REQUIRE(loadedCube.HasComponent<ColliderComponent>());
	const ColliderComponent& collider = loadedCube.GetComponent<ColliderComponent>();
	const ColliderComponent& expectedCollider = sourceCube.GetComponent<ColliderComponent>();
	CHECK(collider.Shape == expectedCollider.Shape);
	CHECK(collider.Shape == ColliderShape::Box);
	CheckVec3(collider.HalfExtents, expectedCollider.HalfExtents);
	CHECK(collider.Radius == doctest::Approx(expectedCollider.Radius).epsilon(0.0001));
	CHECK(collider.HalfHeight == doctest::Approx(expectedCollider.HalfHeight).epsilon(0.0001));
	CheckVec3(collider.Offset, expectedCollider.Offset);
	CHECK(collider.IsTrigger == expectedCollider.IsTrigger);
	CHECK(collider.IsTrigger);

	REQUIRE(loadedCube.HasComponent<LuaScriptComponent>());
	const LuaScriptComponent& script = loadedCube.GetComponent<LuaScriptComponent>();
	const LuaScriptComponent& expectedScript = sourceCube.GetComponent<LuaScriptComponent>();
	CheckHandle(script.Script, expectedScript.Script);
	CHECK(script.Enabled == expectedScript.Enabled);
	CHECK_FALSE(script.Enabled);
	REQUIRE(script.Properties.IsObject());
	CHECK(script.Properties.GetFloat("speed") == doctest::Approx(3.5f));
	CHECK(script.Properties.GetString("label") == "turret");
	REQUIRE(script.Properties.Find("nested") != nullptr);
	CHECK(script.Properties["nested"].GetBool("enabled"));
	CHECK(script.Properties["nested"].GetInt("count") == 2);

	REQUIRE(loadedCube.HasComponent<AudioSourceComponent>());
	const AudioSourceComponent& audioSource = loadedCube.GetComponent<AudioSourceComponent>();
	const AudioSourceComponent& expectedAudioSource = sourceCube.GetComponent<AudioSourceComponent>();
	CheckHandle(audioSource.Clip, expectedAudioSource.Clip);
	CHECK(audioSource.Volume == doctest::Approx(expectedAudioSource.Volume).epsilon(0.0001));
	CHECK(audioSource.Pitch == doctest::Approx(expectedAudioSource.Pitch).epsilon(0.0001));
	CHECK(audioSource.Loop == expectedAudioSource.Loop);
	CHECK(audioSource.PlayOnStart == expectedAudioSource.PlayOnStart);
	CHECK(audioSource.Spatial == expectedAudioSource.Spatial);
	CHECK(audioSource.MinDistance == doctest::Approx(expectedAudioSource.MinDistance).epsilon(0.0001));
	CHECK(audioSource.MaxDistance == doctest::Approx(expectedAudioSource.MaxDistance).epsilon(0.0001));
	CHECK(audioSource.Loop);
	CHECK_FALSE(audioSource.PlayOnStart);
	CHECK_FALSE(audioSource.Spatial);
}

TEST_CASE("SceneSerializer: output is deterministic")
{
	Scene scene("Deterministic Scene");
	BuildFullScene(scene);

	const std::string first = SceneSerializer::Serialize(scene).Dump(4);
	const std::string second = SceneSerializer::Serialize(scene).Dump(4);
	CHECK(first == second);

	// The document is canonical: loading and serialising again reproduces the
	// exact same bytes.
	Scene loaded;
	REQUIRE(SceneSerializer::Deserialize(loaded, SceneSerializer::Serialize(scene)));
	CHECK(SceneSerializer::Serialize(loaded).Dump(4) == first);
}

TEST_CASE("SceneSerializer: unknown versions are rejected with a readable error")
{
	Scene scene("Versioned Scene");
	scene.CreateEntity("Kept");

	JsonValue document = SceneSerializer::Serialize(scene);
	document.Set("version", 99);

	std::string error;
	CHECK_FALSE(SceneSerializer::Deserialize(scene, document, &error));
	CHECK(error.find("version") != std::string::npos);
	CHECK(error.find("99") != std::string::npos);
	CHECK(scene.IsEmpty());
	// A failed load restores the scene identity it had before the attempt.
	CHECK(scene.GetName() == "Versioned Scene");
}

TEST_CASE("SceneSerializer: unknown members are ignored for forward compatibility")
{
	Scene source("Forward Compatible");
	source.CreateEntity("Alpha");

	JsonValue document = SceneSerializer::Serialize(source);
	document.Set("futureRootMember", JsonValue("ignored"));

	JsonValue& sceneObject = document["scene"];
	sceneObject.Set("futureSceneSetting", JsonValue::MakeObject());

	JsonValue& entity = document["entities"][0];
	entity.Set("futureComponent", JsonValue::MakeObject());
	entity["futureComponent"].Set("value", 42);
	entity["transform"].Set("futureTransformField", JsonValue::MakeArray());

	Scene loaded;
	std::string error;
	REQUIRE(SceneSerializer::Deserialize(loaded, document, &error));
	CHECK(error.empty());
	REQUIRE(loaded.GetEntityCount() == 1);
	CHECK(loaded.GetName() == "Forward Compatible");
	CHECK(loaded.FindEntity("Alpha").IsValid());
}

TEST_CASE("SceneSerializer: a missing parent reference drops the link without crashing")
{
	const Uuid childUuid = Uuid::Generate();
	const Uuid missingParentUuid = Uuid::Generate();

	JsonValue document = MinimalDocument();
	JsonValue entity = MinimalEntity(childUuid, "Orphan");
	JsonValue transform = JsonValue::MakeObject();
	transform.Set("position", Vec3Json(1.0f, 2.0f, 3.0f));
	transform.Set("parent", missingParentUuid.ToString());
	transform.Set("children", JsonValue::MakeArray());
	entity.Set("transform", std::move(transform));
	document["entities"].Push(std::move(entity));

	Scene scene("Orphans");
	scene.CreateEntity("Existing");
	std::string error;
	REQUIRE(SceneSerializer::Deserialize(scene, document, &error));
	CHECK(error.empty());
	CHECK(scene.GetEntityCount() == 1);
	CHECK_FALSE(scene.FindEntity("Existing").IsValid());

	const Entity orphan = scene.FindEntity(childUuid);
	REQUIRE(orphan.IsValid());
	const TransformComponent& orphanTransform = orphan.GetComponent<TransformComponent>();
	CHECK(orphanTransform.Parent.IsNull());
	CHECK(orphanTransform.Children.empty());
	CheckVec3(orphanTransform.LocalPosition, glm::vec3(1.0f, 2.0f, 3.0f));
	// Without a parent the local transform is the world transform.
	CheckVec3(orphanTransform.WorldPosition, glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("SceneSerializer: a minimal version 1 document loads with defaults")
{
	JsonValue document = JsonValue::MakeObject();
	document.Set("version", 1);
	JsonValue sceneObject = JsonValue::MakeObject();
	sceneObject.Set("name", "Minimal");
	document.Set("scene", std::move(sceneObject));

	JsonValue entities = JsonValue::MakeArray();
	const Uuid soloUuid = Uuid::Generate();
	JsonValue entity = MinimalEntity(soloUuid, "Solo");
	entities.Push(std::move(entity));
	document.Set("entities", std::move(entities));

	Scene scene("Before");
	std::string error;
	REQUIRE(SceneSerializer::Deserialize(scene, document, &error));
	CHECK(error.empty());
	CHECK(scene.GetName() == "Minimal");
	CHECK_FALSE(scene.GetUuid().IsNull());
	CHECK(scene.GetEntityCount() == 1);

	const Entity solo = scene.FindEntity(soloUuid);
	REQUIRE(solo.IsValid());
	CHECK(solo.IsActive());
	CHECK(solo.HasComponent<TransformComponent>());
	CHECK_FALSE(solo.HasComponent<CameraComponent>());
	CHECK_FALSE(solo.HasComponent<LightComponent>());
	const TransformComponent& transform = solo.GetComponent<TransformComponent>();
	CheckVec3(transform.LocalPosition, glm::vec3(0.0f));
	CheckVec3(transform.LocalScale, glm::vec3(1.0f));
	CHECK(transform.Parent.IsNull());
	CHECK(scene.GetEnvironment().Exposure == doctest::Approx(EnvironmentSettings{}.Exposure));
}

TEST_CASE("SceneSerializer: parent cycles are broken instead of creating a loop")
{
	const Uuid firstUuid = Uuid::Generate();
	const Uuid secondUuid = Uuid::Generate();

	JsonValue document = MinimalDocument();

	JsonValue first = MinimalEntity(firstUuid, "First");
	JsonValue firstTransform = JsonValue::MakeObject();
	firstTransform.Set("parent", secondUuid.ToString());
	first.Set("transform", std::move(firstTransform));
	document["entities"].Push(std::move(first));

	JsonValue second = MinimalEntity(secondUuid, "Second");
	JsonValue secondTransform = JsonValue::MakeObject();
	secondTransform.Set("parent", firstUuid.ToString());
	second.Set("transform", std::move(secondTransform));
	document["entities"].Push(std::move(second));

	Scene scene;
	std::string error;
	REQUIRE(SceneSerializer::Deserialize(scene, document, &error));
	CHECK(error.empty());

	const Entity firstEntity = scene.FindEntity(firstUuid);
	const Entity secondEntity = scene.FindEntity(secondUuid);
	REQUIRE(firstEntity.IsValid());
	REQUIRE(secondEntity.IsValid());

	const TransformComponent& firstLoaded = firstEntity.GetComponent<TransformComponent>();
	const TransformComponent& secondLoaded = secondEntity.GetComponent<TransformComponent>();
	// The first link is accepted; the second one would close a loop and is dropped.
	CHECK(firstLoaded.Parent == secondUuid);
	CHECK(secondLoaded.Parent.IsNull());
	REQUIRE(secondLoaded.Children.size() == 1);
	CHECK(secondLoaded.Children[0] == firstUuid);
	CHECK(firstLoaded.Children.empty());
	CHECK(scene.GetEntityCount() == 2);
}

TEST_CASE("SceneSerializer: malformed documents report errors instead of throwing")
{
	SUBCASE("root is not an object")
	{
		Scene scene("Malformed");
		std::string error;
		CHECK_FALSE(SceneSerializer::Deserialize(scene, JsonValue(nullptr), &error));
		CHECK_FALSE(error.empty());
		CHECK(scene.IsEmpty());
	}

	SUBCASE("version is not a number")
	{
		Scene scene("Malformed");
		JsonValue document = MinimalDocument();
		document.Set("version", JsonValue("one"));

		std::string error;
		CHECK_FALSE(SceneSerializer::Deserialize(scene, document, &error));
		CHECK(error.find("version") != std::string::npos);
		CHECK(scene.IsEmpty());
	}

	SUBCASE("scene object is missing")
	{
		Scene scene("Malformed");
		JsonValue document = MinimalDocument();
		document.Remove("scene");

		std::string error;
		CHECK_FALSE(SceneSerializer::Deserialize(scene, document, &error));
		CHECK(error.find("scene") != std::string::npos);
		CHECK(scene.IsEmpty());
	}

	SUBCASE("entities array is missing")
	{
		Scene scene("Malformed");
		JsonValue document = MinimalDocument();
		document.Remove("entities");

		std::string error;
		CHECK_FALSE(SceneSerializer::Deserialize(scene, document, &error));
		CHECK(error.find("entities") != std::string::npos);
		CHECK(scene.IsEmpty());
	}

	SUBCASE("duplicate entity UUIDs are an error")
	{
		Scene scene("Malformed");
		const Uuid duplicated = Uuid::Generate();
		JsonValue document = MinimalDocument();
		document["entities"].Push(MinimalEntity(duplicated, "First"));
		document["entities"].Push(MinimalEntity(duplicated, "Second"));

		std::string error;
		CHECK_FALSE(SceneSerializer::Deserialize(scene, document, &error));
		CHECK(error.find("UUID") != std::string::npos);
		CHECK(scene.IsEmpty());
	}

	SUBCASE("an entity without a valid UUID is an error")
	{
		Scene scene("Malformed");
		JsonValue document = MinimalDocument();
		JsonValue entity = JsonValue::MakeObject();
		entity.Set("uuid", "not-a-uuid");
		entity.Set("name", "Broken");
		document["entities"].Push(std::move(entity));

		std::string error;
		CHECK_FALSE(SceneSerializer::Deserialize(scene, document, &error));
		CHECK(error.find("uuid") != std::string::npos);
		CHECK(scene.IsEmpty());
	}

	SUBCASE("an entity that is not an object is an error")
	{
		Scene scene("Malformed");
		JsonValue document = MinimalDocument();
		document["entities"].Push(7);

		std::string error;
		CHECK_FALSE(SceneSerializer::Deserialize(scene, document, &error));
		CHECK_FALSE(error.empty());
		CHECK(scene.IsEmpty());
	}

	SUBCASE("a component of the wrong type is an error")
	{
		Scene scene("Malformed");
		JsonValue document = MinimalDocument();
		JsonValue entity = MinimalEntity(Uuid::Generate(), "Broken");
		entity.Set("camera", 5);
		document["entities"].Push(std::move(entity));

		std::string error;
		CHECK_FALSE(SceneSerializer::Deserialize(scene, document, &error));
		CHECK(error.find("camera") != std::string::npos);
		CHECK(scene.IsEmpty());
	}
}

TEST_CASE("SceneSerializer: save and load round trip through a file")
{
	const FileSystem::Path directory = SerializationDirectory();
	const FileSystem::Path path = directory / "round-trip.aegis.json";

	Scene source("Saved Scene");
	BuildFullScene(source);

	REQUIRE(SceneSerializer::Save(source, path));
	CHECK(FileSystem::Exists(path));
	CHECK(FileSystem::IsFile(path));

	const std::string text = FileSystem::ReadTextFile(path);
	CHECK(text == SceneSerializer::Serialize(source).Dump(4) + "\n");
	CHECK(text.find("version") != std::string::npos);

	Scene loaded;
	std::string error;
	REQUIRE(SceneSerializer::Load(loaded, path, &error));
	CHECK(error.empty());
	CHECK(loaded.GetName() == source.GetName());
	CHECK(loaded.GetUuid() == source.GetUuid());
	CHECK(loaded.GetEntityCount() == source.GetEntityCount());
	CheckEnvironment(loaded.GetEnvironment(), source.GetEnvironment());

	const Entity cube = loaded.FindEntity("Cube");
	REQUIRE(cube.IsValid());
	REQUIRE(cube.HasComponent<MeshRendererComponent>());
	CHECK(cube.GetComponent<MeshRendererComponent>().Materials.size() == 2);

	// Saving into a nested directory creates the parents.
	const FileSystem::Path nested = directory / "nested" / "deeper" / "scene.aegis.json";
	CHECK(SceneSerializer::Save(source, nested));
	CHECK(FileSystem::Exists(nested));
}

TEST_CASE("SceneSerializer: missing or invalid files report errors")
{
	const FileSystem::Path directory = SerializationDirectory();
	const FileSystem::Path missing = directory / "missing.aegis.json";
	const FileSystem::Path broken = directory / "broken.aegis.json";
	const FileSystem::Path future = directory / "future.aegis.json";

	Scene scene("Intact Scene");
	scene.CreateEntity("Kept");

	std::string error;
	CHECK_FALSE(SceneSerializer::Load(scene, missing, &error));
	CHECK_FALSE(error.empty());
	CHECK(scene.GetEntityCount() == 1);

	REQUIRE(FileSystem::WriteTextFile(broken, "{ not json"));
	std::string parseError;
	CHECK_FALSE(SceneSerializer::Load(scene, broken, &parseError));
	CHECK_FALSE(parseError.empty());
	CHECK(scene.GetEntityCount() == 1);

	REQUIRE(FileSystem::WriteTextFile(future, "{\"version\": 42, \"scene\": {}, \"entities\": []}"));
	std::string versionError;
	CHECK_FALSE(SceneSerializer::Load(scene, future, &versionError));
	CHECK(versionError.find("42") != std::string::npos);
}

TEST_CASE("SceneSerializer: prefab helper keeps components and assigns a fresh UUID")
{
	Scene authoring("Prefab Source");
	BuildFullScene(authoring);
	const Entity cube = authoring.FindEntity("Cube");
	REQUIRE(cube.IsValid());

	const JsonValue prefab = SceneSerializer::SerializeEntity(cube);
	REQUIRE(prefab.IsObject());
	CHECK(prefab.GetInt("version") == 1);
	REQUIRE(prefab.Find("entity") != nullptr);
	CHECK(prefab["entity"].GetString("uuid") == cube.GetUuid().ToString());

	Scene target("Prefab Target");
	const Entity instance = SceneSerializer::DeserializeEntity(target, prefab);
	REQUIRE(instance.IsValid());
	CHECK(target.GetEntityCount() == 1);
	CHECK(target.FindEntity(instance.GetUuid()) == instance);
	CHECK(instance.GetUuid() != cube.GetUuid());
	CHECK(instance.GetName() == cube.GetName());
	CHECK(instance.IsActive() == cube.IsActive());

	REQUIRE(instance.HasComponent<IdComponent>());
	REQUIRE(instance.HasComponent<TagComponent>());
	REQUIRE(instance.HasComponent<TransformComponent>());
	REQUIRE(instance.HasComponent<MeshRendererComponent>());
	REQUIRE(instance.HasComponent<RigidBodyComponent>());
	REQUIRE(instance.HasComponent<ColliderComponent>());
	REQUIRE(instance.HasComponent<LuaScriptComponent>());
	REQUIRE(instance.HasComponent<AudioSourceComponent>());

	const TransformComponent& sourceTransform = cube.GetComponent<TransformComponent>();
	const TransformComponent& instanceTransform = instance.GetComponent<TransformComponent>();
	CHECK(instanceTransform.Parent.IsNull());
	CHECK(instanceTransform.Children.empty());
	CheckVec3(instanceTransform.LocalPosition, sourceTransform.LocalPosition);

	const MeshRendererComponent& sourceRenderer = cube.GetComponent<MeshRendererComponent>();
	const MeshRendererComponent& instanceRenderer = instance.GetComponent<MeshRendererComponent>();
	CheckHandle(instanceRenderer.Mesh, sourceRenderer.Mesh);
	REQUIRE(instanceRenderer.Materials.size() == 2);
	CheckHandle(instanceRenderer.Materials[0], sourceRenderer.Materials[0]);
	CheckHandle(instanceRenderer.Materials[1], sourceRenderer.Materials[1]);

	const RigidBodyComponent& sourceBody = cube.GetComponent<RigidBodyComponent>();
	const RigidBodyComponent& instanceBody = instance.GetComponent<RigidBodyComponent>();
	CHECK(instanceBody.Type == sourceBody.Type);
	CHECK(instanceBody.Mass == doctest::Approx(sourceBody.Mass).epsilon(0.0001));

	const LuaScriptComponent& instanceScript = instance.GetComponent<LuaScriptComponent>();
	const LuaScriptComponent& sourceScript = cube.GetComponent<LuaScriptComponent>();
	CheckHandle(instanceScript.Script, sourceScript.Script);
	CHECK(instanceScript.Properties.GetFloat("speed") == doctest::Approx(3.5f));
	CHECK(instanceScript.Properties.GetString("label") == "turret");

	// A bare entity object is accepted as a prefab source as well.
	JsonValue bare = prefab["entity"];
	Scene secondTarget("Prefab Target 2");
	const Entity second = SceneSerializer::DeserializeEntity(secondTarget, bare);
	REQUIRE(second.IsValid());
	CHECK(second.GetUuid() != instance.GetUuid());
	CHECK(secondTarget.GetEntityCount() == 1);
}

TEST_CASE("SceneSerializer: prefab helper rejects malformed documents")
{
	Scene scene;

	CHECK_FALSE(SceneSerializer::DeserializeEntity(scene, JsonValue(nullptr)).IsValid());
	CHECK(SceneSerializer::SerializeEntity(Entity()).IsNull());

	JsonValue future = JsonValue::MakeObject();
	future.Set("version", 99);
	future.Set("entity", JsonValue::MakeObject());
	CHECK_FALSE(SceneSerializer::DeserializeEntity(scene, future).IsValid());

	JsonValue missingEntity = JsonValue::MakeObject();
	missingEntity.Set("version", 1);
	CHECK_FALSE(SceneSerializer::DeserializeEntity(scene, missingEntity).IsValid());

	JsonValue wrongComponent = JsonValue::MakeObject();
	wrongComponent.Set("version", 1);
	JsonValue entity = JsonValue::MakeObject();
	entity.Set("name", "Broken Prefab");
	entity.Set("meshRenderer", 5);
	wrongComponent.Set("entity", std::move(entity));
	CHECK_FALSE(SceneSerializer::DeserializeEntity(scene, wrongComponent).IsValid());

	CHECK(scene.IsEmpty());
}
