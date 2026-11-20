#include "DemoScene.h"

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Json.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Project/Project.h"
#include "Aegis/Scene/Scene.h"
#include "Aegis/Serialization/SceneSerializer.h"

namespace aegis::demo
{
	namespace
	{
		Entity CreateEntity(Scene& scene, const char* name, const glm::vec3& position)
		{
			Entity entity = scene.CreateEntity(name);
			entity.GetComponent<TransformComponent>().LocalPosition = position;
			return entity;
		}

		void AddMesh(Entity entity, bool castShadows = true)
		{
			MeshRendererComponent renderer;
			renderer.CastShadows = castShadows;
			renderer.Visible = true;
			entity.AddComponent<MeshRendererComponent>(renderer);
		}
	}

	bool WriteFeatureProject(const std::filesystem::path& projectDirectory, std::string* outError)
	{
		const auto error = [outError](const std::string& message)
		{
			if (outError != nullptr)
			{
				*outError = message;
			}
			AE_CORE_ERROR("{}", message);
			return false;
		};

		if (!FileSystem::CreateDirectories(projectDirectory / "Scenes") ||
			!FileSystem::CreateDirectories(projectDirectory / "Scripts"))
		{
			return error("Could not create the project directories");
		}

		Scene scene("Feature Test");
		EnvironmentSettings& environment = scene.GetEnvironment();
		environment.Exposure = 1.1f;
		environment.EnableIBL = true;
		environment.EnableShadows = true;
		environment.EnableSSAO = true;
		environment.EnableBloom = true;
		environment.ShadowDistance = 80.0f;

		Entity camera = CreateEntity(scene, "Main Camera", glm::vec3(0.0f, 3.0f, 12.0f));
		CameraComponent cameraComponent;
		cameraComponent.Primary = true;
		cameraComponent.FieldOfView = 55.0f;
		camera.AddComponent<CameraComponent>(cameraComponent);
		camera.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-12.0f, 0.0f, 0.0f));
		camera.AddComponent<AudioListenerComponent>(AudioListenerComponent{ true });
		camera.AddComponent<FlyCameraComponent>(FlyCameraComponent{});

		Entity sun = CreateEntity(scene, "Sun", glm::vec3(0.0f, 20.0f, 0.0f));
		LightComponent sunLight;
		sunLight.Type = LightType::Directional;
		sunLight.Intensity = 3.2f;
		sunLight.Color = glm::vec3(1.0f, 0.95f, 0.86f);
		sunLight.CastShadows = true;
		sun.AddComponent<LightComponent>(sunLight);
		sun.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-48.0f, 32.0f, 0.0f));

		Entity pointLight = CreateEntity(scene, "Point Light", glm::vec3(4.0f, 2.5f, 2.0f));
		LightComponent point;
		point.Type = LightType::Point;
		point.Intensity = 18.0f;
		point.Range = 12.0f;
		point.Color = glm::vec3(0.35f, 0.55f, 1.0f);
		point.CastShadows = false;
		pointLight.AddComponent<LightComponent>(point);

		Entity spotLight = CreateEntity(scene, "Spot Light", glm::vec3(-4.0f, 5.0f, 3.0f));
		LightComponent spot;
		spot.Type = LightType::Spot;
		spot.Intensity = 40.0f;
		spot.Range = 20.0f;
		spot.InnerConeDegrees = 18.0f;
		spot.OuterConeDegrees = 34.0f;
		spot.Color = glm::vec3(1.0f, 0.6f, 0.3f);
		spotLight.AddComponent<LightComponent>(spot);
		spotLight.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(-65.0f, 0.0f, 0.0f));

		Entity ground = CreateEntity(scene, "Ground", glm::vec3(0.0f, -0.1f, 0.0f));
		AddMesh(ground, false);
		ground.GetComponent<TransformComponent>().LocalScale = glm::vec3(30.0f, 0.2f, 30.0f);
		RigidBodyComponent groundBody;
		groundBody.Type = BodyType::Static;
		groundBody.Friction = 0.8f;
		ground.AddComponent<RigidBodyComponent>(groundBody);
		ColliderComponent groundCollider;
		groundCollider.Shape = ColliderShape::Box;
		groundCollider.HalfExtents = glm::vec3(30.0f, 0.2f, 30.0f);
		ground.AddComponent<ColliderComponent>(groundCollider);

		Entity fallingCube = CreateEntity(scene, "Falling Cube", glm::vec3(0.0f, 4.0f, 0.0f));
		AddMesh(fallingCube);
		RigidBodyComponent cubeBody;
		cubeBody.Type = BodyType::Dynamic;
		cubeBody.Mass = 1.5f;
		cubeBody.Restitution = 0.25f;
		fallingCube.AddComponent<RigidBodyComponent>(cubeBody);
		ColliderComponent cubeCollider;
		cubeCollider.HalfExtents = glm::vec3(0.5f);
		fallingCube.AddComponent<ColliderComponent>(cubeCollider);
		LuaScriptComponent cubeScript;
		cubeScript.Script.Type = AssetType::Script;
		JsonValue properties = JsonValue::MakeObject();
		properties.Set("speed", 45.0);
		properties.Set("bobHeight", 0.25);
		cubeScript.Properties = properties;
		fallingCube.AddComponent<LuaScriptComponent>(cubeScript);

		Entity sphere = CreateEntity(scene, "Sphere", glm::vec3(2.2f, 1.0f, 0.0f));
		AddMesh(sphere);
		RigidBodyComponent sphereBody;
		sphereBody.Type = BodyType::Dynamic;
		sphereBody.Mass = 0.8f;
		sphere.AddComponent<RigidBodyComponent>(sphereBody);
		ColliderComponent sphereCollider;
		sphereCollider.Shape = ColliderShape::Sphere;
		sphereCollider.Radius = 0.5f;
		sphere.AddComponent<ColliderComponent>(sphereCollider);

		Entity parent = CreateEntity(scene, "Camera Rig", glm::vec3(-3.0f, 0.0f, 0.0f));
		parent.GetComponent<TransformComponent>().SetLocalEulerAngles(glm::vec3(0.0f, 25.0f, 0.0f));
		Entity child = CreateEntity(scene, "Rig Child", glm::vec3(0.0f, 2.0f, 0.0f));
		child.SetParent(parent);
		AddMesh(child, false);

		Entity audio = CreateEntity(scene, "Ambience", glm::vec3(0.0f, 2.0f, 0.0f));
		AudioSourceComponent source;
		source.Volume = 0.6f;
		source.Loop = true;
		source.PlayOnStart = true;
		source.Spatial = true;
		audio.AddComponent<AudioSourceComponent>(source);

		Entity trigger = CreateEntity(scene, "Trigger Volume", glm::vec3(0.0f, 1.0f, -3.0f));
		AddMesh(trigger, false);
		RigidBodyComponent triggerBody;
		triggerBody.Type = BodyType::Static;
		trigger.AddComponent<RigidBodyComponent>(triggerBody);
		ColliderComponent triggerCollider;
		triggerCollider.IsTrigger = true;
		triggerCollider.HalfExtents = glm::vec3(1.5f);
		trigger.AddComponent<ColliderComponent>(triggerCollider);

		scene.UpdateTransforms();

		const auto scenePath = projectDirectory / "Scenes" / "FeatureTest.aescene";
		if (!SceneSerializer::Save(scene, scenePath))
		{
			return error(std::format("Could not write the feature scene to '{}'", FileSystem::ToUtf8(scenePath)));
		}

		const std::string spinnerScript =
			"-- Rotates the entity and gently bobs it up and down.\n"
			"-- Exercises engine.time, engine.properties and entity transforms.\n"
			"local entity = nil\n"
			"local elapsed = 0.0\n"
			"\n"
			"function OnStart(self)\n"
			"    entity = self\n"
			"    elapsed = 0.0\n"
			"    engine.log.info('Spinner started on ' .. entity:name())\n"
			"end\n"
			"\n"
			"function OnUpdate(self, deltaTime)\n"
			"    elapsed = elapsed + deltaTime\n"
			"    local degrees = elapsed * (engine.properties.speed or 45.0)\n"
			"    local height = (engine.properties.bobHeight or 0.25) * math.sin(elapsed * 2.0)\n"
			"    entity:setRotation({ x = 0.0, y = degrees, z = 0.0 })\n"
			"    local position = entity:position()\n"
			"    position.y = position.y + height\n"
			"    entity:setPosition(position)\n"
			"end\n"
			"\n"
			"function OnDestroy(self)\n"
			"    engine.log.info('Spinner destroyed on ' .. self:name())\n"
			"end\n";
		if (!FileSystem::WriteTextFile(projectDirectory / "Scripts" / "Spinner.lua", spinnerScript))
		{
			return error("Could not write the Spinner script");
		}

		Project project;
		project.Name = "FeatureTest";
		project.StartScene = "Scenes/FeatureTest.aescene";
		project.WindowTitle = "Aegis Feature Test";
		project.RootDirectory = projectDirectory;
		if (!project.Save(projectDirectory / "FeatureTest.aeproj", outError))
		{
			return false;
		}

		AE_CORE_INFO("Feature project written to '{}' ({} entities)", FileSystem::ToUtf8(projectDirectory),
			scene.GetEntityCount());
		return true;
	}
}
