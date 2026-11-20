#include "Editor/EditorCommands.h"

#include "Aegis/Commands/CommandBus.h"
#include "Aegis/Core/Config.h"
#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Json.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Core/StringUtils.h"
#include "Aegis/Renderer/Renderer.h"
#include "Aegis/Scene/Components.h"
#include "Aegis/Scene/Entity.h"
#include "Aegis/Scene/Scene.h"
#include "Aegis/Serialization/SceneSerializer.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aegis
{
namespace
{
constexpr std::string_view c_ValidComponentTypes =
    "camera, light, meshRenderer, rigidBody, collider, audioSource, audioListener, luaScript, flyCamera";
constexpr std::string_view c_CameraProperties =
    "projection, fieldOfView, nearClip, farClip, orthographicSize, primary, clearColor";
constexpr std::string_view c_LightProperties =
    "type, color, intensity, range, innerConeDegrees, outerConeDegrees, castShadows";
constexpr std::string_view c_MeshRendererProperties = "mesh, materials, visible, castShadows, receiveShadows";
constexpr std::string_view c_RigidBodyProperties =
    "type, mass, friction, restitution, linearDamping, angularDamping, useGravity";
constexpr std::string_view c_ColliderProperties = "shape, halfExtents, radius, halfHeight, offset, isTrigger";
constexpr std::string_view c_AudioSourceProperties =
    "clip, volume, pitch, loop, playOnStart, spatial, minDistance, maxDistance";
constexpr std::string_view c_AudioListenerProperties = "primary";
constexpr std::string_view c_LuaScriptProperties = "script, enabled, properties";
constexpr std::string_view c_FlyCameraProperties =
    "active, moveSpeed, fastMultiplier, lookSensitivity, speedDamping, velocity";

constexpr usize c_MaxEntityNameLength = 256;
constexpr i64 c_MaxConsoleLogs = 2000;
constexpr i64 c_DefaultConsoleLogs = 200;

// --- JSON helpers ------------------------------------------------------

JsonValue vec3ToJson(const glm::vec3& value)
{
	JsonValue array = JsonValue::MakeArray();
	array.Push(value.x);
	array.Push(value.y);
	array.Push(value.z);
	return array;
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

JsonValue uuidOrNull(const Uuid& uuid)
{
	return uuid.IsNull() ? JsonValue(nullptr) : JsonValue(uuid.ToString());
}

std::string joinOptions(std::initializer_list<std::string_view> options)
{
	std::string joined;
	for (const std::string_view option : options)
	{
		if (!joined.empty())
		{
			joined += ", ";
		}
		joined += option;
	}
	return joined;
}

std::string assetTypeList()
{
	static constexpr AssetType c_Types[] = { AssetType::Mesh,   AssetType::Texture,  AssetType::Material,
		                                     AssetType::Shader, AssetType::Scene,    AssetType::Prefab,
		                                     AssetType::Script, AssetType::Audio,    AssetType::Environment,
		                                     AssetType::Font,   AssetType::Animation };
	std::string joined;
	for (const AssetType type : c_Types)
	{
		if (!joined.empty())
		{
			joined += ", ";
		}
		joined += AssetTypeToString(type);
	}
	return joined;
}

// --- Parameter readers -------------------------------------------------

CommandResult readString(const JsonValue& params, std::string_view key, bool required, std::string& out)
{
	const JsonValue* value = params.Find(key);
	if (value == nullptr || value->IsNull())
	{
		if (required)
		{
			return CommandResult::Fail(std::format("'{}' is required", key));
		}
		return CommandResult::Ok();
	}
	if (!value->IsString())
	{
		return CommandResult::Fail(std::format("'{}' must be a string", key));
	}
	out = std::string(value->AsString());
	return CommandResult::Ok();
}

CommandResult readBool(const JsonValue& params, std::string_view key, bool required, bool& out)
{
	const JsonValue* value = params.Find(key);
	if (value == nullptr || value->IsNull())
	{
		if (required)
		{
			return CommandResult::Fail(std::format("'{}' is required (true or false)", key));
		}
		return CommandResult::Ok();
	}
	if (!value->IsBool())
	{
		return CommandResult::Fail(std::format("'{}' must be true or false", key));
	}
	out = value->AsBool();
	return CommandResult::Ok();
}

CommandResult readUuid(const JsonValue& params, std::string_view key, bool required, Uuid& out)
{
	const JsonValue* value = params.Find(key);
	if (value == nullptr || value->IsNull())
	{
		if (required)
		{
			return CommandResult::Fail(std::format("'{}' is required (entity UUID string)", key));
		}
		out = Uuid::Null();
		return CommandResult::Ok();
	}
	if (!value->IsString())
	{
		return CommandResult::Fail(std::format("'{}' must be a UUID string", key));
	}
	const std::string text(value->AsString());
	out = Uuid::FromString(text);
	if (out.IsNull())
	{
		return CommandResult::Fail(std::format("'{}' is not a valid UUID: '{}'", key, text));
	}
	return CommandResult::Ok();
}

CommandResult readEntityName(const JsonValue& params, bool required, std::string& out)
{
	const JsonValue* value = params.Find("name");
	const bool present = value != nullptr && !value->IsNull();
	CommandResult result = readString(params, "name", required, out);
	if (!result.Success || !present)
	{
		return result;
	}
	if (out.empty())
	{
		return CommandResult::Fail("'name' must not be empty");
	}
	if (out.size() > c_MaxEntityNameLength)
	{
		return CommandResult::Fail(
		    std::format("'name' must be at most {} characters (received {})", c_MaxEntityNameLength, out.size()));
	}
	return CommandResult::Ok();
}

CommandResult requireScene(const EditorCommandContext& context, Scene*& outScene)
{
	if (context.Scene == nullptr)
	{
		return CommandResult::Fail("the editor command context has no active scene");
	}
	outScene = context.Scene;
	return CommandResult::Ok();
}

CommandResult requireEntity(Scene& scene, const JsonValue& params, Entity& outEntity, Uuid& outUuid)
{
	CommandResult result = readUuid(params, "uuid", true, outUuid);
	if (!result.Success)
	{
		return result;
	}
	outEntity = scene.FindEntity(outUuid);
	if (!outEntity.IsValid())
	{
		return CommandResult::Fail(std::format("no entity with uuid '{}'", outUuid.ToString()));
	}
	return CommandResult::Ok();
}

// --- Typed property readers -------------------------------------------

CommandResult readFiniteNumber(const JsonValue& value, std::string_view property, f32& out)
{
	if (!value.IsNumber())
	{
		return CommandResult::Fail(std::format("property '{}' must be a number", property));
	}
	const f64 number = value.AsNumber();
	if (!std::isfinite(number))
	{
		return CommandResult::Fail(std::format("property '{}' must be a finite number", property));
	}
	out = static_cast<f32>(number);
	return CommandResult::Ok();
}

CommandResult readFloatInRange(const JsonValue& value, std::string_view property, f32 minimum, f32 maximum, f32& out)
{
	f32 number = 0.0f;
	CommandResult result = readFiniteNumber(value, property, number);
	if (!result.Success)
	{
		return result;
	}
	if (number < minimum || number > maximum)
	{
		return CommandResult::Fail(
		    std::format("property '{}' must be between {} and {} (received {})", property, minimum, maximum, number));
	}
	out = number;
	return CommandResult::Ok();
}

CommandResult readFloatAtLeast(const JsonValue& value, std::string_view property, f32 minimum, f32& out)
{
	f32 number = 0.0f;
	CommandResult result = readFiniteNumber(value, property, number);
	if (!result.Success)
	{
		return result;
	}
	if (number < minimum)
	{
		return CommandResult::Fail(
		    std::format("property '{}' must be at least {} (received {})", property, minimum, number));
	}
	out = number;
	return CommandResult::Ok();
}

CommandResult readBoolValue(const JsonValue& value, std::string_view property, bool& out)
{
	if (!value.IsBool())
	{
		return CommandResult::Fail(std::format("property '{}' must be true or false", property));
	}
	out = value.AsBool();
	return CommandResult::Ok();
}

CommandResult readEnum(const JsonValue& value,
                       std::string_view property,
                       std::initializer_list<std::string_view> options,
                       std::string& out)
{
	if (!value.IsString())
	{
		return CommandResult::Fail(
		    std::format("property '{}' must be one of: {} (a string)", property, joinOptions(options)));
	}
	const std::string text(value.AsString());
	for (const std::string_view option : options)
	{
		if (StringUtils::EqualsIgnoreCase(text, option))
		{
			out = std::string(option);
			return CommandResult::Ok();
		}
	}
	return CommandResult::Fail(
	    std::format("property '{}' must be one of: {} (received '{}')", property, joinOptions(options), text));
}

CommandResult readVec3(const JsonValue& value, std::string_view property, glm::vec3& out)
{
	if (!value.IsArray() || value.GetSize() != 3)
	{
		return CommandResult::Fail(std::format("property '{}' must be an array of 3 numbers [x, y, z]", property));
	}
	glm::vec3 result(0.0f);
	for (usize index = 0; index < 3; ++index)
	{
		const JsonValue& element = value[index];
		if (!element.IsNumber() || !std::isfinite(element.AsNumber()))
		{
			return CommandResult::Fail(
			    std::format("property '{}' element {} must be a finite number", property, index));
		}
		result[static_cast<glm::length_t>(index)] = element.AsFloat();
	}
	out = result;
	return CommandResult::Ok();
}

CommandResult readVec3NonNegative(const JsonValue& value, std::string_view property, glm::vec3& out)
{
	glm::vec3 result(0.0f);
	CommandResult readResult = readVec3(value, property, result);
	if (!readResult.Success)
	{
		return readResult;
	}
	for (i32 index = 0; index < 3; ++index)
	{
		const glm::length_t component = static_cast<glm::length_t>(index);
		if (result[component] < 0.0f)
		{
			return CommandResult::Fail(std::format("property '{}' elements must be at least 0 (element {} is {})",
			                                       property,
			                                       index,
			                                       result[component]));
		}
	}
	out = result;
	return CommandResult::Ok();
}

CommandResult readVec3Positive(const JsonValue& value, std::string_view property, glm::vec3& out)
{
	glm::vec3 result(0.0f);
	CommandResult readResult = readVec3(value, property, result);
	if (!readResult.Success)
	{
		return readResult;
	}
	for (i32 index = 0; index < 3; ++index)
	{
		const glm::length_t component = static_cast<glm::length_t>(index);
		if (result[component] <= 0.0f)
		{
			return CommandResult::Fail(std::format("property '{}' elements must be greater than 0 (element {} is {})",
			                                       property,
			                                       index,
			                                       result[component]));
		}
	}
	out = result;
	return CommandResult::Ok();
}

CommandResult readVec4(const JsonValue& value, std::string_view property, glm::vec4& out)
{
	if (!value.IsArray() || value.GetSize() != 4)
	{
		return CommandResult::Fail(std::format("property '{}' must be an array of 4 numbers [x, y, z, w]", property));
	}
	glm::vec4 result(0.0f);
	for (usize index = 0; index < 4; ++index)
	{
		const JsonValue& element = value[index];
		if (!element.IsNumber() || !std::isfinite(element.AsNumber()))
		{
			return CommandResult::Fail(
			    std::format("property '{}' element {} must be a finite number", property, index));
		}
		result[static_cast<glm::length_t>(index)] = element.AsFloat();
	}
	out = result;
	return CommandResult::Ok();
}

CommandResult readQuaternion(const JsonValue& value, std::string_view property, glm::quat& out)
{
	if (!value.IsArray() || value.GetSize() != 4)
	{
		return CommandResult::Fail(std::format("property '{}' must be an array of 4 numbers [x, y, z, w]", property));
	}
	f32 components[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (usize index = 0; index < 4; ++index)
	{
		const JsonValue& element = value[index];
		if (!element.IsNumber() || !std::isfinite(element.AsNumber()))
		{
			return CommandResult::Fail(
			    std::format("property '{}' element {} must be a finite number", property, index));
		}
		components[index] = element.AsFloat();
	}

	const f32 lengthSquared = components[0] * components[0] + components[1] * components[1] +
	                          components[2] * components[2] + components[3] * components[3];
	if (lengthSquared < 1.0e-8f)
	{
		return CommandResult::Fail(
		    std::format("property '{}' must have a non-zero length (received a zero quaternion)", property));
	}
	out = glm::normalize(glm::quat(components[3], components[0], components[1], components[2]));
	return CommandResult::Ok();
}

CommandResult
readAssetHandle(const JsonValue& value, AssetType expectedType, std::string_view property, AssetHandle& out)
{
	if (value.IsNull())
	{
		out = AssetHandle{};
		return CommandResult::Ok();
	}
	if (!value.IsObject())
	{
		return CommandResult::Fail(std::format(
		    "property '{}' must be null or an asset handle object like {{\"id\":\"<uuid>\",\"type\":\"{}\"}}",
		    property,
		    AssetTypeToString(expectedType)));
	}

	const std::string idText = value.GetString("id");
	if (idText.empty())
	{
		return CommandResult::Fail(std::format("property '{}' requires an 'id' UUID string", property));
	}
	const Uuid id = Uuid::FromString(idText);
	if (id.IsNull())
	{
		return CommandResult::Fail(std::format("property '{}' has an invalid UUID '{}'", property, idText));
	}

	const std::string typeText = value.GetString("type", AssetTypeToString(expectedType));
	const AssetType type = AssetTypeFromString(typeText);
	if (type == AssetType::None)
	{
		return CommandResult::Fail(std::format("property '{}' has an unknown asset type '{}'; valid types: {}",
		                                       property,
		                                       typeText,
		                                       assetTypeList()));
	}
	if (type != expectedType)
	{
		return CommandResult::Fail(std::format("property '{}' must be a '{}' asset handle (received '{}')",
		                                       property,
		                                       AssetTypeToString(expectedType),
		                                       AssetTypeToString(type)));
	}

	out.Id = id;
	out.Type = type;
	return CommandResult::Ok();
}

CommandResult readMaterialList(const JsonValue& value, std::string_view property, std::vector<AssetHandle>& out)
{
	if (!value.IsArray())
	{
		return CommandResult::Fail(std::format("property '{}' must be an array of material asset handles", property));
	}
	std::vector<AssetHandle> materials;
	materials.reserve(value.GetSize());
	for (usize index = 0; index < value.GetSize(); ++index)
	{
		AssetHandle handle;
		const std::string elementProperty = std::format("{}[{}]", property, index);
		CommandResult result = readAssetHandle(value[index], AssetType::Material, elementProperty, handle);
		if (!result.Success)
		{
			return result;
		}
		materials.push_back(handle);
	}
	out = std::move(materials);
	return CommandResult::Ok();
}

CommandResult unknownProperty(std::string_view type, std::string_view property, std::string_view validProperties)
{
	return CommandResult::Fail(
	    std::format("unknown property '{}' for component '{}'; valid properties: {}", property, type, validProperties));
}

// --- Component property setters ---------------------------------------

CommandResult setCameraProperty(CameraComponent& camera, std::string_view property, const JsonValue& value)
{
	if (property == "projection")
	{
		std::string projection;
		CommandResult result = readEnum(value, "camera.projection", { "perspective", "orthographic" }, projection);
		if (!result.Success)
		{
			return result;
		}
		camera.Projection =
		    projection == "orthographic" ? CameraProjection::Orthographic : CameraProjection::Perspective;
		return CommandResult::Ok();
	}
	if (property == "fieldOfView")
	{
		return readFloatInRange(value, "camera.fieldOfView", 1.0f, 179.0f, camera.FieldOfView);
	}
	if (property == "nearClip")
	{
		return readFloatInRange(value, "camera.nearClip", 0.0001f, 1.0e6f, camera.NearClip);
	}
	if (property == "farClip")
	{
		return readFloatInRange(value, "camera.farClip", 0.0001f, 1.0e9f, camera.FarClip);
	}
	if (property == "orthographicSize")
	{
		return readFloatInRange(value, "camera.orthographicSize", 0.0001f, 1.0e6f, camera.OrthographicSize);
	}
	if (property == "primary")
	{
		return readBoolValue(value, "camera.primary", camera.Primary);
	}
	if (property == "clearColor")
	{
		glm::vec4 color(0.0f);
		CommandResult result = readVec4(value, "camera.clearColor", color);
		if (!result.Success)
		{
			return result;
		}
		for (usize index = 0; index < 4; ++index)
		{
			if (color[static_cast<glm::length_t>(index)] < 0.0f || color[static_cast<glm::length_t>(index)] > 1.0f)
			{
				return CommandResult::Fail(
				    std::format("property 'camera.clearColor' components must be between 0 and 1 (element {} is {})",
				                index,
				                color[static_cast<glm::length_t>(index)]));
			}
		}
		camera.ClearColor = color;
		return CommandResult::Ok();
	}
	return unknownProperty("camera", property, c_CameraProperties);
}

CommandResult validateCamera(const CameraComponent& camera)
{
	if (camera.NearClip <= 0.0f || camera.FarClip <= camera.NearClip)
	{
		return CommandResult::Fail(
		    std::format("camera.nearClip ({}) must be positive and smaller than camera.farClip ({})",
		                camera.NearClip,
		                camera.FarClip));
	}
	return CommandResult::Ok();
}

CommandResult setLightProperty(LightComponent& light, std::string_view property, const JsonValue& value)
{
	if (property == "type")
	{
		std::string type;
		CommandResult result = readEnum(value, "light.type", { "directional", "point", "spot" }, type);
		if (!result.Success)
		{
			return result;
		}
		light.Type = type == "point" ? LightType::Point : (type == "spot" ? LightType::Spot : LightType::Directional);
		return CommandResult::Ok();
	}
	if (property == "color")
	{
		return readVec3NonNegative(value, "light.color", light.Color);
	}
	if (property == "intensity")
	{
		return readFloatAtLeast(value, "light.intensity", 0.0f, light.Intensity);
	}
	if (property == "range")
	{
		return readFloatInRange(value, "light.range", 0.0001f, 1.0e6f, light.Range);
	}
	if (property == "innerConeDegrees")
	{
		return readFloatInRange(value, "light.innerConeDegrees", 0.0f, 179.0f, light.InnerConeDegrees);
	}
	if (property == "outerConeDegrees")
	{
		return readFloatInRange(value, "light.outerConeDegrees", 0.0f, 179.0f, light.OuterConeDegrees);
	}
	if (property == "castShadows")
	{
		return readBoolValue(value, "light.castShadows", light.CastShadows);
	}
	return unknownProperty("light", property, c_LightProperties);
}

CommandResult validateLight(const LightComponent& light)
{
	if (light.OuterConeDegrees < light.InnerConeDegrees)
	{
		return CommandResult::Fail(
		    std::format("light.outerConeDegrees ({}) must be greater than or equal to light.innerConeDegrees ({})",
		                light.OuterConeDegrees,
		                light.InnerConeDegrees));
	}
	return CommandResult::Ok();
}

CommandResult
setMeshRendererProperty(MeshRendererComponent& renderer, std::string_view property, const JsonValue& value)
{
	if (property == "mesh")
	{
		return readAssetHandle(value, AssetType::Mesh, "meshRenderer.mesh", renderer.Mesh);
	}
	if (property == "materials")
	{
		return readMaterialList(value, "meshRenderer.materials", renderer.Materials);
	}
	if (property == "visible")
	{
		return readBoolValue(value, "meshRenderer.visible", renderer.Visible);
	}
	if (property == "castShadows")
	{
		return readBoolValue(value, "meshRenderer.castShadows", renderer.CastShadows);
	}
	if (property == "receiveShadows")
	{
		return readBoolValue(value, "meshRenderer.receiveShadows", renderer.ReceiveShadows);
	}
	return unknownProperty("meshRenderer", property, c_MeshRendererProperties);
}

CommandResult validateMeshRenderer(const MeshRendererComponent&)
{
	return CommandResult::Ok();
}

CommandResult setRigidBodyProperty(RigidBodyComponent& body, std::string_view property, const JsonValue& value)
{
	if (property == "type")
	{
		std::string type;
		CommandResult result = readEnum(value, "rigidBody.type", { "static", "kinematic", "dynamic" }, type);
		if (!result.Success)
		{
			return result;
		}
		body.Type =
		    type == "kinematic" ? BodyType::Kinematic : (type == "dynamic" ? BodyType::Dynamic : BodyType::Static);
		return CommandResult::Ok();
	}
	if (property == "mass")
	{
		return readFloatAtLeast(value, "rigidBody.mass", 0.0f, body.Mass);
	}
	if (property == "friction")
	{
		return readFloatInRange(value, "rigidBody.friction", 0.0f, 1.0f, body.Friction);
	}
	if (property == "restitution")
	{
		return readFloatInRange(value, "rigidBody.restitution", 0.0f, 1.0f, body.Restitution);
	}
	if (property == "linearDamping")
	{
		return readFloatAtLeast(value, "rigidBody.linearDamping", 0.0f, body.LinearDamping);
	}
	if (property == "angularDamping")
	{
		return readFloatAtLeast(value, "rigidBody.angularDamping", 0.0f, body.AngularDamping);
	}
	if (property == "useGravity")
	{
		return readBoolValue(value, "rigidBody.useGravity", body.UseGravity);
	}
	return unknownProperty("rigidBody", property, c_RigidBodyProperties);
}

CommandResult validateRigidBody(const RigidBodyComponent& body)
{
	if (body.Type == BodyType::Dynamic && body.Mass <= 0.0f)
	{
		return CommandResult::Fail(
		    std::format("rigidBody.mass must be greater than 0 for a dynamic body (received {})", body.Mass));
	}
	return CommandResult::Ok();
}

CommandResult setColliderProperty(ColliderComponent& collider, std::string_view property, const JsonValue& value)
{
	if (property == "shape")
	{
		std::string shape;
		CommandResult result = readEnum(value, "collider.shape", { "box", "sphere", "capsule", "mesh" }, shape);
		if (!result.Success)
		{
			return result;
		}
		collider.Shape = shape == "sphere"
		                     ? ColliderShape::Sphere
		                     : (shape == "capsule" ? ColliderShape::Capsule
		                                           : (shape == "mesh" ? ColliderShape::Mesh : ColliderShape::Box));
		return CommandResult::Ok();
	}
	if (property == "halfExtents")
	{
		return readVec3Positive(value, "collider.halfExtents", collider.HalfExtents);
	}
	if (property == "radius")
	{
		return readFloatInRange(value, "collider.radius", 0.0001f, 1.0e6f, collider.Radius);
	}
	if (property == "halfHeight")
	{
		return readFloatAtLeast(value, "collider.halfHeight", 0.0f, collider.HalfHeight);
	}
	if (property == "offset")
	{
		return readVec3(value, "collider.offset", collider.Offset);
	}
	if (property == "isTrigger")
	{
		return readBoolValue(value, "collider.isTrigger", collider.IsTrigger);
	}
	return unknownProperty("collider", property, c_ColliderProperties);
}

CommandResult validateCollider(const ColliderComponent&)
{
	return CommandResult::Ok();
}

CommandResult setAudioSourceProperty(AudioSourceComponent& source, std::string_view property, const JsonValue& value)
{
	if (property == "clip")
	{
		return readAssetHandle(value, AssetType::Audio, "audioSource.clip", source.Clip);
	}
	if (property == "volume")
	{
		return readFloatInRange(value, "audioSource.volume", 0.0f, 1.0f, source.Volume);
	}
	if (property == "pitch")
	{
		return readFloatInRange(value, "audioSource.pitch", 0.01f, 4.0f, source.Pitch);
	}
	if (property == "loop")
	{
		return readBoolValue(value, "audioSource.loop", source.Loop);
	}
	if (property == "playOnStart")
	{
		return readBoolValue(value, "audioSource.playOnStart", source.PlayOnStart);
	}
	if (property == "spatial")
	{
		return readBoolValue(value, "audioSource.spatial", source.Spatial);
	}
	if (property == "minDistance")
	{
		return readFloatInRange(value, "audioSource.minDistance", 0.0001f, 1.0e6f, source.MinDistance);
	}
	if (property == "maxDistance")
	{
		return readFloatInRange(value, "audioSource.maxDistance", 0.0001f, 1.0e6f, source.MaxDistance);
	}
	return unknownProperty("audioSource", property, c_AudioSourceProperties);
}

CommandResult validateAudioSource(const AudioSourceComponent& source)
{
	if (source.MaxDistance < source.MinDistance)
	{
		return CommandResult::Fail(
		    std::format("audioSource.maxDistance ({}) must be greater than or equal to audioSource.minDistance ({})",
		                source.MaxDistance,
		                source.MinDistance));
	}
	return CommandResult::Ok();
}

CommandResult
setAudioListenerProperty(AudioListenerComponent& listener, std::string_view property, const JsonValue& value)
{
	if (property == "primary")
	{
		return readBoolValue(value, "audioListener.primary", listener.Primary);
	}
	return unknownProperty("audioListener", property, c_AudioListenerProperties);
}

CommandResult validateAudioListener(const AudioListenerComponent&)
{
	return CommandResult::Ok();
}

CommandResult setLuaScriptProperty(LuaScriptComponent& script, std::string_view property, const JsonValue& value)
{
	if (property == "script")
	{
		return readAssetHandle(value, AssetType::Script, "luaScript.script", script.Script);
	}
	if (property == "enabled")
	{
		return readBoolValue(value, "luaScript.enabled", script.Enabled);
	}
	if (property == "properties")
	{
		if (!value.IsObject())
		{
			return CommandResult::Fail("property 'luaScript.properties' must be a JSON object");
		}
		script.Properties = value;
		return CommandResult::Ok();
	}
	return unknownProperty("luaScript", property, c_LuaScriptProperties);
}

CommandResult validateLuaScript(const LuaScriptComponent&)
{
	return CommandResult::Ok();
}

CommandResult setFlyCameraProperty(FlyCameraComponent& flyCamera, std::string_view property, const JsonValue& value)
{
	if (property == "active")
	{
		return readBoolValue(value, "flyCamera.active", flyCamera.Active);
	}
	if (property == "moveSpeed")
	{
		return readFloatAtLeast(value, "flyCamera.moveSpeed", 0.0f, flyCamera.MoveSpeed);
	}
	if (property == "fastMultiplier")
	{
		return readFloatAtLeast(value, "flyCamera.fastMultiplier", 0.0001f, flyCamera.FastMultiplier);
	}
	if (property == "lookSensitivity")
	{
		return readFloatAtLeast(value, "flyCamera.lookSensitivity", 0.0f, flyCamera.LookSensitivity);
	}
	if (property == "speedDamping")
	{
		return readFloatAtLeast(value, "flyCamera.speedDamping", 0.0f, flyCamera.SpeedDamping);
	}
	if (property == "velocity")
	{
		return readVec3(value, "flyCamera.velocity", flyCamera.Velocity);
	}
	return unknownProperty("flyCamera", property, c_FlyCameraProperties);
}

CommandResult validateFlyCamera(const FlyCameraComponent&)
{
	return CommandResult::Ok();
}

// --- Component descriptors --------------------------------------------

struct ComponentDescriptor
{
	std::string_view Type;
	std::string_view Properties;
	std::function<bool(const Entity&)> Has;
	std::function<void(Entity&)> Add;
	std::function<void(Entity&)> Remove;
	std::function<CommandResult(Entity&, const JsonValue&)> Apply;
};

template <typename T, typename Setter, typename Validator>
CommandResult applyComponentProperties(Entity& entity,
                                       const JsonValue& values,
                                       std::string_view type,
                                       std::string_view validProperties,
                                       Setter setter,
                                       Validator validator)
{
	T* component = entity.TryGetComponent<T>();
	if (component == nullptr)
	{
		return CommandResult::Fail(std::format("entity '{}' has no '{}' component", entity.GetName(), type));
	}
	if (!values.IsObject())
	{
		return CommandResult::Fail(
		    std::format("the values for '{}' must be a JSON object of property names to values", type));
	}
	if (values.GetSize() == 0)
	{
		return CommandResult::Fail(
		    std::format("at least one property is required for component '{}'; valid properties: {}",
		                type,
		                validProperties));
	}

	// Apply to a copy and commit only when every property and the component's
	// cross-property invariants validate, so a failed setMany never leaves a
	// half-edited component behind.
	T copy = *component;
	for (const auto& [property, value] : values.GetObject())
	{
		CommandResult result = setter(copy, property, value);
		if (!result.Success)
		{
			return result;
		}
	}
	CommandResult validation = validator(copy);
	if (!validation.Success)
	{
		return validation;
	}
	*component = std::move(copy);
	return CommandResult::Ok();
}

const std::vector<ComponentDescriptor>& componentDescriptors()
{
	static const std::vector<ComponentDescriptor> s_Descriptors = {
		{ "camera",
		  c_CameraProperties,
		  [](const Entity& entity) { return entity.HasComponent<CameraComponent>(); },
		  [](Entity& entity) { entity.AddComponent<CameraComponent>(); },
		  [](Entity& entity) { entity.RemoveComponent<CameraComponent>(); },
		  [](Entity& entity, const JsonValue& values)
		  {
		      return applyComponentProperties<CameraComponent>(entity,
		                                                       values,
		                                                       "camera",
		                                                       c_CameraProperties,
		                                                       setCameraProperty,
		                                                       validateCamera);
		  } },
		{ "light",
		  c_LightProperties,
		  [](const Entity& entity) { return entity.HasComponent<LightComponent>(); },
		  [](Entity& entity) { entity.AddComponent<LightComponent>(); },
		  [](Entity& entity) { entity.RemoveComponent<LightComponent>(); },
		  [](Entity& entity, const JsonValue& values)
		  {
		      return applyComponentProperties<LightComponent>(entity,
		                                                      values,
		                                                      "light",
		                                                      c_LightProperties,
		                                                      setLightProperty,
		                                                      validateLight);
		  } },
		{ "meshRenderer",
		  c_MeshRendererProperties,
		  [](const Entity& entity) { return entity.HasComponent<MeshRendererComponent>(); },
		  [](Entity& entity) { entity.AddComponent<MeshRendererComponent>(); },
		  [](Entity& entity) { entity.RemoveComponent<MeshRendererComponent>(); },
		  [](Entity& entity, const JsonValue& values)
		  {
		      return applyComponentProperties<MeshRendererComponent>(entity,
		                                                             values,
		                                                             "meshRenderer",
		                                                             c_MeshRendererProperties,
		                                                             setMeshRendererProperty,
		                                                             validateMeshRenderer);
		  } },
		{ "rigidBody",
		  c_RigidBodyProperties,
		  [](const Entity& entity) { return entity.HasComponent<RigidBodyComponent>(); },
		  [](Entity& entity) { entity.AddComponent<RigidBodyComponent>(); },
		  [](Entity& entity) { entity.RemoveComponent<RigidBodyComponent>(); },
		  [](Entity& entity, const JsonValue& values)
		  {
		      return applyComponentProperties<RigidBodyComponent>(entity,
		                                                          values,
		                                                          "rigidBody",
		                                                          c_RigidBodyProperties,
		                                                          setRigidBodyProperty,
		                                                          validateRigidBody);
		  } },
		{ "collider",
		  c_ColliderProperties,
		  [](const Entity& entity) { return entity.HasComponent<ColliderComponent>(); },
		  [](Entity& entity) { entity.AddComponent<ColliderComponent>(); },
		  [](Entity& entity) { entity.RemoveComponent<ColliderComponent>(); },
		  [](Entity& entity, const JsonValue& values)
		  {
		      return applyComponentProperties<ColliderComponent>(entity,
		                                                         values,
		                                                         "collider",
		                                                         c_ColliderProperties,
		                                                         setColliderProperty,
		                                                         validateCollider);
		  } },
		{ "audioSource",
		  c_AudioSourceProperties,
		  [](const Entity& entity) { return entity.HasComponent<AudioSourceComponent>(); },
		  [](Entity& entity) { entity.AddComponent<AudioSourceComponent>(); },
		  [](Entity& entity) { entity.RemoveComponent<AudioSourceComponent>(); },
		  [](Entity& entity, const JsonValue& values)
		  {
		      return applyComponentProperties<AudioSourceComponent>(entity,
		                                                            values,
		                                                            "audioSource",
		                                                            c_AudioSourceProperties,
		                                                            setAudioSourceProperty,
		                                                            validateAudioSource);
		  } },
		{ "audioListener",
		  c_AudioListenerProperties,
		  [](const Entity& entity) { return entity.HasComponent<AudioListenerComponent>(); },
		  [](Entity& entity) { entity.AddComponent<AudioListenerComponent>(); },
		  [](Entity& entity) { entity.RemoveComponent<AudioListenerComponent>(); },
		  [](Entity& entity, const JsonValue& values)
		  {
		      return applyComponentProperties<AudioListenerComponent>(entity,
		                                                              values,
		                                                              "audioListener",
		                                                              c_AudioListenerProperties,
		                                                              setAudioListenerProperty,
		                                                              validateAudioListener);
		  } },
		{ "luaScript",
		  c_LuaScriptProperties,
		  [](const Entity& entity) { return entity.HasComponent<LuaScriptComponent>(); },
		  [](Entity& entity) { entity.AddComponent<LuaScriptComponent>(); },
		  [](Entity& entity) { entity.RemoveComponent<LuaScriptComponent>(); },
		  [](Entity& entity, const JsonValue& values)
		  {
		      return applyComponentProperties<LuaScriptComponent>(entity,
		                                                          values,
		                                                          "luaScript",
		                                                          c_LuaScriptProperties,
		                                                          setLuaScriptProperty,
		                                                          validateLuaScript);
		  } },
		{ "flyCamera",
		  c_FlyCameraProperties,
		  [](const Entity& entity) { return entity.HasComponent<FlyCameraComponent>(); },
		  [](Entity& entity) { entity.AddComponent<FlyCameraComponent>(); },
		  [](Entity& entity) { entity.RemoveComponent<FlyCameraComponent>(); },
		  [](Entity& entity, const JsonValue& values)
		  {
		      return applyComponentProperties<FlyCameraComponent>(entity,
		                                                          values,
		                                                          "flyCamera",
		                                                          c_FlyCameraProperties,
		                                                          setFlyCameraProperty,
		                                                          validateFlyCamera);
		  } },
	};
	return s_Descriptors;
}

const ComponentDescriptor* findComponentDescriptor(std::string_view type)
{
	for (const ComponentDescriptor& descriptor : componentDescriptors())
	{
		if (descriptor.Type == type)
		{
			return &descriptor;
		}
	}
	return nullptr;
}

std::string componentNames(const Entity& entity)
{
	std::string names;
	for (const ComponentDescriptor& descriptor : componentDescriptors())
	{
		if (!descriptor.Has(entity))
		{
			continue;
		}
		if (!names.empty())
		{
			names += ", ";
		}
		names += descriptor.Type;
	}
	return names.empty() ? "none" : names;
}

CommandResult unknownComponentType(std::string_view type)
{
	return CommandResult::Fail(
	    std::format("unknown component type '{}'; valid types: {}", type, c_ValidComponentTypes));
}

// --- Entity / scene JSON ----------------------------------------------

JsonValue entityDocument(const Entity& entity)
{
	const JsonValue prefab = SceneSerializer::SerializeEntity(entity);
	const JsonValue* entityValue = prefab.Find("entity");
	JsonValue document = (entityValue != nullptr && entityValue->IsObject()) ? *entityValue : JsonValue::MakeObject();

	JsonValue components = JsonValue::MakeArray();
	for (const ComponentDescriptor& descriptor : componentDescriptors())
	{
		if (descriptor.Has(entity))
		{
			components.Push(std::string(descriptor.Type));
		}
	}
	document.Set("components", std::move(components));

	const TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
	if (transform != nullptr)
	{
		JsonValue transformValue = document["transform"].IsObject() ? document["transform"] : JsonValue::MakeObject();
		transformValue.Set("eulerAngles", vec3ToJson(transform->GetLocalEulerAngles()));
		transformValue.Set("parent", uuidOrNull(transform->Parent));
		JsonValue children = JsonValue::MakeArray();
		for (const Uuid& child : transform->Children)
		{
			children.Push(child.ToString());
		}
		transformValue.Set("children", std::move(children));
		document.Set("transform", std::move(transformValue));
	}
	return document;
}

CommandResult componentDocument(const Entity& entity, std::string_view type, JsonValue& out)
{
	const JsonValue document = entityDocument(entity);
	const JsonValue* component = document.Find(type);
	if (component == nullptr)
	{
		return CommandResult::Fail(std::format("entity '{}' has no '{}' component (current components: {})",
		                                       entity.GetName(),
		                                       type,
		                                       componentNames(entity)));
	}
	out = *component;
	return CommandResult::Ok();
}

JsonValue sceneSummary(const Scene& scene)
{
	JsonValue summary = JsonValue::MakeObject();
	summary.Set("name", scene.GetName());
	summary.Set("uuid", scene.GetUuid().ToString());
	summary.Set("entityCount", static_cast<u64>(scene.GetEntityCount()));
	return summary;
}

JsonValue selectionJson(const EditorCommandContext& context)
{
	JsonValue selection = JsonValue::MakeObject();
	if (context.Selection == nullptr || context.Selection->IsNull())
	{
		selection.Set("uuid", nullptr);
	}
	else
	{
		selection.Set("uuid", context.Selection->ToString());
	}
	return selection;
}

std::filesystem::path resolveProjectPath(const EditorCommandContext& context, std::string_view path)
{
	const std::filesystem::path candidate = FileSystem::FromUtf8(path);
	if (candidate.is_absolute())
	{
		return candidate;
	}
	const std::filesystem::path root = context.ProjectRoot.empty() ? FileSystem::GetCurrentWorkingDirectory()
	                                                               : FileSystem::FromUtf8(context.ProjectRoot);
	return root / candidate;
}

bool wouldCreateCycle(Scene& scene, const Uuid& childUuid, const Uuid& parentUuid)
{
	Uuid cursor = parentUuid;
	for (i32 depth = 0; !cursor.IsNull() && depth < 4096; ++depth)
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

// --- Command metadata helpers -----------------------------------------

CommandParameter parameter(std::string name, std::string type, bool required, std::string description)
{
	CommandParameter result;
	result.Name = std::move(name);
	result.Type = std::move(type);
	result.Required = required;
	result.Description = std::move(description);
	return result;
}

CommandInfo
info(std::string name, std::string category, std::string description, std::vector<CommandParameter> parameters = {})
{
	CommandInfo result;
	result.Name = std::move(name);
	result.Category = std::move(category);
	result.Description = std::move(description);
	// No undo stack exists yet, so advertising undoable commands would be a
	// lie; wiring undo in later only changes this flag and the handlers.
	result.Undoable = false;
	result.Parameters = std::move(parameters);
	return result;
}

void registerCommand(CommandInfo commandInfo, CommandBus::Handler handler)
{
	CommandBus& bus = CommandBus::Get();
	// Idempotent registration: the editor and tests may update the context and
	// register again, which must not fail as a duplicate command.
	const std::string name = commandInfo.Name;
	if (bus.HasCommand(name))
	{
		bus.Unregister(name);
	}
	if (!bus.Register(std::move(commandInfo), std::move(handler)))
	{
		AE_CORE_ERROR("Failed to register editor command '{}'", name);
	}
}
} // namespace

void RegisterEditorCommands(EditorCommandContext& context)
{
	registerCommand(
	    info("project.info", "project", "Reports the scene identity, entity count, engine version and renderer state."),
	    [context](const JsonValue&) -> CommandResult
	    {
		    JsonValue result = JsonValue::MakeObject();
		    result.Set("engineName", std::string(c_EngineName));
		    result.Set("engineVersion", std::string(c_EngineVersion));
		    result.Set("projectRoot", context.ProjectRoot);
		    if (context.Scene != nullptr)
		    {
			    result.Set("sceneName", context.Scene->GetName());
			    result.Set("sceneUuid", context.Scene->GetUuid().ToString());
			    result.Set("entityCount", static_cast<u64>(context.Scene->GetEntityCount()));
		    }
		    else
		    {
			    result.Set("sceneName", nullptr);
			    result.Set("sceneUuid", nullptr);
			    result.Set("entityCount", 0);
		    }
		    if (context.Renderer != nullptr)
		    {
			    result.Set("gpuName", context.Renderer->GetGpuName());
			    result.Set("width", static_cast<u32>(context.Renderer->GetWidth()));
			    result.Set("height", static_cast<u32>(context.Renderer->GetHeight()));
			    result.Set("rendererInitialized", context.Renderer->IsInitialized());
			    result.Set("vSync", context.Renderer->IsVSync());
		    }
		    else
		    {
			    result.Set("gpuName", "unavailable");
			    result.Set("width", 0);
			    result.Set("height", 0);
			    result.Set("rendererInitialized", false);
			    result.Set("vSync", false);
		    }
		    result.Set("selection",
		               context.Selection == nullptr || context.Selection->IsNull()
		                   ? JsonValue(nullptr)
		                   : JsonValue(context.Selection->ToString()));
		    return CommandResult::Ok(result);
	    });

	registerCommand(
	    info("environment.get", "environment",
	         "Reads the scene environment settings: ambient light, IBL, SSAO, shadows, exposure and bloom."),
	    [context](const JsonValue&) -> CommandResult
	    {
		    Scene* scene = nullptr;
		    CommandResult sceneResult = requireScene(context, scene);
		    if (!sceneResult.Success)
		    {
			    return sceneResult;
		    }
		    const EnvironmentSettings& environment = scene->GetEnvironment();
		    JsonValue result = JsonValue::MakeObject();
		    JsonValue ambient = JsonValue::MakeArray();
		    ambient.Push(static_cast<f64>(environment.AmbientColor.x));
		    ambient.Push(static_cast<f64>(environment.AmbientColor.y));
		    ambient.Push(static_cast<f64>(environment.AmbientColor.z));
		    result.Set("ambientColor", std::move(ambient));
		    result.Set("ambientIntensity", static_cast<f64>(environment.AmbientIntensity));
		    result.Set("hdriPath", environment.HdriPath);
		    result.Set("hdriIntensity", static_cast<f64>(environment.HdriIntensity));
		    result.Set("exposure", static_cast<f64>(environment.Exposure));
		    result.Set("enableIBL", environment.EnableIBL);
		    result.Set("enableShadows", environment.EnableShadows);
		    result.Set("enableSSAO", environment.EnableSSAO);
		    result.Set("enableBloom", environment.EnableBloom);
		    result.Set("enableFXAA", environment.EnableFXAA);
		    result.Set("shadowDistance", static_cast<f64>(environment.ShadowDistance));
		    result.Set("ssaoRadius", static_cast<f64>(environment.SSAORadius));
		    result.Set("bloomThreshold", static_cast<f64>(environment.BloomThreshold));
		    result.Set("bloomIntensity", static_cast<f64>(environment.BloomIntensity));
		    return CommandResult::Ok(result);
	    });

	registerCommand(
	    info("environment.set", "environment",
	         "Updates any combination of scene environment settings. Omitted fields keep their value.",
	         { parameter("ambientColor", "number[3]", false, "Ambient light colour (linear RGB)"),
	           parameter("ambientIntensity", "number", false, "Ambient light intensity"),
	           parameter("hdriPath", "string", false, "Equirectangular .hdr environment (empty = procedural sky)"),
	           parameter("hdriIntensity", "number", false, "IBL intensity"),
	           parameter("exposure", "number", false, "Tonemap exposure"),
	           parameter("enableIBL", "bool", false, "Image-based lighting toggle"),
	           parameter("enableShadows", "bool", false, "Sun shadow toggle"),
	           parameter("enableSSAO", "bool", false, "Screen-space ambient occlusion toggle"),
	           parameter("enableBloom", "bool", false, "Bloom toggle (not implemented yet)"),
	           parameter("enableFXAA", "bool", false, "FXAA toggle (not implemented yet)"),
	           parameter("shadowDistance", "number", false, "Shadow map fitting distance"),
	           parameter("ssaoRadius", "number", false, "SSAO sample radius in metres"),
	           parameter("bloomThreshold", "number", false, "Bloom bright-pass threshold"),
	           parameter("bloomIntensity", "number", false, "Bloom composite intensity") }),
	    [context](const JsonValue& params) -> CommandResult
	    {
		    Scene* scene = nullptr;
		    CommandResult sceneResult = requireScene(context, scene);
		    if (!sceneResult.Success)
		    {
			    return sceneResult;
		    }
		    if (params.IsNull() || !params.IsObject() || params.IsEmpty())
		    {
			    return CommandResult::Fail("environment.set requires at least one environment field");
		    }

		    EnvironmentSettings& environment = scene->GetEnvironment();
		    if (params.Contains("ambientColor"))
		    {
			    CommandResult read = readVec3(*params.Find("ambientColor"), "ambientColor", environment.AmbientColor);
			    if (!read.Success)
			    {
				    return read;
			    }
		    }
		    const struct
		    {
			    const char* Key;
			    f32* Target;
		    } floats[] = {
			    { "ambientIntensity", &environment.AmbientIntensity },
			    { "hdriIntensity", &environment.HdriIntensity },
			    { "exposure", &environment.Exposure },
			    { "shadowDistance", &environment.ShadowDistance },
			    { "ssaoRadius", &environment.SSAORadius },
			    { "bloomThreshold", &environment.BloomThreshold },
			    { "bloomIntensity", &environment.BloomIntensity },
		    };
		    for (const auto& field : floats)
		    {
			    if (!params.Contains(field.Key))
			    {
				    continue;
			    }
			    CommandResult read = readFloatAtLeast(*params.Find(field.Key), field.Key, 0.0f, *field.Target);
			    if (!read.Success)
			    {
				    return read;
			    }
		    }
		    const struct
		    {
			    const char* Key;
			    bool* Target;
		    } flags[] = {
			    { "enableIBL", &environment.EnableIBL },
			    { "enableShadows", &environment.EnableShadows },
			    { "enableSSAO", &environment.EnableSSAO },
			    { "enableBloom", &environment.EnableBloom },
			    { "enableFXAA", &environment.EnableFXAA },
		    };
		    for (const auto& field : flags)
		    {
			    if (!params.Contains(field.Key))
			    {
				    continue;
			    }
			    CommandResult read = readBool(params, field.Key, false, *field.Target);
			    if (!read.Success)
			    {
				    return read;
			    }
		    }
		    if (params.Contains("hdriPath"))
		    {
			    CommandResult read = readString(params, "hdriPath", false, environment.HdriPath);
			    if (!read.Success)
			    {
				    return read;
			    }
		    }

		    JsonValue result = JsonValue::MakeObject();
		    JsonValue ambient = JsonValue::MakeArray();
		    ambient.Push(static_cast<f64>(environment.AmbientColor.x));
		    ambient.Push(static_cast<f64>(environment.AmbientColor.y));
		    ambient.Push(static_cast<f64>(environment.AmbientColor.z));
		    result.Set("ambientColor", std::move(ambient));
		    result.Set("ambientIntensity", static_cast<f64>(environment.AmbientIntensity));
		    result.Set("hdriPath", environment.HdriPath);
		    result.Set("hdriIntensity", static_cast<f64>(environment.HdriIntensity));
		    result.Set("exposure", static_cast<f64>(environment.Exposure));
		    result.Set("enableIBL", environment.EnableIBL);
		    result.Set("enableShadows", environment.EnableShadows);
		    result.Set("enableSSAO", environment.EnableSSAO);
		    result.Set("enableBloom", environment.EnableBloom);
		    result.Set("enableFXAA", environment.EnableFXAA);
		    result.Set("shadowDistance", static_cast<f64>(environment.ShadowDistance));
		    result.Set("ssaoRadius", static_cast<f64>(environment.SSAORadius));
		    result.Set("bloomThreshold", static_cast<f64>(environment.BloomThreshold));
		    result.Set("bloomIntensity", static_cast<f64>(environment.BloomIntensity));
		    return CommandResult::Ok(result);
	    });

	registerCommand(
	    info("scene.list", "scene", "Lists every entity with its UUID, name, active flag, parent and component names."),
	    [context](const JsonValue&) -> CommandResult
	    {
		    Scene* scene = nullptr;
		    CommandResult sceneResult = requireScene(context, scene);
		    if (!sceneResult.Success)
		    {
			    return sceneResult;
		    }
		    JsonValue entities = JsonValue::MakeArray();
		    for (const Entity& entity : scene->GetEntities())
		    {
			    if (!entity.IsValid())
			    {
				    continue;
			    }
			    JsonValue entry = JsonValue::MakeObject();
			    entry.Set("uuid", entity.GetUuid().ToString());
			    entry.Set("name", entity.GetName());
			    entry.Set("active", entity.IsActive());
			    const TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
			    entry.Set("parent", transform != nullptr ? uuidOrNull(transform->Parent) : JsonValue(nullptr));
			    JsonValue components = JsonValue::MakeArray();
			    for (const ComponentDescriptor& descriptor : componentDescriptors())
			    {
				    if (descriptor.Has(entity))
				    {
					    components.Push(std::string(descriptor.Type));
				    }
			    }
			    entry.Set("components", std::move(components));
			    entities.Push(std::move(entry));
		    }
		    return CommandResult::Ok(entities);
	    });

	registerCommand(info("scene.get",
	                     "scene",
	                     "Dumps one entity: name, active flag, transform, component names and component data.",
	                     { parameter("uuid", "string", true, "Entity UUID") }),
	                [context](const JsonValue& params) -> CommandResult
	                {
		                Scene* scene = nullptr;
		                CommandResult sceneResult = requireScene(context, scene);
		                if (!sceneResult.Success)
		                {
			                return sceneResult;
		                }
		                Entity entity;
		                Uuid uuid;
		                CommandResult result = requireEntity(*scene, params, entity, uuid);
		                if (!result.Success)
		                {
			                return result;
		                }
		                return CommandResult::Ok(entityDocument(entity));
	                });

	registerCommand(info("scene.createEntity",
	                     "scene",
	                     "Creates an empty entity and returns its UUID.",
	                     { parameter("name", "string", true, "Entity name") }),
	                [context](const JsonValue& params) -> CommandResult
	                {
		                Scene* scene = nullptr;
		                CommandResult sceneResult = requireScene(context, scene);
		                if (!sceneResult.Success)
		                {
			                return sceneResult;
		                }
		                std::string name;
		                CommandResult result = readEntityName(params, true, name);
		                if (!result.Success)
		                {
			                return result;
		                }
		                const Entity entity = scene->CreateEntity(name);
		                scene->UpdateTransforms();
		                JsonValue response = JsonValue::MakeObject();
		                response.Set("uuid", entity.GetUuid().ToString());
		                response.Set("name", entity.GetName());
		                response.Set("entityCount", static_cast<u64>(scene->GetEntityCount()));
		                return CommandResult::Ok(response);
	                });

	registerCommand(info("scene.destroyEntity",
	                     "scene",
	                     "Destroys an entity and its descendants; clears the selection when it pointed at it.",
	                     { parameter("uuid", "string", true, "Entity UUID") }),
	                [context](const JsonValue& params) -> CommandResult
	                {
		                Scene* scene = nullptr;
		                CommandResult sceneResult = requireScene(context, scene);
		                if (!sceneResult.Success)
		                {
			                return sceneResult;
		                }
		                Entity entity;
		                Uuid uuid;
		                CommandResult result = requireEntity(*scene, params, entity, uuid);
		                if (!result.Success)
		                {
			                return result;
		                }
		                scene->DestroyEntity(entity);
		                if (context.Selection != nullptr && !scene->FindEntity(*context.Selection).IsValid())
		                {
			                // Also clears the selection when it pointed at a destroyed descendant.
			                *context.Selection = Uuid::Null();
		                }
		                JsonValue response = JsonValue::MakeObject();
		                response.Set("uuid", uuid.ToString());
		                response.Set("destroyed", true);
		                response.Set("entityCount", static_cast<u64>(scene->GetEntityCount()));
		                return CommandResult::Ok(response);
	                });

	registerCommand(
	    info("scene.reparent",
	         "scene",
	         "Moves an entity under a new parent (null detaches it) while preserving its world "
	         "transform.",
	         { parameter("uuid", "string", true, "Entity UUID"),
	           parameter("parent", "string|null", true, "New parent UUID, or null to detach") }),
	    [context](const JsonValue& params) -> CommandResult
	    {
		    Scene* scene = nullptr;
		    CommandResult sceneResult = requireScene(context, scene);
		    if (!sceneResult.Success)
		    {
			    return sceneResult;
		    }
		    Entity entity;
		    Uuid uuid;
		    CommandResult result = requireEntity(*scene, params, entity, uuid);
		    if (!result.Success)
		    {
			    return result;
		    }

		    const JsonValue* parentValue = params.Find("parent");
		    if (parentValue == nullptr)
		    {
			    return CommandResult::Fail("'parent' is required (use null to detach the entity)");
		    }
		    Entity parent;
		    Uuid parentUuid = Uuid::Null();
		    if (!parentValue->IsNull())
		    {
			    if (!parentValue->IsString())
			    {
				    return CommandResult::Fail("'parent' must be an entity UUID string or null");
			    }
			    const std::string parentText(parentValue->AsString());
			    parentUuid = Uuid::FromString(parentText);
			    if (parentUuid.IsNull())
			    {
				    return CommandResult::Fail(std::format("'parent' is not a valid UUID: '{}'", parentText));
			    }
			    if (parentUuid == uuid)
			    {
				    return CommandResult::Fail("an entity cannot be parented to itself");
			    }
			    parent = scene->FindEntity(parentUuid);
			    if (!parent.IsValid())
			    {
				    return CommandResult::Fail(std::format("no parent entity with uuid '{}'", parentUuid.ToString()));
			    }
			    if (!parent.HasComponent<TransformComponent>())
			    {
				    return CommandResult::Fail(
				        std::format("entity '{}' cannot be a parent because it has no transform", parent.GetName()));
			    }
			    if (wouldCreateCycle(*scene, uuid, parentUuid))
			    {
				    return CommandResult::Fail(
				        "reparenting would create a hierarchy cycle through the requested parent");
			    }
		    }

		    entity.SetParent(parent);
		    scene->UpdateTransforms();
		    JsonValue response = JsonValue::MakeObject();
		    response.Set("uuid", uuid.ToString());
		    response.Set("parent", uuidOrNull(parentUuid));
		    return CommandResult::Ok(response);
	    });

	registerCommand(info("scene.rename",
	                     "scene",
	                     "Renames an entity.",
	                     { parameter("uuid", "string", true, "Entity UUID"),
	                       parameter("name", "string", true, "New entity name") }),
	                [context](const JsonValue& params) -> CommandResult
	                {
		                Scene* scene = nullptr;
		                CommandResult sceneResult = requireScene(context, scene);
		                if (!sceneResult.Success)
		                {
			                return sceneResult;
		                }
		                Entity entity;
		                Uuid uuid;
		                CommandResult result = requireEntity(*scene, params, entity, uuid);
		                if (!result.Success)
		                {
			                return result;
		                }
		                std::string name;
		                result = readEntityName(params, true, name);
		                if (!result.Success)
		                {
			                return result;
		                }
		                entity.SetName(name);
		                JsonValue response = JsonValue::MakeObject();
		                response.Set("uuid", uuid.ToString());
		                response.Set("name", entity.GetName());
		                return CommandResult::Ok(response);
	                });

	registerCommand(info("scene.setActive",
	                     "scene",
	                     "Activates or deactivates an entity.",
	                     { parameter("uuid", "string", true, "Entity UUID"),
	                       parameter("active", "bool", true, "New active state") }),
	                [context](const JsonValue& params) -> CommandResult
	                {
		                Scene* scene = nullptr;
		                CommandResult sceneResult = requireScene(context, scene);
		                if (!sceneResult.Success)
		                {
			                return sceneResult;
		                }
		                Entity entity;
		                Uuid uuid;
		                CommandResult result = requireEntity(*scene, params, entity, uuid);
		                if (!result.Success)
		                {
			                return result;
		                }
		                bool active = true;
		                result = readBool(params, "active", true, active);
		                if (!result.Success)
		                {
			                return result;
		                }
		                entity.SetActive(active);
		                JsonValue response = JsonValue::MakeObject();
		                response.Set("uuid", uuid.ToString());
		                response.Set("active", entity.IsActive());
		                return CommandResult::Ok(response);
	                });

	registerCommand(info("scene.new",
	                     "scene",
	                     "Clears the scene, gives it a fresh identity and creates the default primary camera.",
	                     { parameter("name", "string", false, "Scene name (default: Untitled Scene)") }),
	                [context](const JsonValue& params) -> CommandResult
	                {
		                Scene* scene = nullptr;
		                CommandResult sceneResult = requireScene(context, scene);
		                if (!sceneResult.Success)
		                {
			                return sceneResult;
		                }
		                std::string name = "Untitled Scene";
		                CommandResult result = readEntityName(params, false, name);
		                if (!result.Success)
		                {
			                return result;
		                }

		                scene->Clear();
		                scene->SetUuid(Uuid::Generate());
		                scene->SetName(name);
		                scene->SetEnvironment(EnvironmentSettings{});
		                const Entity camera = scene->EnsurePrimaryCamera();
		                scene->UpdateTransforms();
		                if (context.Selection != nullptr)
		                {
			                *context.Selection = Uuid::Null();
		                }

		                JsonValue response = sceneSummary(*scene);
		                response.Set("cameraUuid", camera.GetUuid().ToString());
		                return CommandResult::Ok(response);
	                });

	registerCommand(
	    info("scene.save",
	         "scene",
	         "Saves the scene to a path, or through the editor's SaveScene callback when no path is "
	         "given.",
	         { parameter("path", "string", false, "Scene file path (relative paths resolve against projectRoot)") }),
	    [context](const JsonValue& params) -> CommandResult
	    {
		    Scene* scene = nullptr;
		    CommandResult sceneResult = requireScene(context, scene);
		    if (!sceneResult.Success)
		    {
			    return sceneResult;
		    }
		    const JsonValue* pathValue = params.Find("path");
		    if (pathValue != nullptr && !pathValue->IsNull())
		    {
			    std::string path;
			    CommandResult readResult = readString(params, "path", true, path);
			    if (!readResult.Success)
			    {
				    return readResult;
			    }
			    if (path.empty())
			    {
				    return CommandResult::Fail("'path' must not be empty");
			    }
			    const std::filesystem::path resolved = resolveProjectPath(context, path);
			    if (!SceneSerializer::Save(*scene, resolved))
			    {
				    return CommandResult::Fail(
				        std::format("failed to write the scene to '{}'", FileSystem::ToUtf8(resolved)));
			    }
			    JsonValue response = sceneSummary(*scene);
			    response.Set("path", FileSystem::ToUtf8(resolved));
			    response.Set("saved", true);
			    return CommandResult::Ok(response);
		    }

		    if (!context.SaveScene)
		    {
			    return CommandResult::Fail("scene.save requires a 'path' parameter or an editor SaveScene callback");
		    }
		    std::string savedPathOrError;
		    if (!context.SaveScene(&savedPathOrError))
		    {
			    return CommandResult::Fail(savedPathOrError.empty() ? "the editor failed to save the scene"
			                                                        : savedPathOrError);
		    }
		    JsonValue response = sceneSummary(*scene);
		    response.Set("saved", true);
		    if (!savedPathOrError.empty())
		    {
			    response.Set("path", savedPathOrError);
		    }
		    return CommandResult::Ok(response);
	    });

	registerCommand(
	    info("scene.load",
	         "scene",
	         "Loads a scene file through the editor's LoadScene callback (or directly when the "
	         "editor provides none).",
	         { parameter("path", "string", true, "Scene file path (relative paths resolve against projectRoot)") }),
	    [context](const JsonValue& params) -> CommandResult
	    {
		    Scene* scene = nullptr;
		    CommandResult sceneResult = requireScene(context, scene);
		    if (!sceneResult.Success)
		    {
			    return sceneResult;
		    }
		    std::string path;
		    CommandResult readResult = readString(params, "path", true, path);
		    if (!readResult.Success)
		    {
			    return readResult;
		    }
		    if (path.empty())
		    {
			    return CommandResult::Fail("'path' must not be empty");
		    }

		    const std::filesystem::path resolved = resolveProjectPath(context, path);
		    const std::string resolvedText = FileSystem::ToUtf8(resolved);
		    std::string error;
		    if (context.LoadScene)
		    {
			    if (!context.LoadScene(resolvedText, &error))
			    {
				    return CommandResult::Fail(
				        error.empty() ? std::format("failed to load the scene from '{}'", resolvedText) : error);
			    }
		    }
		    else if (!SceneSerializer::Load(*scene, resolved, &error))
		    {
			    return CommandResult::Fail(
			        error.empty() ? std::format("failed to load the scene from '{}'", resolvedText) : error);
		    }

		    scene->UpdateTransforms();
		    if (context.Selection != nullptr && !scene->FindEntity(*context.Selection).IsValid())
		    {
			    *context.Selection = Uuid::Null();
		    }
		    JsonValue response = sceneSummary(*scene);
		    response.Set("path", resolvedText);
		    return CommandResult::Ok(response);
	    });

	registerCommand(info("scene.snapshot", "scene", "Serialises the current scene and returns the scene document."),
	                [context](const JsonValue&) -> CommandResult
	                {
		                Scene* scene = nullptr;
		                CommandResult sceneResult = requireScene(context, scene);
		                if (!sceneResult.Success)
		                {
			                return sceneResult;
		                }
		                return CommandResult::Ok(SceneSerializer::Serialize(*scene));
	                });

	registerCommand(
	    info("transform.set",
	         "transform",
	         "Sets any combination of local position, rotation (euler degrees or quaternion) and "
	         "scale.",
	         { parameter("uuid", "string", true, "Entity UUID"),
	           parameter("position", "number[3]", false, "Local position [x, y, z]"),
	           parameter("rotation", "number[3]", false, "Local euler rotation in degrees [x, y, z]"),
	           parameter("rotationQuaternion", "number[4]", false, "Local rotation quaternion [x, y, z, w]"),
	           parameter("scale", "number[3]", false, "Local scale [x, y, z]") }),
	    [context](const JsonValue& params) -> CommandResult
	    {
		    Scene* scene = nullptr;
		    CommandResult sceneResult = requireScene(context, scene);
		    if (!sceneResult.Success)
		    {
			    return sceneResult;
		    }
		    Entity entity;
		    Uuid uuid;
		    CommandResult result = requireEntity(*scene, params, entity, uuid);
		    if (!result.Success)
		    {
			    return result;
		    }
		    TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
		    if (transform == nullptr)
		    {
			    return CommandResult::Fail(std::format("entity '{}' has no transform component", entity.GetName()));
		    }

		    const bool hasPosition = params.Contains("position");
		    const bool hasRotation = params.Contains("rotation");
		    const bool hasRotationQuaternion = params.Contains("rotationQuaternion");
		    const bool hasScale = params.Contains("scale");
		    if (!hasPosition && !hasRotation && !hasRotationQuaternion && !hasScale)
		    {
			    return CommandResult::Fail(
			        "transform.set requires at least one of: position, rotation, rotationQuaternion, scale");
		    }
		    if (hasRotation && hasRotationQuaternion)
		    {
			    return CommandResult::Fail(
			        "provide either 'rotation' (euler degrees) or 'rotationQuaternion', not both");
		    }

		    glm::vec3 position(0.0f);
		    glm::vec3 eulerDegrees(0.0f);
		    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
		    glm::vec3 scale(1.0f);
		    if (hasPosition)
		    {
			    result = readVec3(*params.Find("position"), "position", position);
			    if (!result.Success)
			    {
				    return result;
			    }
		    }
		    if (hasRotation)
		    {
			    result = readVec3(*params.Find("rotation"), "rotation", eulerDegrees);
			    if (!result.Success)
			    {
				    return result;
			    }
		    }
		    if (hasRotationQuaternion)
		    {
			    result = readQuaternion(*params.Find("rotationQuaternion"), "rotationQuaternion", rotation);
			    if (!result.Success)
			    {
				    return result;
			    }
		    }
		    if (hasScale)
		    {
			    result = readVec3(*params.Find("scale"), "scale", scale);
			    if (!result.Success)
			    {
				    return result;
			    }
		    }

		    if (hasPosition)
		    {
			    transform->LocalPosition = position;
		    }
		    if (hasRotation)
		    {
			    transform->SetLocalEulerAngles(eulerDegrees);
		    }
		    if (hasRotationQuaternion)
		    {
			    transform->LocalRotation = rotation;
		    }
		    if (hasScale)
		    {
			    transform->LocalScale = scale;
		    }
		    scene->UpdateTransforms();

		    const JsonValue document = entityDocument(entity);
		    return CommandResult::Ok(document["transform"]);
	    });

	registerCommand(info("component.add",
	                     "component",
	                     "Adds a component with default values.",
	                     { parameter("uuid", "string", true, "Entity UUID"),
	                       parameter("type", "string", true, "Component type (see component.get)") }),
	                [context](const JsonValue& params) -> CommandResult
	                {
		                Scene* scene = nullptr;
		                CommandResult sceneResult = requireScene(context, scene);
		                if (!sceneResult.Success)
		                {
			                return sceneResult;
		                }
		                Entity entity;
		                Uuid uuid;
		                CommandResult result = requireEntity(*scene, params, entity, uuid);
		                if (!result.Success)
		                {
			                return result;
		                }
		                std::string type;
		                result = readString(params, "type", true, type);
		                if (!result.Success)
		                {
			                return result;
		                }
		                const ComponentDescriptor* descriptor = findComponentDescriptor(type);
		                if (descriptor == nullptr)
		                {
			                return unknownComponentType(type);
		                }
		                if (descriptor->Has(entity))
		                {
			                return CommandResult::Fail(
			                    std::format("entity '{}' already has a '{}' component", entity.GetName(), type));
		                }
		                descriptor->Add(entity);
		                scene->UpdateTransforms();

		                JsonValue component;
		                result = componentDocument(entity, type, component);
		                if (!result.Success)
		                {
			                return result;
		                }
		                return CommandResult::Ok(component);
	                });

	registerCommand(
	    info("component.remove",
	         "component",
	         "Removes a component from an entity.",
	         { parameter("uuid", "string", true, "Entity UUID"), parameter("type", "string", true, "Component type") }),
	    [context](const JsonValue& params) -> CommandResult
	    {
		    Scene* scene = nullptr;
		    CommandResult sceneResult = requireScene(context, scene);
		    if (!sceneResult.Success)
		    {
			    return sceneResult;
		    }
		    Entity entity;
		    Uuid uuid;
		    CommandResult result = requireEntity(*scene, params, entity, uuid);
		    if (!result.Success)
		    {
			    return result;
		    }
		    std::string type;
		    result = readString(params, "type", true, type);
		    if (!result.Success)
		    {
			    return result;
		    }
		    const ComponentDescriptor* descriptor = findComponentDescriptor(type);
		    if (descriptor == nullptr)
		    {
			    return unknownComponentType(type);
		    }
		    if (!descriptor->Has(entity))
		    {
			    return CommandResult::Fail(std::format("entity '{}' has no '{}' component (current components: {})",
			                                           entity.GetName(),
			                                           type,
			                                           componentNames(entity)));
		    }
		    descriptor->Remove(entity);
		    scene->UpdateTransforms();
		    JsonValue response = JsonValue::MakeObject();
		    response.Set("uuid", uuid.ToString());
		    response.Set("type", type);
		    response.Set("removed", true);
		    return CommandResult::Ok(response);
	    });

	registerCommand(
	    info("component.get",
	         "component",
	         "Returns one component's properties.",
	         { parameter("uuid", "string", true, "Entity UUID"), parameter("type", "string", true, "Component type") }),
	    [context](const JsonValue& params) -> CommandResult
	    {
		    Scene* scene = nullptr;
		    CommandResult sceneResult = requireScene(context, scene);
		    if (!sceneResult.Success)
		    {
			    return sceneResult;
		    }
		    Entity entity;
		    Uuid uuid;
		    CommandResult result = requireEntity(*scene, params, entity, uuid);
		    if (!result.Success)
		    {
			    return result;
		    }
		    std::string type;
		    result = readString(params, "type", true, type);
		    if (!result.Success)
		    {
			    return result;
		    }
		    if (findComponentDescriptor(type) == nullptr)
		    {
			    return unknownComponentType(type);
		    }
		    JsonValue component;
		    result = componentDocument(entity, type, component);
		    if (!result.Success)
		    {
			    return result;
		    }
		    return CommandResult::Ok(component);
	    });

	registerCommand(info("component.set",
	                     "component",
	                     "Sets one validated property of a component and returns the updated component.",
	                     { parameter("uuid", "string", true, "Entity UUID"),
	                       parameter("type", "string", true, "Component type"),
	                       parameter("property", "string", true, "Property name"),
	                       parameter("value", "any", true, "New value (type depends on the property)") }),
	                [context](const JsonValue& params) -> CommandResult
	                {
		                Scene* scene = nullptr;
		                CommandResult sceneResult = requireScene(context, scene);
		                if (!sceneResult.Success)
		                {
			                return sceneResult;
		                }
		                Entity entity;
		                Uuid uuid;
		                CommandResult result = requireEntity(*scene, params, entity, uuid);
		                if (!result.Success)
		                {
			                return result;
		                }
		                std::string type;
		                result = readString(params, "type", true, type);
		                if (!result.Success)
		                {
			                return result;
		                }
		                const ComponentDescriptor* descriptor = findComponentDescriptor(type);
		                if (descriptor == nullptr)
		                {
			                return unknownComponentType(type);
		                }
		                std::string property;
		                result = readString(params, "property", true, property);
		                if (!result.Success)
		                {
			                return result;
		                }
		                if (property.empty())
		                {
			                return CommandResult::Fail(
			                    std::format("'property' must not be empty; valid properties for '{}': {}",
			                                type,
			                                descriptor->Properties));
		                }
		                const JsonValue* value = params.Find("value");
		                if (value == nullptr)
		                {
			                return CommandResult::Fail("'value' is required (the new property value)");
		                }

		                JsonValue values = JsonValue::MakeObject();
		                values.Set(property, *value);
		                CommandResult applyResult = descriptor->Apply(entity, values);
		                if (!applyResult.Success)
		                {
			                return applyResult;
		                }
		                scene->UpdateTransforms();
		                JsonValue component;
		                result = componentDocument(entity, type, component);
		                if (!result.Success)
		                {
			                return result;
		                }
		                return CommandResult::Ok(component);
	                });

	registerCommand(info("component.setMany",
	                     "component",
	                     "Atomically sets several validated properties of one component.",
	                     { parameter("uuid", "string", true, "Entity UUID"),
	                       parameter("type", "string", true, "Component type"),
	                       parameter("values", "object", true, "Object of property names to values") }),
	                [context](const JsonValue& params) -> CommandResult
	                {
		                Scene* scene = nullptr;
		                CommandResult sceneResult = requireScene(context, scene);
		                if (!sceneResult.Success)
		                {
			                return sceneResult;
		                }
		                Entity entity;
		                Uuid uuid;
		                CommandResult result = requireEntity(*scene, params, entity, uuid);
		                if (!result.Success)
		                {
			                return result;
		                }
		                std::string type;
		                result = readString(params, "type", true, type);
		                if (!result.Success)
		                {
			                return result;
		                }
		                const ComponentDescriptor* descriptor = findComponentDescriptor(type);
		                if (descriptor == nullptr)
		                {
			                return unknownComponentType(type);
		                }
		                const JsonValue* values = params.Find("values");
		                if (values == nullptr || values->IsNull())
		                {
			                return CommandResult::Fail("'values' is required (an object of property names to values)");
		                }
		                if (!values->IsObject())
		                {
			                return CommandResult::Fail(
			                    std::format("'values' must be a JSON object; valid properties for '{}': {}",
			                                type,
			                                descriptor->Properties));
		                }

		                CommandResult applyResult = descriptor->Apply(entity, *values);
		                if (!applyResult.Success)
		                {
			                return applyResult;
		                }
		                scene->UpdateTransforms();
		                JsonValue component;
		                result = componentDocument(entity, type, component);
		                if (!result.Success)
		                {
			                return result;
		                }
		                return CommandResult::Ok(component);
	                });

	registerCommand(info("editor.selection.get", "editor", "Returns the selected entity UUID (or null)."),
	                [context](const JsonValue&) -> CommandResult
	                {
		                if (context.Selection == nullptr)
		                {
			                return CommandResult::Fail("the editor command context has no selection");
		                }
		                return CommandResult::Ok(selectionJson(context));
	                });

	registerCommand(info("editor.selection.set",
	                     "editor",
	                     "Selects an entity, or clears the selection with null.",
	                     { parameter("uuid", "string|null", true, "Entity UUID, or null to clear") }),
	                [context](const JsonValue& params) -> CommandResult
	                {
		                if (context.Selection == nullptr)
		                {
			                return CommandResult::Fail("the editor command context has no selection");
		                }
		                const JsonValue* value = params.Find("uuid");
		                if (value == nullptr)
		                {
			                return CommandResult::Fail("'uuid' is required (use null to clear the selection)");
		                }
		                if (value->IsNull())
		                {
			                *context.Selection = Uuid::Null();
			                return CommandResult::Ok(selectionJson(context));
		                }
		                if (!value->IsString())
		                {
			                return CommandResult::Fail("'uuid' must be an entity UUID string or null");
		                }
		                const std::string text(value->AsString());
		                const Uuid uuid = Uuid::FromString(text);
		                if (uuid.IsNull())
		                {
			                return CommandResult::Fail(std::format("'uuid' is not a valid UUID: '{}'", text));
		                }
		                if (context.Scene == nullptr)
		                {
			                return CommandResult::Fail("the editor command context has no active scene");
		                }
		                if (!context.Scene->FindEntity(uuid).IsValid())
		                {
			                return CommandResult::Fail(std::format("no entity with uuid '{}'", uuid.ToString()));
		                }
		                *context.Selection = uuid;
		                return CommandResult::Ok(selectionJson(context));
	                });

	registerCommand(
	    info("editor.screenshot",
	         "editor",
	         "Requests a screenshot of the next rendered frame (written asynchronously).",
	         { parameter("path", "string", true, "PNG output path (relative paths resolve against projectRoot)") }),
	    [context](const JsonValue& params) -> CommandResult
	    {
		    std::string path;
		    CommandResult result = readString(params, "path", true, path);
		    if (!result.Success)
		    {
			    return result;
		    }
		    if (path.empty())
		    {
			    return CommandResult::Fail("'path' must not be empty");
		    }
		    if (!context.ScreenshotRequest)
		    {
			    return CommandResult::Fail("the editor command context has no ScreenshotRequest callback");
		    }
		    const std::filesystem::path resolved = resolveProjectPath(context, path);
		    const std::string resolvedText = FileSystem::ToUtf8(resolved);
		    context.ScreenshotRequest(resolvedText);
		    JsonValue response = JsonValue::MakeObject();
		    response.Set("path", resolvedText);
		    response.Set("requested", true);
		    return CommandResult::Ok(response);
	    });

	registerCommand(info("editor.stats",
	                     "editor",
	                     "Reports renderer state and scene counters. Frame pacing (fps) belongs to the main "
	                     "loop and is reported as null."),
	                [context](const JsonValue&) -> CommandResult
	                {
		                JsonValue result = JsonValue::MakeObject();
		                result.Set("fps", nullptr);
		                result.Set("frameTimeMs", nullptr);
		                if (context.Scene != nullptr)
		                {
			                result.Set("sceneName", context.Scene->GetName());
			                result.Set("entityCount", static_cast<u64>(context.Scene->GetEntityCount()));
		                }
		                else
		                {
			                result.Set("sceneName", nullptr);
			                result.Set("entityCount", 0);
		                }
		                if (context.Renderer != nullptr)
		                {
			                result.Set("width", static_cast<u32>(context.Renderer->GetWidth()));
			                result.Set("height", static_cast<u32>(context.Renderer->GetHeight()));
			                result.Set("gpuName", context.Renderer->GetGpuName());
			                result.Set("rendererInitialized", context.Renderer->IsInitialized());
			                result.Set("vSync", context.Renderer->IsVSync());
		                }
		                else
		                {
			                result.Set("width", 0);
			                result.Set("height", 0);
			                result.Set("gpuName", "unavailable");
			                result.Set("rendererInitialized", false);
			                result.Set("vSync", false);
		                }
		                return CommandResult::Ok(result);
	                });

	registerCommand(info("console.logs",
	                     "console",
	                     "Returns the most recent log messages, oldest first.",
	                     { parameter("limit", "integer", false, "Maximum number of messages (1..2000, default 200)") }),
	                [](const JsonValue& params) -> CommandResult
	                {
		                i64 limit = c_DefaultConsoleLogs;
		                const JsonValue* limitValue = params.Find("limit");
		                if (limitValue != nullptr && !limitValue->IsNull())
		                {
			                if (!limitValue->IsNumber())
			                {
				                return CommandResult::Fail("'limit' must be an integer between 1 and 2000");
			                }
			                const f64 rawLimit = limitValue->AsNumber();
			                if (!std::isfinite(rawLimit) || rawLimit < 1.0 ||
			                    rawLimit > static_cast<f64>(c_MaxConsoleLogs) || std::floor(rawLimit) != rawLimit)
			                {
				                return CommandResult::Fail(
				                    std::format("'limit' must be an integer between 1 and {} (received {})",
				                                c_MaxConsoleLogs,
				                                rawLimit));
			                }
			                limit = static_cast<i64>(rawLimit);
		                }

		                const std::vector<LogMessage> messages = Log::GetRecentMessages();
		                const usize wanted = static_cast<usize>(limit);
		                const usize count = wanted < messages.size() ? wanted : messages.size();
		                JsonValue logs = JsonValue::MakeArray();
		                for (usize index = messages.size() - count; index < messages.size(); ++index)
		                {
			                const LogMessage& message = messages[index];
			                JsonValue entry = JsonValue::MakeObject();
			                entry.Set("level", std::string(LogLevelToString(message.Level)));
			                entry.Set("logger", message.Logger);
			                entry.Set("message", message.Message);
			                entry.Set("timestamp", message.Timestamp);
			                logs.Push(std::move(entry));
		                }
		                return CommandResult::Ok(logs);
	                });
}
} // namespace aegis
