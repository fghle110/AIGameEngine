#include "Aegis/Serialization/SceneSerializer.h"

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Core/StringUtils.h"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aegis
{
	namespace
	{
		// --- JSON value helpers ------------------------------------------------

		JsonValue vec3ToJson(const glm::vec3& value)
		{
			JsonValue array = JsonValue::MakeArray();
			array.Push(value.x);
			array.Push(value.y);
			array.Push(value.z);
			return array;
		}

		glm::vec3 vec3FromJson(const JsonValue* value, const glm::vec3& fallback)
		{
			if (value == nullptr || !value->IsArray() || value->GetSize() < 3)
			{
				return fallback;
			}
			return glm::vec3((*value)[0].AsFloat(fallback.x), (*value)[1].AsFloat(fallback.y),
				(*value)[2].AsFloat(fallback.z));
		}

		JsonValue vec4ToJson(const glm::vec4& value)
		{
			JsonValue array = JsonValue::MakeArray();
			array.Push(value.x);
			array.Push(value.y);
			array.Push(value.z);
			array.Push(value.w);
			return array;
		}

		glm::vec4 vec4FromJson(const JsonValue* value, const glm::vec4& fallback)
		{
			if (value == nullptr || !value->IsArray() || value->GetSize() < 4)
			{
				return fallback;
			}
			return glm::vec4((*value)[0].AsFloat(fallback.x), (*value)[1].AsFloat(fallback.y),
				(*value)[2].AsFloat(fallback.z), (*value)[3].AsFloat(fallback.w));
		}

		JsonValue quatToJson(const glm::quat& value)
		{
			JsonValue array = JsonValue::MakeArray();
			array.Push(value.x);
			array.Push(value.y);
			array.Push(value.z);
			array.Push(value.w);
			return array;
		}

		glm::quat quatFromJson(const JsonValue* value, const glm::quat& fallback)
		{
			if (value == nullptr || !value->IsArray() || value->GetSize() < 4)
			{
				return fallback;
			}
			glm::quat rotation((*value)[3].AsFloat(fallback.w), (*value)[0].AsFloat(fallback.x),
				(*value)[1].AsFloat(fallback.y), (*value)[2].AsFloat(fallback.z));

			// Authored data is trusted, but a rotation that lost precision (or was
			// scaled) must not shear the transform hierarchy.
			const f32 lengthSquared = rotation.w * rotation.w + rotation.x * rotation.x + rotation.y * rotation.y +
				rotation.z * rotation.z;
			if (lengthSquared > 1.0e-8f && std::fabs(lengthSquared - 1.0f) > 1.0e-6f)
			{
				rotation = glm::normalize(rotation);
			}
			return rotation;
		}

		JsonValue uuidToJson(const Uuid& uuid)
		{
			return uuid.IsNull() ? JsonValue(nullptr) : JsonValue(uuid.ToString());
		}

		Uuid uuidFromJson(const JsonValue* value)
		{
			if (value == nullptr || !value->IsString())
			{
				return Uuid::Null();
			}
			return Uuid::FromString(value->AsString());
		}

		JsonValue assetToJson(const AssetHandle& handle)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("id", uuidToJson(handle.Id));
			object.Set("type", JsonValue(AssetTypeToString(handle.Type)));
			return object;
		}

		AssetHandle assetFromJson(const JsonValue* value)
		{
			AssetHandle handle;
			if (value == nullptr || !value->IsObject())
			{
				return handle;
			}
			handle.Id = uuidFromJson(value->Find("id"));
			handle.Type = AssetTypeFromString(value->GetString("type"));
			return handle;
		}

		// --- Enum names --------------------------------------------------------

		std::string_view cameraProjectionToString(CameraProjection projection)
		{
			switch (projection)
			{
				case CameraProjection::Perspective: return "perspective";
				case CameraProjection::Orthographic: return "orthographic";
			}
			return "perspective";
		}

		CameraProjection cameraProjectionFromString(std::string_view text, std::string_view owner)
		{
			if (StringUtils::EqualsIgnoreCase(text, "orthographic"))
			{
				return CameraProjection::Orthographic;
			}
			if (!StringUtils::EqualsIgnoreCase(text, "perspective"))
			{
				AE_CORE_WARN("Unknown camera projection '{}' on entity '{}'; using 'perspective'", text, owner);
			}
			return CameraProjection::Perspective;
		}

		std::string_view lightTypeToString(LightType type)
		{
			switch (type)
			{
				case LightType::Directional: return "directional";
				case LightType::Point: return "point";
				case LightType::Spot: return "spot";
			}
			return "directional";
		}

		LightType lightTypeFromString(std::string_view text, std::string_view owner)
		{
			if (StringUtils::EqualsIgnoreCase(text, "point"))
			{
				return LightType::Point;
			}
			if (StringUtils::EqualsIgnoreCase(text, "spot"))
			{
				return LightType::Spot;
			}
			if (!StringUtils::EqualsIgnoreCase(text, "directional"))
			{
				AE_CORE_WARN("Unknown light type '{}' on entity '{}'; using 'directional'", text, owner);
			}
			return LightType::Directional;
		}

		std::string_view bodyTypeToString(BodyType type)
		{
			switch (type)
			{
				case BodyType::Static: return "static";
				case BodyType::Kinematic: return "kinematic";
				case BodyType::Dynamic: return "dynamic";
			}
			return "static";
		}

		BodyType bodyTypeFromString(std::string_view text, std::string_view owner)
		{
			if (StringUtils::EqualsIgnoreCase(text, "kinematic"))
			{
				return BodyType::Kinematic;
			}
			if (StringUtils::EqualsIgnoreCase(text, "dynamic"))
			{
				return BodyType::Dynamic;
			}
			if (!StringUtils::EqualsIgnoreCase(text, "static"))
			{
				AE_CORE_WARN("Unknown rigid body type '{}' on entity '{}'; using 'static'", text, owner);
			}
			return BodyType::Static;
		}

		std::string_view colliderShapeToString(ColliderShape shape)
		{
			switch (shape)
			{
				case ColliderShape::Box: return "box";
				case ColliderShape::Sphere: return "sphere";
				case ColliderShape::Capsule: return "capsule";
				case ColliderShape::Mesh: return "mesh";
			}
			return "box";
		}

		ColliderShape colliderShapeFromString(std::string_view text, std::string_view owner)
		{
			if (StringUtils::EqualsIgnoreCase(text, "sphere"))
			{
				return ColliderShape::Sphere;
			}
			if (StringUtils::EqualsIgnoreCase(text, "capsule"))
			{
				return ColliderShape::Capsule;
			}
			if (StringUtils::EqualsIgnoreCase(text, "mesh"))
			{
				return ColliderShape::Mesh;
			}
			if (!StringUtils::EqualsIgnoreCase(text, "box"))
			{
				AE_CORE_WARN("Unknown collider shape '{}' on entity '{}'; using 'box'", text, owner);
			}
			return ColliderShape::Box;
		}

		// --- Component serialisation -------------------------------------------

		JsonValue cameraToJson(const CameraComponent& camera)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("projection", JsonValue(cameraProjectionToString(camera.Projection)));
			object.Set("fieldOfView", camera.FieldOfView);
			object.Set("nearClip", camera.NearClip);
			object.Set("farClip", camera.FarClip);
			object.Set("orthographicSize", camera.OrthographicSize);
			object.Set("primary", camera.Primary);
			object.Set("clearColor", vec4ToJson(camera.ClearColor));
			return object;
		}

		CameraComponent cameraFromJson(const JsonValue& value, std::string_view owner)
		{
			CameraComponent camera;
			camera.Projection = cameraProjectionFromString(value.GetString("projection", "perspective"), owner);
			camera.FieldOfView = value.GetFloat("fieldOfView", camera.FieldOfView);
			camera.NearClip = value.GetFloat("nearClip", camera.NearClip);
			camera.FarClip = value.GetFloat("farClip", camera.FarClip);
			camera.OrthographicSize = value.GetFloat("orthographicSize", camera.OrthographicSize);
			camera.Primary = value.GetBool("primary", camera.Primary);
			camera.ClearColor = vec4FromJson(value.Find("clearColor"), camera.ClearColor);
			return camera;
		}

		JsonValue lightToJson(const LightComponent& light)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("type", JsonValue(lightTypeToString(light.Type)));
			object.Set("color", vec3ToJson(light.Color));
			object.Set("intensity", light.Intensity);
			object.Set("range", light.Range);
			object.Set("innerConeDegrees", light.InnerConeDegrees);
			object.Set("outerConeDegrees", light.OuterConeDegrees);
			object.Set("castShadows", light.CastShadows);
			return object;
		}

		LightComponent lightFromJson(const JsonValue& value, std::string_view owner)
		{
			LightComponent light;
			light.Type = lightTypeFromString(value.GetString("type", "directional"), owner);
			light.Color = vec3FromJson(value.Find("color"), light.Color);
			light.Intensity = value.GetFloat("intensity", light.Intensity);
			light.Range = value.GetFloat("range", light.Range);
			light.InnerConeDegrees = value.GetFloat("innerConeDegrees", light.InnerConeDegrees);
			light.OuterConeDegrees = value.GetFloat("outerConeDegrees", light.OuterConeDegrees);
			light.CastShadows = value.GetBool("castShadows", light.CastShadows);
			return light;
		}

		JsonValue meshRendererToJson(const MeshRendererComponent& renderer)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("mesh", assetToJson(renderer.Mesh));

			JsonValue materials = JsonValue::MakeArray();
			for (const AssetHandle& material : renderer.Materials)
			{
				materials.Push(assetToJson(material));
			}
			object.Set("materials", std::move(materials));
			object.Set("visible", renderer.Visible);
			object.Set("castShadows", renderer.CastShadows);
			object.Set("receiveShadows", renderer.ReceiveShadows);
			return object;
		}

		MeshRendererComponent meshRendererFromJson(const JsonValue& value)
		{
			MeshRendererComponent renderer;
			renderer.Mesh = assetFromJson(value.Find("mesh"));

			const JsonValue* materials = value.Find("materials");
			if (materials != nullptr && materials->IsArray())
			{
				renderer.Materials.reserve(materials->GetSize());
				for (usize index = 0; index < materials->GetSize(); ++index)
				{
					renderer.Materials.push_back(assetFromJson(&(*materials)[index]));
				}
			}

			renderer.Visible = value.GetBool("visible", renderer.Visible);
			renderer.CastShadows = value.GetBool("castShadows", renderer.CastShadows);
			renderer.ReceiveShadows = value.GetBool("receiveShadows", renderer.ReceiveShadows);
			return renderer;
		}

		JsonValue rigidBodyToJson(const RigidBodyComponent& body)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("type", JsonValue(bodyTypeToString(body.Type)));
			object.Set("mass", body.Mass);
			object.Set("friction", body.Friction);
			object.Set("restitution", body.Restitution);
			object.Set("linearDamping", body.LinearDamping);
			object.Set("angularDamping", body.AngularDamping);
			object.Set("useGravity", body.UseGravity);
			return object;
		}

		RigidBodyComponent rigidBodyFromJson(const JsonValue& value, std::string_view owner)
		{
			RigidBodyComponent body;
			body.Type = bodyTypeFromString(value.GetString("type", "static"), owner);
			body.Mass = value.GetFloat("mass", body.Mass);
			body.Friction = value.GetFloat("friction", body.Friction);
			body.Restitution = value.GetFloat("restitution", body.Restitution);
			body.LinearDamping = value.GetFloat("linearDamping", body.LinearDamping);
			body.AngularDamping = value.GetFloat("angularDamping", body.AngularDamping);
			body.UseGravity = value.GetBool("useGravity", body.UseGravity);
			return body;
		}

		JsonValue colliderToJson(const ColliderComponent& collider)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("shape", JsonValue(colliderShapeToString(collider.Shape)));
			object.Set("halfExtents", vec3ToJson(collider.HalfExtents));
			object.Set("radius", collider.Radius);
			object.Set("halfHeight", collider.HalfHeight);
			object.Set("offset", vec3ToJson(collider.Offset));
			object.Set("isTrigger", collider.IsTrigger);
			return object;
		}

		ColliderComponent colliderFromJson(const JsonValue& value, std::string_view owner)
		{
			ColliderComponent collider;
			collider.Shape = colliderShapeFromString(value.GetString("shape", "box"), owner);
			collider.HalfExtents = vec3FromJson(value.Find("halfExtents"), collider.HalfExtents);
			collider.Radius = value.GetFloat("radius", collider.Radius);
			collider.HalfHeight = value.GetFloat("halfHeight", collider.HalfHeight);
			collider.Offset = vec3FromJson(value.Find("offset"), collider.Offset);
			collider.IsTrigger = value.GetBool("isTrigger", collider.IsTrigger);
			return collider;
		}

		JsonValue luaScriptToJson(const LuaScriptComponent& script)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("script", assetToJson(script.Script));
			object.Set("enabled", script.Enabled);
			object.Set("properties", script.Properties.IsObject() ? script.Properties : JsonValue::MakeObject());
			return object;
		}

		LuaScriptComponent luaScriptFromJson(const JsonValue& value)
		{
			LuaScriptComponent script;
			script.Script = assetFromJson(value.Find("script"));
			script.Enabled = value.GetBool("enabled", script.Enabled);

			const JsonValue* properties = value.Find("properties");
			script.Properties =
				(properties != nullptr && properties->IsObject()) ? *properties : JsonValue::MakeObject();
			return script;
		}

		JsonValue audioSourceToJson(const AudioSourceComponent& source)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("clip", assetToJson(source.Clip));
			object.Set("volume", source.Volume);
			object.Set("pitch", source.Pitch);
			object.Set("loop", source.Loop);
			object.Set("playOnStart", source.PlayOnStart);
			object.Set("spatial", source.Spatial);
			object.Set("minDistance", source.MinDistance);
			object.Set("maxDistance", source.MaxDistance);
			return object;
		}

		AudioSourceComponent audioSourceFromJson(const JsonValue& value)
		{
			AudioSourceComponent source;
			source.Clip = assetFromJson(value.Find("clip"));
			source.Volume = value.GetFloat("volume", source.Volume);
			source.Pitch = value.GetFloat("pitch", source.Pitch);
			source.Loop = value.GetBool("loop", source.Loop);
			source.PlayOnStart = value.GetBool("playOnStart", source.PlayOnStart);
			source.Spatial = value.GetBool("spatial", source.Spatial);
			source.MinDistance = value.GetFloat("minDistance", source.MinDistance);
			source.MaxDistance = value.GetFloat("maxDistance", source.MaxDistance);
			return source;
		}

		JsonValue audioListenerToJson(const AudioListenerComponent& listener)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("primary", listener.Primary);
			return object;
		}

		AudioListenerComponent audioListenerFromJson(const JsonValue& value)
		{
			AudioListenerComponent listener;
			listener.Primary = value.GetBool("primary", listener.Primary);
			return listener;
		}

		JsonValue flyCameraToJson(const FlyCameraComponent& flyCamera)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("active", flyCamera.Active);
			object.Set("moveSpeed", flyCamera.MoveSpeed);
			object.Set("fastMultiplier", flyCamera.FastMultiplier);
			object.Set("lookSensitivity", flyCamera.LookSensitivity);
			object.Set("speedDamping", flyCamera.SpeedDamping);
			object.Set("velocity", vec3ToJson(flyCamera.Velocity));
			return object;
		}

		FlyCameraComponent flyCameraFromJson(const JsonValue& value)
		{
			FlyCameraComponent flyCamera;
			flyCamera.Active = value.GetBool("active", flyCamera.Active);
			flyCamera.MoveSpeed = value.GetFloat("moveSpeed", flyCamera.MoveSpeed);
			flyCamera.FastMultiplier = value.GetFloat("fastMultiplier", flyCamera.FastMultiplier);
			flyCamera.LookSensitivity = value.GetFloat("lookSensitivity", flyCamera.LookSensitivity);
			flyCamera.SpeedDamping = value.GetFloat("speedDamping", flyCamera.SpeedDamping);
			flyCamera.Velocity = vec3FromJson(value.Find("velocity"), flyCamera.Velocity);
			return flyCamera;
		}

		JsonValue transformToJson(const TransformComponent& transform, bool includeHierarchy)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("position", vec3ToJson(transform.LocalPosition));
			object.Set("rotation", quatToJson(transform.LocalRotation));
			object.Set("scale", vec3ToJson(transform.LocalScale));

			JsonValue children = JsonValue::MakeArray();
			if (includeHierarchy)
			{
				for (const Uuid& child : transform.Children)
				{
					children.Push(child.ToString());
				}
			}
			object.Set("parent", includeHierarchy ? uuidToJson(transform.Parent) : JsonValue(nullptr));
			object.Set("children", std::move(children));
			return object;
		}

		JsonValue environmentToJson(const EnvironmentSettings& environment)
		{
			JsonValue object = JsonValue::MakeObject();
			object.Set("ambientColor", vec3ToJson(environment.AmbientColor));
			object.Set("ambientIntensity", environment.AmbientIntensity);
			object.Set("hdriPath", environment.HdriPath);
			object.Set("hdriIntensity", environment.HdriIntensity);
			object.Set("exposure", environment.Exposure);
			object.Set("enableIBL", environment.EnableIBL);
			object.Set("enableShadows", environment.EnableShadows);
			object.Set("enableSSAO", environment.EnableSSAO);
			object.Set("enableBloom", environment.EnableBloom);
			object.Set("enableFXAA", environment.EnableFXAA);
			object.Set("shadowDistance", environment.ShadowDistance);
			object.Set("ssaoRadius", environment.SSAORadius);
			object.Set("bloomThreshold", environment.BloomThreshold);
			object.Set("bloomIntensity", environment.BloomIntensity);
			return object;
		}

		EnvironmentSettings environmentFromJson(const JsonValue& value)
		{
			EnvironmentSettings environment;
			environment.AmbientColor = vec3FromJson(value.Find("ambientColor"), environment.AmbientColor);
			environment.AmbientIntensity = value.GetFloat("ambientIntensity", environment.AmbientIntensity);
			environment.HdriPath = value.GetString("hdriPath", environment.HdriPath);
			environment.HdriIntensity = value.GetFloat("hdriIntensity", environment.HdriIntensity);
			environment.Exposure = value.GetFloat("exposure", environment.Exposure);
			environment.EnableIBL = value.GetBool("enableIBL", environment.EnableIBL);
			environment.EnableShadows = value.GetBool("enableShadows", environment.EnableShadows);
			environment.EnableSSAO = value.GetBool("enableSSAO", environment.EnableSSAO);
			environment.EnableBloom = value.GetBool("enableBloom", environment.EnableBloom);
			environment.EnableFXAA = value.GetBool("enableFXAA", environment.EnableFXAA);
			environment.ShadowDistance = value.GetFloat("shadowDistance", environment.ShadowDistance);
			environment.SSAORadius = value.GetFloat("ssaoRadius", environment.SSAORadius);
			environment.BloomThreshold = value.GetFloat("bloomThreshold", environment.BloomThreshold);
			environment.BloomIntensity = value.GetFloat("bloomIntensity", environment.BloomIntensity);
			return environment;
		}

		// --- Entity serialisation ----------------------------------------------

		JsonValue entityToJson(const Entity& entity, bool includeHierarchy)
		{
			static const TransformComponent s_DefaultTransform;

			JsonValue object = JsonValue::MakeObject();
			object.Set("uuid", entity.GetUuid().ToString());
			object.Set("name", entity.GetName());
			object.Set("active", entity.IsActive());

			const TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
			object.Set("transform", transformToJson(transform != nullptr ? *transform : s_DefaultTransform,
				includeHierarchy));

			if (const CameraComponent* camera = entity.TryGetComponent<CameraComponent>())
			{
				object.Set("camera", cameraToJson(*camera));
			}
			if (const LightComponent* light = entity.TryGetComponent<LightComponent>())
			{
				object.Set("light", lightToJson(*light));
			}
			if (const MeshRendererComponent* renderer = entity.TryGetComponent<MeshRendererComponent>())
			{
				object.Set("meshRenderer", meshRendererToJson(*renderer));
			}
			if (const RigidBodyComponent* body = entity.TryGetComponent<RigidBodyComponent>())
			{
				object.Set("rigidBody", rigidBodyToJson(*body));
			}
			if (const ColliderComponent* collider = entity.TryGetComponent<ColliderComponent>())
			{
				object.Set("collider", colliderToJson(*collider));
			}
			if (const LuaScriptComponent* script = entity.TryGetComponent<LuaScriptComponent>())
			{
				object.Set("luaScript", luaScriptToJson(*script));
			}
			if (const AudioSourceComponent* source = entity.TryGetComponent<AudioSourceComponent>())
			{
				object.Set("audioSource", audioSourceToJson(*source));
			}
			if (const AudioListenerComponent* listener = entity.TryGetComponent<AudioListenerComponent>())
			{
				object.Set("audioListener", audioListenerToJson(*listener));
			}
			if (const FlyCameraComponent* flyCamera = entity.TryGetComponent<FlyCameraComponent>())
			{
				object.Set("flyCamera", flyCameraToJson(*flyCamera));
			}
			return object;
		}

		bool malformedComponent(std::string& outError, const Entity& entity, std::string_view key)
		{
			outError = std::format("entity '{}' has a malformed '{}' member (expected an object)", entity.GetName(),
				key);
			return false;
		}

		// Restores the local transform and every optional component of one entity.
		// Hierarchy links are restored separately, once every entity exists.
		bool applyComponents(Entity entity, const JsonValue& document, std::string& outError)
		{
			const std::string owner = entity.GetName();

			const JsonValue* transformValue = document.Find("transform");
			if (transformValue != nullptr)
			{
				if (!transformValue->IsObject())
				{
					return malformedComponent(outError, entity, "transform");
				}
				TransformComponent& transform = entity.GetComponent<TransformComponent>();
				transform.LocalPosition = vec3FromJson(transformValue->Find("position"), transform.LocalPosition);
				transform.LocalRotation = quatFromJson(transformValue->Find("rotation"), transform.LocalRotation);
				transform.LocalScale = vec3FromJson(transformValue->Find("scale"), transform.LocalScale);
			}

			if (const JsonValue* cameraValue = document.Find("camera"))
			{
				if (!cameraValue->IsObject())
				{
					return malformedComponent(outError, entity, "camera");
				}
				entity.AddComponent<CameraComponent>(cameraFromJson(*cameraValue, owner));
			}
			if (const JsonValue* lightValue = document.Find("light"))
			{
				if (!lightValue->IsObject())
				{
					return malformedComponent(outError, entity, "light");
				}
				entity.AddComponent<LightComponent>(lightFromJson(*lightValue, owner));
			}
			if (const JsonValue* rendererValue = document.Find("meshRenderer"))
			{
				if (!rendererValue->IsObject())
				{
					return malformedComponent(outError, entity, "meshRenderer");
				}
				entity.AddComponent<MeshRendererComponent>(meshRendererFromJson(*rendererValue));
			}
			if (const JsonValue* bodyValue = document.Find("rigidBody"))
			{
				if (!bodyValue->IsObject())
				{
					return malformedComponent(outError, entity, "rigidBody");
				}
				entity.AddComponent<RigidBodyComponent>(rigidBodyFromJson(*bodyValue, owner));
			}
			if (const JsonValue* colliderValue = document.Find("collider"))
			{
				if (!colliderValue->IsObject())
				{
					return malformedComponent(outError, entity, "collider");
				}
				entity.AddComponent<ColliderComponent>(colliderFromJson(*colliderValue, owner));
			}
			if (const JsonValue* scriptValue = document.Find("luaScript"))
			{
				if (!scriptValue->IsObject())
				{
					return malformedComponent(outError, entity, "luaScript");
				}
				entity.AddComponent<LuaScriptComponent>(luaScriptFromJson(*scriptValue));
			}
			if (const JsonValue* sourceValue = document.Find("audioSource"))
			{
				if (!sourceValue->IsObject())
				{
					return malformedComponent(outError, entity, "audioSource");
				}
				entity.AddComponent<AudioSourceComponent>(audioSourceFromJson(*sourceValue));
			}
			if (const JsonValue* listenerValue = document.Find("audioListener"))
			{
				if (!listenerValue->IsObject())
				{
					return malformedComponent(outError, entity, "audioListener");
				}
				entity.AddComponent<AudioListenerComponent>(audioListenerFromJson(*listenerValue));
			}
			if (const JsonValue* flyCameraValue = document.Find("flyCamera"))
			{
				if (!flyCameraValue->IsObject())
				{
					return malformedComponent(outError, entity, "flyCamera");
				}
				entity.AddComponent<FlyCameraComponent>(flyCameraFromJson(*flyCameraValue));
			}
			return true;
		}

		// --- Document loading --------------------------------------------------

		struct EntityRecord
		{
			Entity Instance;
			const JsonValue* Json = nullptr;
		};

		bool readSceneMetadata(Scene& scene, const JsonValue& sceneValue, std::string& outError)
		{
			scene.SetName(sceneValue.GetString("name", "Untitled Scene"));

			const JsonValue* uuidValue = sceneValue.Find("uuid");
			if (uuidValue != nullptr && !uuidValue->IsNull())
			{
				if (!uuidValue->IsString())
				{
					outError = "scene member 'uuid' must be a string or null";
					return false;
				}
				const Uuid sceneUuid = Uuid::FromString(uuidValue->AsString());
				if (sceneUuid.IsNull())
				{
					outError = std::format("scene member 'uuid' is not a valid UUID: '{}'", uuidValue->AsString());
					return false;
				}
				scene.SetUuid(sceneUuid);
			}

			const JsonValue* environmentValue = sceneValue.Find("environment");
			if (environmentValue != nullptr)
			{
				if (!environmentValue->IsObject())
				{
					outError = "scene member 'environment' must be an object";
					return false;
				}
				scene.SetEnvironment(environmentFromJson(*environmentValue));
			}
			return true;
		}

		bool createEntities(Scene& scene, const JsonValue& entitiesValue, std::vector<EntityRecord>& outRecords,
			std::string& outError)
		{
			std::unordered_set<Uuid, UuidHash> seen;
			const usize count = entitiesValue.GetSize();
			outRecords.reserve(count);

			for (usize index = 0; index < count; ++index)
			{
				const JsonValue& entry = entitiesValue[index];
				if (!entry.IsObject())
				{
					outError = std::format("entity {} must be a JSON object", index);
					return false;
				}

				const std::string uuidText = entry.GetString("uuid");
				const Uuid uuid = Uuid::FromString(uuidText);
				if (uuidText.empty() || uuid.IsNull())
				{
					outError = std::format("entity {} has an invalid or missing 'uuid'", index);
					return false;
				}
				if (!seen.insert(uuid).second)
				{
					outError = std::format("entity {} repeats the UUID '{}'", index, uuid.ToString());
					return false;
				}

				EntityRecord record;
				record.Instance = scene.CreateEntity(uuid, entry.GetString("name", "Entity"));
				record.Instance.SetActive(entry.GetBool("active", true));
				record.Json = &entry;
				outRecords.push_back(record);
			}
			return true;
		}

		bool wouldCreateCycle(const Scene& scene, const Uuid& childUuid, const Uuid& parentUuid)
		{
			Uuid cursor = parentUuid;
			for (int depth = 0; !cursor.IsNull() && depth < 4096; ++depth)
			{
				if (cursor == childUuid)
				{
					return true;
				}
				const Entity current = scene.FindEntity(cursor);
				if (!current.IsValid())
				{
					return false;
				}
				const TransformComponent* transform = current.TryGetComponent<TransformComponent>();
				if (transform == nullptr)
				{
					return false;
				}
				cursor = transform->Parent;
			}
			return false;
		}

		// Restores the hierarchy from the stored parent references and rebuilds
		// every child list from those links, so a hand-edited document cannot leave
		// the graph half-linked. The authored child order wins where it agrees with
		// the parent links.
		void linkHierarchy(Scene& scene, std::vector<EntityRecord>& records)
		{
			for (EntityRecord& record : records)
			{
				const JsonValue* transformValue = record.Json->Find("transform");
				if (transformValue == nullptr)
				{
					continue;
				}
				const JsonValue* parentValue = transformValue->Find("parent");
				if (parentValue == nullptr || parentValue->IsNull())
				{
					continue;
				}
				if (!parentValue->IsString())
				{
					AE_CORE_WARN("Entity '{}' has a malformed 'transform.parent'; the link was dropped",
						record.Instance.GetName());
					continue;
				}

				const Uuid parentUuid = Uuid::FromString(parentValue->AsString());
				if (parentUuid.IsNull())
				{
					AE_CORE_WARN("Entity '{}' has an invalid parent UUID '{}'; the link was dropped",
						record.Instance.GetName(), parentValue->AsString());
					continue;
				}

				const Entity parent = scene.FindEntity(parentUuid);
				if (!parent.IsValid())
				{
					AE_CORE_WARN("Entity '{}' references missing parent '{}'; the link was dropped",
						record.Instance.GetName(), parentUuid.ToString());
					continue;
				}
				if (!parent.HasComponent<TransformComponent>())
				{
					AE_CORE_WARN("Entity '{}' cannot be parented to '{}': the parent has no transform",
						record.Instance.GetName(), parent.GetName());
					continue;
				}
				if (wouldCreateCycle(scene, record.Instance.GetUuid(), parentUuid))
				{
					AE_CORE_WARN("Entity '{}' would create a hierarchy cycle through '{}'; the link was dropped",
						record.Instance.GetName(), parent.GetName());
					continue;
				}

				record.Instance.GetComponent<TransformComponent>().Parent = parentUuid;
			}

			for (EntityRecord& record : records)
			{
				record.Instance.GetComponent<TransformComponent>().Children.clear();
			}

			for (EntityRecord& record : records)
			{
				const JsonValue* transformValue = record.Json->Find("transform");
				if (transformValue == nullptr)
				{
					continue;
				}
				const JsonValue* childrenValue = transformValue->Find("children");
				if (childrenValue == nullptr || !childrenValue->IsArray())
				{
					continue;
				}

				TransformComponent& parentTransform = record.Instance.GetComponent<TransformComponent>();
				const Uuid parentUuid = record.Instance.GetUuid();
				for (usize index = 0; index < childrenValue->GetSize(); ++index)
				{
					const JsonValue& childValue = (*childrenValue)[index];
					if (!childValue.IsString())
					{
						AE_CORE_WARN("Entity '{}' lists a non-string child reference; it was ignored",
							record.Instance.GetName());
						continue;
					}

					const Uuid childUuid = Uuid::FromString(childValue.AsString());
					if (childUuid.IsNull())
					{
						continue;
					}
					const Entity child = scene.FindEntity(childUuid);
					if (!child.IsValid() || !child.HasComponent<TransformComponent>())
					{
						continue;
					}
					if (child.GetComponent<TransformComponent>().Parent != parentUuid)
					{
						continue;
					}
					if (std::find(parentTransform.Children.begin(), parentTransform.Children.end(), childUuid) ==
						parentTransform.Children.end())
					{
						parentTransform.Children.push_back(childUuid);
					}
				}
			}

			for (EntityRecord& record : records)
			{
				const TransformComponent& transform = record.Instance.GetComponent<TransformComponent>();
				if (transform.Parent.IsNull())
				{
					continue;
				}
				Entity parent = scene.FindEntity(transform.Parent);
				if (!parent.IsValid() || !parent.HasComponent<TransformComponent>())
				{
					continue;
				}

				TransformComponent& parentTransform = parent.GetComponent<TransformComponent>();
				const Uuid self = record.Instance.GetUuid();
				if (std::find(parentTransform.Children.begin(), parentTransform.Children.end(), self) ==
					parentTransform.Children.end())
				{
					parentTransform.Children.push_back(self);
				}
			}
		}
	}

	JsonValue SceneSerializer::Serialize(const Scene& scene)
	{
		JsonValue document = JsonValue::MakeObject();
		document.Set("version", c_CurrentVersion);

		JsonValue sceneObject = JsonValue::MakeObject();
		sceneObject.Set("uuid", scene.GetUuid().ToString());
		sceneObject.Set("name", scene.GetName());
		sceneObject.Set("environment", environmentToJson(scene.GetEnvironment()));
		document.Set("scene", std::move(sceneObject));

		JsonValue entities = JsonValue::MakeArray();
		for (const Entity& entity : scene.GetEntities())
		{
			if (!entity.IsValid())
			{
				continue;
			}
			entities.Push(entityToJson(entity, true));
		}
		document.Set("entities", std::move(entities));
		return document;
	}

	bool SceneSerializer::Deserialize(Scene& scene, const JsonValue& document, std::string* outError)
	{
		const Uuid previousUuid = scene.GetUuid();
		const std::string previousName = scene.GetName();
		const EnvironmentSettings previousEnvironment = scene.GetEnvironment();

		if (outError != nullptr)
		{
			outError->clear();
		}

		scene.Clear();
		scene.SetName("Untitled Scene");
		scene.SetEnvironment(EnvironmentSettings{});

		const auto fail = [&](std::string message) -> bool
		{
			scene.Clear();
			scene.SetUuid(previousUuid);
			scene.SetName(previousName);
			scene.SetEnvironment(previousEnvironment);
			if (outError != nullptr)
			{
				*outError = std::move(message);
			}
			return false;
		};

		if (!document.IsObject())
		{
			return fail("scene document root must be a JSON object");
		}

		const JsonValue* versionValue = document.Find("version");
		if (versionValue == nullptr || !versionValue->IsNumber())
		{
			return fail("scene document must contain a numeric 'version' member");
		}
		if (versionValue->AsNumber(-1.0) != static_cast<f64>(c_CurrentVersion))
		{
			return fail(std::format("unsupported scene format version {} (this build reads version {})",
				versionValue->Dump(), c_CurrentVersion));
		}

		const JsonValue* sceneValue = document.Find("scene");
		if (sceneValue == nullptr || !sceneValue->IsObject())
		{
			return fail("scene document must contain a 'scene' object");
		}

		const JsonValue* entitiesValue = document.Find("entities");
		if (entitiesValue == nullptr || !entitiesValue->IsArray())
		{
			return fail("scene document must contain an 'entities' array");
		}

		std::string error;
		if (!readSceneMetadata(scene, *sceneValue, error))
		{
			return fail(std::move(error));
		}

		std::vector<EntityRecord> records;
		if (!createEntities(scene, *entitiesValue, records, error))
		{
			return fail(std::move(error));
		}

		for (const EntityRecord& record : records)
		{
			if (!applyComponents(record.Instance, *record.Json, error))
			{
				return fail(std::move(error));
			}
		}

		linkHierarchy(scene, records);
		scene.UpdateTransforms();
		return true;
	}

	bool SceneSerializer::Save(const Scene& scene, const std::filesystem::path& path)
	{
		std::string text = Serialize(scene).Dump(4);
		text.push_back('\n');

		if (!FileSystem::WriteTextFile(path, text))
		{
			AE_CORE_ERROR("Failed to write scene '{}' to '{}'", scene.GetName(), FileSystem::ToUtf8(path));
			return false;
		}

		AE_CORE_DEBUG("Saved scene '{}' ({} entities) to '{}'", scene.GetName(), scene.GetEntityCount(),
			FileSystem::ToUtf8(path));
		return true;
	}

	bool SceneSerializer::Load(Scene& scene, const std::filesystem::path& path, std::string* outError)
	{
		std::string text;
		if (!FileSystem::TryReadTextFile(path, text))
		{
			if (outError != nullptr)
			{
				*outError = std::format("cannot read scene file '{}'", FileSystem::ToUtf8(path));
			}
			return false;
		}

		JsonValue::ParseError parseError;
		const std::optional<JsonValue> document = JsonValue::Parse(text, &parseError);
		if (!document.has_value())
		{
			if (outError != nullptr)
			{
				*outError = std::format("cannot parse scene file '{}' at {}:{}: {}", FileSystem::ToUtf8(path),
					parseError.Line, parseError.Column, parseError.Message);
			}
			return false;
		}

		return Deserialize(scene, *document, outError);
	}

	JsonValue SceneSerializer::SerializeEntity(const Entity& entity)
	{
		if (!entity.IsValid())
		{
			AE_CORE_WARN("Cannot serialise an invalid entity as a prefab");
			return JsonValue(nullptr);
		}

		JsonValue document = JsonValue::MakeObject();
		document.Set("version", c_CurrentVersion);
		document.Set("entity", entityToJson(entity, false));
		return document;
	}

	Entity SceneSerializer::DeserializeEntity(Scene& scene, const JsonValue& document)
	{
		if (!document.IsObject())
		{
			AE_CORE_ERROR("Prefab document root must be a JSON object");
			return Entity();
		}

		const JsonValue* versionValue = document.Find("version");
		if (versionValue != nullptr &&
			(!versionValue->IsNumber() || versionValue->AsNumber(-1.0) != static_cast<f64>(c_CurrentVersion)))
		{
			AE_CORE_ERROR("Unsupported prefab format version {} (this build reads version {})",
				versionValue->Dump(), c_CurrentVersion);
			return Entity();
		}

		const JsonValue* entityValue = document.Find("entity");
		if (entityValue == nullptr && (document.Contains("uuid") || document.Contains("transform")))
		{
			// A bare entity object is accepted so a snippet copied out of a scene
			// document can be used directly as a prefab source.
			entityValue = &document;
		}
		if (entityValue == nullptr || !entityValue->IsObject())
		{
			AE_CORE_ERROR("Prefab document must contain an 'entity' object");
			return Entity();
		}

		// Prefab instances always receive a fresh UUID so two instances of the same
		// prefab are distinct entities; the stored UUID identifies the authored
		// entity inside the prefab file only.
		Entity entity = scene.CreateEntity(entityValue->GetString("name", "Entity"));
		entity.SetActive(entityValue->GetBool("active", true));

		std::string error;
		if (!applyComponents(entity, *entityValue, error))
		{
			AE_CORE_ERROR("Failed to instantiate prefab entity: {}", error);
			scene.DestroyEntity(entity);
			return Entity();
		}

		scene.UpdateTransforms();
		return entity;
	}
}
