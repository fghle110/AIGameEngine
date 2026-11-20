#include "Aegis/Scripting/LuaEngine.h"

#include "Aegis/Audio/AudioEngine.h"
#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Input.h"
#include "Aegis/Core/KeyCodes.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Physics/PhysicsWorld.h"
#include "Aegis/Scene/Scene.h"

// Lua's public headers are plain C and carry no extern "C" guard of their own
// (that is what lua.hpp adds); the engine includes them directly, so the
// linkage block has to live here.
extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aegis
{
namespace
{
constexpr const char* c_EntityMetatableName = "Aegis.Entity";

// Scripts may build nested property tables; the limit keeps a malformed or
// cyclic table from recursing without bound. A cyclic table is cut off at
// the limit instead of being reported, because the properties table has to
// stay JSON-serialisable.
constexpr int c_MaxJsonDepth = 32;

struct EntityUserdata
{
	Uuid Id;
};

std::string lowerAscii(std::string_view text)
{
	std::string result;
	result.reserve(text.size());
	for (const char character : text)
	{
		const auto byte = static_cast<unsigned char>(character);
		result.push_back(byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a') : character);
	}
	return result;
}

std::string describeEntity(Scene* scene, const Uuid& id)
{
	const Entity entity = scene != nullptr ? scene->FindEntity(id) : Entity();
	if (entity.IsValid())
	{
		return std::format("'{}' ({})", entity.GetName(), id.ToString());
	}
	return std::format("<destroyed> ({})", id.ToString());
}

std::string entityNameFor(Scene* scene, const Uuid& id)
{
	const Entity entity = scene != nullptr ? scene->FindEntity(id) : Entity();
	return entity.IsValid() ? entity.GetName() : std::string("<destroyed>");
}

// ------------------------------------------------------------------
// JSON <-> Lua
// ------------------------------------------------------------------

void pushJsonValue(lua_State* L, const JsonValue& value, int depth = 0)
{
	if (depth > c_MaxJsonDepth)
	{
		lua_pushnil(L);
		return;
	}

	switch (value.GetType())
	{
	case JsonType::Null:
		lua_pushnil(L);
		return;
	case JsonType::Bool:
		lua_pushboolean(L, value.AsBool() ? 1 : 0);
		return;
	case JsonType::Number:
		lua_pushnumber(L, static_cast<lua_Number>(value.AsNumber()));
		return;
	case JsonType::String:
	{
		const std::string_view text = value.AsString();
		lua_pushlstring(L, text.data(), text.size());
		return;
	}
	case JsonType::Array:
	{
		lua_createtable(L, static_cast<int>(value.GetSize()), 0);
		for (usize index = 0; index < value.GetSize(); ++index)
		{
			pushJsonValue(L, value[index], depth + 1);
			lua_rawseti(L, -2, static_cast<lua_Integer>(index + 1));
		}
		return;
	}
	case JsonType::Object:
	{
		lua_createtable(L, 0, static_cast<int>(value.GetObject().size()));
		for (const auto& entry : value.GetObject())
		{
			lua_pushlstring(L, entry.first.data(), entry.first.size());
			pushJsonValue(L, entry.second, depth + 1);
			lua_rawset(L, -3);
		}
		return;
	}
	}
}

JsonValue readJsonValue(lua_State* L, int index, int depth = 0)
{
	if (depth > c_MaxJsonDepth)
	{
		return JsonValue();
	}

	const int absolute = lua_absindex(L, index);
	switch (lua_type(L, absolute))
	{
	case LUA_TNONE:
	case LUA_TNIL:
		return JsonValue();
	case LUA_TBOOLEAN:
		return JsonValue(lua_toboolean(L, absolute) != 0);
	case LUA_TNUMBER:
		return JsonValue(static_cast<f64>(lua_tonumber(L, absolute)));
	case LUA_TSTRING:
	{
		size_t length = 0;
		const char* text = lua_tolstring(L, absolute, &length);
		return JsonValue(text != nullptr ? std::string_view(text, length) : std::string_view{});
	}
	case LUA_TTABLE:
	{
		// String keys make it an object; a pure sequence becomes an array.
		// An empty table is an empty object, which is what a properties
		// table starts as.
		bool hasStringKey = false;
		bool hasUnsupportedKey = false;
		lua_pushnil(L);
		while (lua_next(L, absolute) != 0)
		{
			lua_pop(L, 1); // drop the value, keep the key for lua_next
			const int keyType = lua_type(L, -1);
			if (keyType == LUA_TSTRING)
			{
				hasStringKey = true;
			}
			else if (keyType != LUA_TNUMBER)
			{
				hasUnsupportedKey = true;
			}
		}

		if (hasStringKey || hasUnsupportedKey)
		{
			JsonValue object = JsonValue::MakeObject();
			lua_pushnil(L);
			while (lua_next(L, absolute) != 0)
			{
				if (lua_type(L, -2) == LUA_TSTRING)
				{
					size_t length = 0;
					const char* key = lua_tolstring(L, -2, &length);
					object.Set(std::string_view(key, length), readJsonValue(L, lua_gettop(L), depth + 1));
				}
				lua_pop(L, 1); // drop the value, keep the key for lua_next
			}
			return object;
		}

		JsonValue array = JsonValue::MakeArray();
		const size_t length = lua_rawlen(L, absolute);
		for (size_t position = 1; position <= length; ++position)
		{
			lua_rawgeti(L, absolute, static_cast<lua_Integer>(position));
			array.Push(readJsonValue(L, lua_gettop(L), depth + 1));
			lua_pop(L, 1);
		}
		return array;
	}
	default:
		return JsonValue();
	}
}

// ------------------------------------------------------------------
// Table field helpers
// ------------------------------------------------------------------

void setNumberField(lua_State* L, int tableIndex, const char* key, f64 value)
{
	const int table = lua_absindex(L, tableIndex);
	lua_pushnumber(L, static_cast<lua_Number>(value));
	lua_setfield(L, table, key);
}

void setBoolField(lua_State* L, int tableIndex, const char* key, bool value)
{
	const int table = lua_absindex(L, tableIndex);
	lua_pushboolean(L, value ? 1 : 0);
	lua_setfield(L, table, key);
}

void setStringField(lua_State* L, int tableIndex, const char* key, std::string_view value)
{
	const int table = lua_absindex(L, tableIndex);
	lua_pushlstring(L, value.data(), value.size());
	lua_setfield(L, table, key);
}

void setFunctionField(lua_State* L, int tableIndex, const char* name, lua_CFunction function)
{
	const int table = lua_absindex(L, tableIndex);
	lua_pushcfunction(L, function);
	lua_setfield(L, table, name);
}

bool readNumberField(lua_State* L, int tableIndex, const char* key, f32& out)
{
	const int table = lua_absindex(L, tableIndex);
	lua_getfield(L, table, key);
	bool found = false;
	if (lua_isnumber(L, -1) != 0)
	{
		const f64 value = static_cast<f64>(lua_tonumber(L, -1));
		if (std::isfinite(value))
		{
			out = static_cast<f32>(value);
			found = true;
		}
	}
	lua_pop(L, 1);
	return found;
}

// Reads a numeric field that has a second name (currently only camera.fov).
// A table read back from getComponent carries both names, so the value that
// differs from the component's current value is the one the script changed;
// the canonical name wins when both changed.
bool readNumberFieldWithAlias(lua_State* L, int tableIndex, const char* key, const char* alias, f32 current, f32& out)
{
	f32 canonical = 0.0f;
	f32 aliased = 0.0f;
	const bool hasCanonical = readNumberField(L, tableIndex, key, canonical);
	const bool hasAlias = readNumberField(L, tableIndex, alias, aliased);
	if (!hasCanonical && !hasAlias)
	{
		return false;
	}
	if (hasCanonical && hasAlias)
	{
		out = canonical == current && aliased != current ? aliased : canonical;
		return true;
	}
	out = hasCanonical ? canonical : aliased;
	return true;
}

bool readBoolField(lua_State* L, int tableIndex, const char* key, bool& out)
{
	const int table = lua_absindex(L, tableIndex);
	lua_getfield(L, table, key);
	bool found = false;
	if (lua_isboolean(L, -1) != 0)
	{
		out = lua_toboolean(L, -1) != 0;
		found = true;
	}
	lua_pop(L, 1);
	return found;
}

// ------------------------------------------------------------------
// Enumerations by name
// ------------------------------------------------------------------

struct EnumEntry
{
	std::string_view Name;
	i32 Value;
};

constexpr EnumEntry c_CameraProjectionNames[] = {
	{ "perspective", 0 },
	{ "orthographic", 1 },
};

constexpr EnumEntry c_LightTypeNames[] = {
	{ "directional", 0 },
	{ "point", 1 },
	{ "spot", 2 },
};

constexpr EnumEntry c_BodyTypeNames[] = {
	{ "static", 0 },
	{ "kinematic", 1 },
	{ "dynamic", 2 },
};

constexpr EnumEntry c_ColliderShapeNames[] = {
	{ "box", 0 },
	{ "sphere", 1 },
	{ "capsule", 2 },
	{ "mesh", 3 },
};

bool enumValueFromText(std::string_view text, const EnumEntry* entries, usize count, i32& out)
{
	const std::string normalised = lowerAscii(text);
	for (usize index = 0; index < count; ++index)
	{
		if (normalised == entries[index].Name)
		{
			out = entries[index].Value;
			return true;
		}
	}
	return false;
}

const char* enumTextFromValue(i32 value, const EnumEntry* entries, usize count, const char* fallback)
{
	for (usize index = 0; index < count; ++index)
	{
		if (entries[index].Value == value)
		{
			return entries[index].Name.data();
		}
	}
	return fallback;
}

bool readEnumField(lua_State* L, int tableIndex, const char* key, const EnumEntry* entries, usize count, i32& out)
{
	const int table = lua_absindex(L, tableIndex);
	lua_getfield(L, table, key);
	bool found = false;
	if (lua_isnumber(L, -1) != 0)
	{
		out = static_cast<i32>(lua_tointeger(L, -1));
		found = true;
	}
	else if (lua_isstring(L, -1) != 0)
	{
		size_t length = 0;
		const char* text = lua_tolstring(L, -1, &length);
		found = text != nullptr && enumValueFromText(std::string_view(text, length), entries, count, out);
	}
	lua_pop(L, 1);
	return found;
}

void setEnumField(lua_State* L,
                  int tableIndex,
                  const char* key,
                  i32 value,
                  const EnumEntry* entries,
                  usize count,
                  const char* fallback)
{
	setStringField(L, tableIndex, key, enumTextFromValue(value, entries, count, fallback));
}

// ------------------------------------------------------------------
// Vectors. Positions, rotations, scales and colours are tables; input
// accepts {x=,y=,z=}, {r=,g=,b=} and the array form [1]..[3].
// ------------------------------------------------------------------

bool readVectorComponents(lua_State* L, int tableIndex, f32* values, int count)
{
	const int table = lua_absindex(L, tableIndex);
	if (lua_istable(L, table) == 0)
	{
		return false;
	}

	static constexpr const char* c_AxisNames[] = { "x", "y", "z", "w" };
	static constexpr const char* c_ColorNames[] = { "r", "g", "b", "a" };

	bool any = false;
	for (int axis = 0; axis < count; ++axis)
	{
		f32 value = 0.0f;
		bool found =
		    readNumberField(L, table, c_AxisNames[axis], value) || readNumberField(L, table, c_ColorNames[axis], value);
		if (!found)
		{
			lua_rawgeti(L, table, static_cast<lua_Integer>(axis + 1));
			if (lua_isnumber(L, -1) != 0)
			{
				const f64 raw = static_cast<f64>(lua_tonumber(L, -1));
				if (std::isfinite(raw))
				{
					value = static_cast<f32>(raw);
					found = true;
				}
			}
			lua_pop(L, 1);
		}
		if (found)
		{
			values[axis] = value;
			any = true;
		}
	}
	return any;
}

bool readVector3(lua_State* L, int index, glm::vec3& out)
{
	f32 values[3] = { 0.0f, 0.0f, 0.0f };
	if (!readVectorComponents(L, index, values, 3))
	{
		return false;
	}
	out = glm::vec3(values[0], values[1], values[2]);
	return true;
}

bool readVector4(lua_State* L, int index, glm::vec4& out)
{
	f32 values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	if (!readVectorComponents(L, index, values, 4))
	{
		return false;
	}
	out = glm::vec4(values[0], values[1], values[2], values[3]);
	return true;
}

bool readVector3Field(lua_State* L, int tableIndex, const char* key, glm::vec3& out)
{
	const int table = lua_absindex(L, tableIndex);
	lua_getfield(L, table, key);
	const bool found = lua_istable(L, -1) != 0 && readVector3(L, lua_gettop(L), out);
	lua_pop(L, 1);
	return found;
}

bool readVector4Field(lua_State* L, int tableIndex, const char* key, glm::vec4& out)
{
	const int table = lua_absindex(L, tableIndex);
	lua_getfield(L, table, key);
	const bool found = lua_istable(L, -1) != 0 && readVector4(L, lua_gettop(L), out);
	lua_pop(L, 1);
	return found;
}

void pushVector3(lua_State* L, const glm::vec3& value)
{
	lua_createtable(L, 0, 3);
	setNumberField(L, -1, "x", value.x);
	setNumberField(L, -1, "y", value.y);
	setNumberField(L, -1, "z", value.z);
}

void pushVector4(lua_State* L, const glm::vec4& value)
{
	lua_createtable(L, 0, 4);
	setNumberField(L, -1, "x", value.x);
	setNumberField(L, -1, "y", value.y);
	setNumberField(L, -1, "z", value.z);
	setNumberField(L, -1, "w", value.w);
}

// ------------------------------------------------------------------
// Asset handles. Lua sees { id = "<uuid>|nil", type = "<type>" }; a bare
// uuid string keeps the type the field already had.
// ------------------------------------------------------------------

void pushAssetHandle(lua_State* L, const AssetHandle& handle)
{
	lua_createtable(L, 0, 2);
	if (handle.Id.IsNull())
	{
		lua_pushnil(L);
	}
	else
	{
		const std::string id = handle.Id.ToString();
		lua_pushlstring(L, id.data(), id.size());
	}
	lua_setfield(L, -2, "id");
	setStringField(L, -1, "type", AssetTypeToString(handle.Type));
}

bool readAssetHandle(lua_State* L, int index, AssetHandle& out)
{
	if (lua_isstring(L, index) != 0)
	{
		size_t length = 0;
		const char* text = lua_tolstring(L, index, &length);
		const Uuid id = text != nullptr ? Uuid::FromString(std::string_view(text, length)) : Uuid::Null();
		out.Id = id;
		return !id.IsNull();
	}

	if (lua_istable(L, index) == 0)
	{
		return false;
	}

	const int table = lua_absindex(L, index);
	AssetHandle handle = out;
	lua_getfield(L, table, "id");
	if (lua_isstring(L, -1) != 0)
	{
		size_t length = 0;
		const char* text = lua_tolstring(L, -1, &length);
		handle.Id = text != nullptr ? Uuid::FromString(std::string_view(text, length)) : Uuid::Null();
	}
	else if (lua_isnil(L, -1) == 0)
	{
		lua_pop(L, 1);
		return false;
	}
	else
	{
		handle.Id = Uuid::Null();
	}
	lua_pop(L, 1);

	lua_getfield(L, table, "type");
	if (lua_isstring(L, -1) != 0)
	{
		size_t length = 0;
		const char* text = lua_tolstring(L, -1, &length);
		if (text != nullptr)
		{
			handle.Type = AssetTypeFromString(std::string_view(text, length));
		}
	}
	lua_pop(L, 1);

	out = handle;
	return true;
}

bool readAssetField(lua_State* L, int tableIndex, const char* key, AssetHandle& out)
{
	const int table = lua_absindex(L, tableIndex);
	lua_getfield(L, table, key);
	const bool found = readAssetHandle(L, lua_gettop(L), out);
	lua_pop(L, 1);
	return found;
}

// ------------------------------------------------------------------
// Entity userdata
// ------------------------------------------------------------------

void pushEntityUserdata(lua_State* L, const Uuid& id)
{
	void* memory = lua_newuserdatauv(L, sizeof(EntityUserdata), 0);
	new (memory) EntityUserdata{ id };
	luaL_getmetatable(L, c_EntityMetatableName);
	lua_setmetatable(L, -2);
}

EntityUserdata* checkEntityUserdata(lua_State* L, int index)
{
	return static_cast<EntityUserdata*>(luaL_checkudata(L, index, c_EntityMetatableName));
}

// ------------------------------------------------------------------
// Components
// ------------------------------------------------------------------

enum class ComponentType : u8
{
	None = 0,
	Camera,
	Light,
	MeshRenderer,
	RigidBody,
	Collider,
	AudioSource,
	AudioListener,
	FlyCamera,
	LuaScript
};

ComponentType componentTypeFromName(std::string_view name)
{
	const std::string normalised = lowerAscii(name);
	if (normalised == "camera")
	{
		return ComponentType::Camera;
	}
	if (normalised == "light")
	{
		return ComponentType::Light;
	}
	if (normalised == "meshrenderer")
	{
		return ComponentType::MeshRenderer;
	}
	if (normalised == "rigidbody")
	{
		return ComponentType::RigidBody;
	}
	if (normalised == "collider")
	{
		return ComponentType::Collider;
	}
	if (normalised == "audiosource")
	{
		return ComponentType::AudioSource;
	}
	if (normalised == "audiolistener")
	{
		return ComponentType::AudioListener;
	}
	if (normalised == "flycamera")
	{
		return ComponentType::FlyCamera;
	}
	if (normalised == "luascript")
	{
		return ComponentType::LuaScript;
	}
	return ComponentType::None;
}

void clampCamera(CameraComponent& camera)
{
	camera.FieldOfView = Clamp(camera.FieldOfView, 1.0f, 179.0f);
	camera.NearClip = Max(camera.NearClip, 0.001f);
	camera.FarClip = Max(camera.FarClip, camera.NearClip + 0.001f);
	camera.OrthographicSize = Max(camera.OrthographicSize, 0.001f);
	camera.ClearColor = glm::clamp(camera.ClearColor, glm::vec4(0.0f), glm::vec4(1.0f));
}

void clampLight(LightComponent& light)
{
	light.Intensity = Max(light.Intensity, 0.0f);
	light.Range = Max(light.Range, 0.0f);
	light.InnerConeDegrees = Clamp(light.InnerConeDegrees, 0.0f, 89.9f);
	light.OuterConeDegrees = Clamp(light.OuterConeDegrees, 0.0f, 89.9f);
	light.InnerConeDegrees = Min(light.InnerConeDegrees, light.OuterConeDegrees);
}

void clampRigidBody(RigidBodyComponent& body)
{
	body.Mass = Max(body.Mass, 0.001f);
	body.Friction = Max(body.Friction, 0.0f);
	body.Restitution = Clamp(body.Restitution, 0.0f, 1.0f);
	body.LinearDamping = Max(body.LinearDamping, 0.0f);
	body.AngularDamping = Max(body.AngularDamping, 0.0f);
}

void clampCollider(ColliderComponent& collider)
{
	collider.HalfExtents.x = Max(collider.HalfExtents.x, 0.001f);
	collider.HalfExtents.y = Max(collider.HalfExtents.y, 0.001f);
	collider.HalfExtents.z = Max(collider.HalfExtents.z, 0.001f);
	collider.Radius = Max(collider.Radius, 0.001f);
	collider.HalfHeight = Max(collider.HalfHeight, 0.0f);
}

void clampAudioSource(AudioSourceComponent& source)
{
	source.Volume = AudioEngine::ClampVolume(source.Volume);
	source.Pitch = AudioEngine::ClampPitch(source.Pitch);
	source.MinDistance = Max(source.MinDistance, 0.0f);
	source.MaxDistance = Max(source.MaxDistance, source.MinDistance);
}

void clampFlyCamera(FlyCameraComponent& flyCamera)
{
	flyCamera.MoveSpeed = Max(flyCamera.MoveSpeed, 0.0f);
	flyCamera.FastMultiplier = Max(flyCamera.FastMultiplier, 0.0f);
	flyCamera.LookSensitivity = Max(flyCamera.LookSensitivity, 0.0f);
	flyCamera.SpeedDamping = Max(flyCamera.SpeedDamping, 0.0f);
}

void pushCamera(lua_State* L, const CameraComponent& camera)
{
	lua_createtable(L, 0, 7);
	setEnumField(L,
	             -1,
	             "projection",
	             static_cast<i32>(camera.Projection),
	             c_CameraProjectionNames,
	             ArrayCount(c_CameraProjectionNames),
	             "perspective");
	setNumberField(L, -1, "fieldOfView", camera.FieldOfView);
	setNumberField(L, -1, "nearClip", camera.NearClip);
	setNumberField(L, -1, "farClip", camera.FarClip);
	setNumberField(L, -1, "orthographicSize", camera.OrthographicSize);
	setBoolField(L, -1, "primary", camera.Primary);
	pushVector4(L, camera.ClearColor);
	lua_setfield(L, -2, "clearColor");
}

void applyCamera(lua_State* L, int index, CameraComponent& camera)
{
	i32 projection = static_cast<i32>(camera.Projection);
	if (readEnumField(L, index, "projection", c_CameraProjectionNames, ArrayCount(c_CameraProjectionNames), projection))
	{
		camera.Projection = projection == 1 ? CameraProjection::Orthographic : CameraProjection::Perspective;
	}
	f32 number = 0.0f;
	if (readNumberFieldWithAlias(L, index, "fieldOfView", "fov", camera.FieldOfView, number))
	{
		camera.FieldOfView = number;
	}
	if (readNumberField(L, index, "nearClip", number))
	{
		camera.NearClip = number;
	}
	if (readNumberField(L, index, "farClip", number))
	{
		camera.FarClip = number;
	}
	if (readNumberField(L, index, "orthographicSize", number))
	{
		camera.OrthographicSize = number;
	}
	bool primary = camera.Primary;
	if (readBoolField(L, index, "primary", primary))
	{
		camera.Primary = primary;
	}
	glm::vec4 clearColor = camera.ClearColor;
	if (readVector4Field(L, index, "clearColor", clearColor))
	{
		camera.ClearColor = clearColor;
	}
}

void pushLight(lua_State* L, const LightComponent& light)
{
	lua_createtable(L, 0, 7);
	setEnumField(L,
	             -1,
	             "type",
	             static_cast<i32>(light.Type),
	             c_LightTypeNames,
	             ArrayCount(c_LightTypeNames),
	             "directional");
	pushVector3(L, light.Color);
	lua_setfield(L, -2, "color");
	setNumberField(L, -1, "intensity", light.Intensity);
	setNumberField(L, -1, "range", light.Range);
	setNumberField(L, -1, "innerConeDegrees", light.InnerConeDegrees);
	setNumberField(L, -1, "outerConeDegrees", light.OuterConeDegrees);
	setBoolField(L, -1, "castShadows", light.CastShadows);
}

void applyLight(lua_State* L, int index, LightComponent& light)
{
	i32 type = static_cast<i32>(light.Type);
	if (readEnumField(L, index, "type", c_LightTypeNames, ArrayCount(c_LightTypeNames), type))
	{
		light.Type = static_cast<LightType>(type);
	}
	glm::vec3 color = light.Color;
	if (readVector3Field(L, index, "color", color))
	{
		light.Color = color;
	}
	f32 number = 0.0f;
	if (readNumberField(L, index, "intensity", number))
	{
		light.Intensity = number;
	}
	if (readNumberField(L, index, "range", number))
	{
		light.Range = number;
	}
	if (readNumberField(L, index, "innerConeDegrees", number))
	{
		light.InnerConeDegrees = number;
	}
	if (readNumberField(L, index, "outerConeDegrees", number))
	{
		light.OuterConeDegrees = number;
	}
	bool castShadows = light.CastShadows;
	if (readBoolField(L, index, "castShadows", castShadows))
	{
		light.CastShadows = castShadows;
	}
}

void pushMeshRenderer(lua_State* L, const MeshRendererComponent& renderer)
{
	lua_createtable(L, 0, 5);
	pushAssetHandle(L, renderer.Mesh);
	lua_setfield(L, -2, "mesh");
	lua_createtable(L, static_cast<int>(renderer.Materials.size()), 0);
	for (usize index = 0; index < renderer.Materials.size(); ++index)
	{
		pushAssetHandle(L, renderer.Materials[index]);
		lua_rawseti(L, -2, static_cast<lua_Integer>(index + 1));
	}
	lua_setfield(L, -2, "materials");
	setBoolField(L, -1, "visible", renderer.Visible);
	setBoolField(L, -1, "castShadows", renderer.CastShadows);
	setBoolField(L, -1, "receiveShadows", renderer.ReceiveShadows);
}

void applyMeshRenderer(lua_State* L, int index, MeshRendererComponent& renderer)
{
	AssetHandle mesh = renderer.Mesh;
	if (readAssetField(L, index, "mesh", mesh))
	{
		renderer.Mesh = mesh;
	}

	const int table = lua_absindex(L, index);
	lua_getfield(L, table, "materials");
	if (lua_istable(L, -1) != 0)
	{
		const int materials = lua_absindex(L, -1);
		const size_t count = lua_rawlen(L, materials);
		std::vector<AssetHandle> handles;
		handles.reserve(count);
		for (size_t position = 1; position <= count; ++position)
		{
			lua_rawgeti(L, materials, static_cast<lua_Integer>(position));
			AssetHandle handle;
			if (readAssetHandle(L, lua_gettop(L), handle))
			{
				handles.push_back(handle);
			}
			lua_pop(L, 1);
		}
		renderer.Materials = std::move(handles);
	}
	lua_pop(L, 1);

	bool flag = renderer.Visible;
	if (readBoolField(L, index, "visible", flag))
	{
		renderer.Visible = flag;
	}
	flag = renderer.CastShadows;
	if (readBoolField(L, index, "castShadows", flag))
	{
		renderer.CastShadows = flag;
	}
	flag = renderer.ReceiveShadows;
	if (readBoolField(L, index, "receiveShadows", flag))
	{
		renderer.ReceiveShadows = flag;
	}
}

void pushRigidBody(lua_State* L, const RigidBodyComponent& body)
{
	lua_createtable(L, 0, 7);
	setEnumField(L, -1, "type", static_cast<i32>(body.Type), c_BodyTypeNames, ArrayCount(c_BodyTypeNames), "static");
	setNumberField(L, -1, "mass", body.Mass);
	setNumberField(L, -1, "friction", body.Friction);
	setNumberField(L, -1, "restitution", body.Restitution);
	setNumberField(L, -1, "linearDamping", body.LinearDamping);
	setNumberField(L, -1, "angularDamping", body.AngularDamping);
	setBoolField(L, -1, "useGravity", body.UseGravity);
}

void applyRigidBody(lua_State* L, int index, RigidBodyComponent& body)
{
	i32 type = static_cast<i32>(body.Type);
	if (readEnumField(L, index, "type", c_BodyTypeNames, ArrayCount(c_BodyTypeNames), type))
	{
		body.Type = static_cast<BodyType>(type);
	}
	f32 number = 0.0f;
	if (readNumberField(L, index, "mass", number))
	{
		body.Mass = number;
	}
	if (readNumberField(L, index, "friction", number))
	{
		body.Friction = number;
	}
	if (readNumberField(L, index, "restitution", number))
	{
		body.Restitution = number;
	}
	if (readNumberField(L, index, "linearDamping", number))
	{
		body.LinearDamping = number;
	}
	if (readNumberField(L, index, "angularDamping", number))
	{
		body.AngularDamping = number;
	}
	bool useGravity = body.UseGravity;
	if (readBoolField(L, index, "useGravity", useGravity))
	{
		body.UseGravity = useGravity;
	}
}

void pushCollider(lua_State* L, const ColliderComponent& collider)
{
	lua_createtable(L, 0, 6);
	setEnumField(L,
	             -1,
	             "shape",
	             static_cast<i32>(collider.Shape),
	             c_ColliderShapeNames,
	             ArrayCount(c_ColliderShapeNames),
	             "box");
	pushVector3(L, collider.HalfExtents);
	lua_setfield(L, -2, "halfExtents");
	setNumberField(L, -1, "radius", collider.Radius);
	setNumberField(L, -1, "halfHeight", collider.HalfHeight);
	pushVector3(L, collider.Offset);
	lua_setfield(L, -2, "offset");
	setBoolField(L, -1, "isTrigger", collider.IsTrigger);
}

void applyCollider(lua_State* L, int index, ColliderComponent& collider)
{
	i32 shape = static_cast<i32>(collider.Shape);
	if (readEnumField(L, index, "shape", c_ColliderShapeNames, ArrayCount(c_ColliderShapeNames), shape))
	{
		collider.Shape = static_cast<ColliderShape>(shape);
	}
	glm::vec3 vector = collider.HalfExtents;
	if (readVector3Field(L, index, "halfExtents", vector))
	{
		collider.HalfExtents = vector;
	}
	f32 number = 0.0f;
	if (readNumberField(L, index, "radius", number))
	{
		collider.Radius = number;
	}
	if (readNumberField(L, index, "halfHeight", number))
	{
		collider.HalfHeight = number;
	}
	vector = collider.Offset;
	if (readVector3Field(L, index, "offset", vector))
	{
		collider.Offset = vector;
	}
	bool isTrigger = collider.IsTrigger;
	if (readBoolField(L, index, "isTrigger", isTrigger))
	{
		collider.IsTrigger = isTrigger;
	}
}

void pushAudioSource(lua_State* L, const AudioSourceComponent& source)
{
	lua_createtable(L, 0, 8);
	pushAssetHandle(L, source.Clip);
	lua_setfield(L, -2, "clip");
	setNumberField(L, -1, "volume", source.Volume);
	setNumberField(L, -1, "pitch", source.Pitch);
	setBoolField(L, -1, "loop", source.Loop);
	setBoolField(L, -1, "playOnStart", source.PlayOnStart);
	setBoolField(L, -1, "spatial", source.Spatial);
	setNumberField(L, -1, "minDistance", source.MinDistance);
	setNumberField(L, -1, "maxDistance", source.MaxDistance);
}

void applyAudioSource(lua_State* L, int index, AudioSourceComponent& source)
{
	AssetHandle clip = source.Clip;
	if (readAssetField(L, index, "clip", clip))
	{
		source.Clip = clip;
	}
	f32 number = 0.0f;
	if (readNumberField(L, index, "volume", number))
	{
		source.Volume = number;
	}
	if (readNumberField(L, index, "pitch", number))
	{
		source.Pitch = number;
	}
	if (readNumberField(L, index, "minDistance", number))
	{
		source.MinDistance = number;
	}
	if (readNumberField(L, index, "maxDistance", number))
	{
		source.MaxDistance = number;
	}
	bool flag = source.Loop;
	if (readBoolField(L, index, "loop", flag))
	{
		source.Loop = flag;
	}
	flag = source.PlayOnStart;
	if (readBoolField(L, index, "playOnStart", flag))
	{
		source.PlayOnStart = flag;
	}
	flag = source.Spatial;
	if (readBoolField(L, index, "spatial", flag))
	{
		source.Spatial = flag;
	}
}

void pushAudioListener(lua_State* L, const AudioListenerComponent& listener)
{
	lua_createtable(L, 0, 1);
	setBoolField(L, -1, "primary", listener.Primary);
}

void applyAudioListener(lua_State* L, int index, AudioListenerComponent& listener)
{
	bool primary = listener.Primary;
	if (readBoolField(L, index, "primary", primary))
	{
		listener.Primary = primary;
	}
}

void pushFlyCamera(lua_State* L, const FlyCameraComponent& flyCamera)
{
	lua_createtable(L, 0, 6);
	setBoolField(L, -1, "active", flyCamera.Active);
	setNumberField(L, -1, "moveSpeed", flyCamera.MoveSpeed);
	setNumberField(L, -1, "fastMultiplier", flyCamera.FastMultiplier);
	setNumberField(L, -1, "lookSensitivity", flyCamera.LookSensitivity);
	setNumberField(L, -1, "speedDamping", flyCamera.SpeedDamping);
	pushVector3(L, flyCamera.Velocity);
	lua_setfield(L, -2, "velocity");
}

void applyFlyCamera(lua_State* L, int index, FlyCameraComponent& flyCamera)
{
	bool active = flyCamera.Active;
	if (readBoolField(L, index, "active", active))
	{
		flyCamera.Active = active;
	}
	f32 number = 0.0f;
	if (readNumberField(L, index, "moveSpeed", number))
	{
		flyCamera.MoveSpeed = number;
	}
	if (readNumberField(L, index, "fastMultiplier", number))
	{
		flyCamera.FastMultiplier = number;
	}
	if (readNumberField(L, index, "lookSensitivity", number))
	{
		flyCamera.LookSensitivity = number;
	}
	if (readNumberField(L, index, "speedDamping", number))
	{
		flyCamera.SpeedDamping = number;
	}
	glm::vec3 velocity = flyCamera.Velocity;
	if (readVector3Field(L, index, "velocity", velocity))
	{
		flyCamera.Velocity = velocity;
	}
}

void pushLuaScript(lua_State* L, const LuaScriptComponent& script)
{
	lua_createtable(L, 0, 3);
	pushAssetHandle(L, script.Script);
	lua_setfield(L, -2, "script");
	setBoolField(L, -1, "enabled", script.Enabled);
	pushJsonValue(L, script.Properties.IsObject() ? script.Properties : JsonValue::MakeObject());
	lua_setfield(L, -2, "properties");
}

void applyLuaScript(lua_State* L, int index, LuaScriptComponent& script)
{
	AssetHandle asset = script.Script;
	if (readAssetField(L, index, "script", asset))
	{
		script.Script = asset;
	}
	bool enabled = script.Enabled;
	if (readBoolField(L, index, "enabled", enabled))
	{
		script.Enabled = enabled;
	}

	const int table = lua_absindex(L, index);
	lua_getfield(L, table, "properties");
	if (lua_istable(L, -1) != 0)
	{
		JsonValue properties = readJsonValue(L, lua_gettop(L));
		if (properties.IsObject())
		{
			script.Properties = std::move(properties);
		}
	}
	lua_pop(L, 1);
}

bool entityHasComponent(const Entity& entity, ComponentType type)
{
	switch (type)
	{
	case ComponentType::Camera:
		return entity.HasComponent<CameraComponent>();
	case ComponentType::Light:
		return entity.HasComponent<LightComponent>();
	case ComponentType::MeshRenderer:
		return entity.HasComponent<MeshRendererComponent>();
	case ComponentType::RigidBody:
		return entity.HasComponent<RigidBodyComponent>();
	case ComponentType::Collider:
		return entity.HasComponent<ColliderComponent>();
	case ComponentType::AudioSource:
		return entity.HasComponent<AudioSourceComponent>();
	case ComponentType::AudioListener:
		return entity.HasComponent<AudioListenerComponent>();
	case ComponentType::FlyCamera:
		return entity.HasComponent<FlyCameraComponent>();
	case ComponentType::LuaScript:
		return entity.HasComponent<LuaScriptComponent>();
	case ComponentType::None:
		break;
	}
	return false;
}

bool pushComponentTable(lua_State* L, const Entity& entity, ComponentType type)
{
	switch (type)
	{
	case ComponentType::Camera:
	{
		const CameraComponent* camera = entity.TryGetComponent<CameraComponent>();
		if (camera == nullptr)
		{
			break;
		}
		pushCamera(L, *camera);
		return true;
	}
	case ComponentType::Light:
	{
		const LightComponent* light = entity.TryGetComponent<LightComponent>();
		if (light == nullptr)
		{
			break;
		}
		pushLight(L, *light);
		return true;
	}
	case ComponentType::MeshRenderer:
	{
		const MeshRendererComponent* renderer = entity.TryGetComponent<MeshRendererComponent>();
		if (renderer == nullptr)
		{
			break;
		}
		pushMeshRenderer(L, *renderer);
		return true;
	}
	case ComponentType::RigidBody:
	{
		const RigidBodyComponent* body = entity.TryGetComponent<RigidBodyComponent>();
		if (body == nullptr)
		{
			break;
		}
		pushRigidBody(L, *body);
		return true;
	}
	case ComponentType::Collider:
	{
		const ColliderComponent* collider = entity.TryGetComponent<ColliderComponent>();
		if (collider == nullptr)
		{
			break;
		}
		pushCollider(L, *collider);
		return true;
	}
	case ComponentType::AudioSource:
	{
		const AudioSourceComponent* source = entity.TryGetComponent<AudioSourceComponent>();
		if (source == nullptr)
		{
			break;
		}
		pushAudioSource(L, *source);
		return true;
	}
	case ComponentType::AudioListener:
	{
		const AudioListenerComponent* listener = entity.TryGetComponent<AudioListenerComponent>();
		if (listener == nullptr)
		{
			break;
		}
		pushAudioListener(L, *listener);
		return true;
	}
	case ComponentType::FlyCamera:
	{
		const FlyCameraComponent* flyCamera = entity.TryGetComponent<FlyCameraComponent>();
		if (flyCamera == nullptr)
		{
			break;
		}
		pushFlyCamera(L, *flyCamera);
		return true;
	}
	case ComponentType::LuaScript:
	{
		const LuaScriptComponent* script = entity.TryGetComponent<LuaScriptComponent>();
		if (script == nullptr)
		{
			break;
		}
		pushLuaScript(L, *script);
		return true;
	}
	case ComponentType::None:
		break;
	}
	lua_pushnil(L);
	return false;
}

bool applyComponentTable(lua_State* L, int index, Entity entity, ComponentType type)
{
	switch (type)
	{
	case ComponentType::Camera:
	{
		CameraComponent* camera = entity.TryGetComponent<CameraComponent>();
		if (camera == nullptr)
		{
			return false;
		}
		CameraComponent copy = *camera;
		applyCamera(L, index, copy);
		clampCamera(copy);
		*camera = copy;
		return true;
	}
	case ComponentType::Light:
	{
		LightComponent* light = entity.TryGetComponent<LightComponent>();
		if (light == nullptr)
		{
			return false;
		}
		LightComponent copy = *light;
		applyLight(L, index, copy);
		clampLight(copy);
		*light = copy;
		return true;
	}
	case ComponentType::MeshRenderer:
	{
		MeshRendererComponent* renderer = entity.TryGetComponent<MeshRendererComponent>();
		if (renderer == nullptr)
		{
			return false;
		}
		MeshRendererComponent copy = *renderer;
		applyMeshRenderer(L, index, copy);
		*renderer = copy;
		return true;
	}
	case ComponentType::RigidBody:
	{
		RigidBodyComponent* body = entity.TryGetComponent<RigidBodyComponent>();
		if (body == nullptr)
		{
			return false;
		}
		RigidBodyComponent copy = *body;
		applyRigidBody(L, index, copy);
		clampRigidBody(copy);
		*body = copy;
		return true;
	}
	case ComponentType::Collider:
	{
		ColliderComponent* collider = entity.TryGetComponent<ColliderComponent>();
		if (collider == nullptr)
		{
			return false;
		}
		ColliderComponent copy = *collider;
		applyCollider(L, index, copy);
		clampCollider(copy);
		*collider = copy;
		return true;
	}
	case ComponentType::AudioSource:
	{
		AudioSourceComponent* source = entity.TryGetComponent<AudioSourceComponent>();
		if (source == nullptr)
		{
			return false;
		}
		AudioSourceComponent copy = *source;
		applyAudioSource(L, index, copy);
		clampAudioSource(copy);
		*source = copy;
		return true;
	}
	case ComponentType::AudioListener:
	{
		AudioListenerComponent* listener = entity.TryGetComponent<AudioListenerComponent>();
		if (listener == nullptr)
		{
			return false;
		}
		AudioListenerComponent copy = *listener;
		applyAudioListener(L, index, copy);
		*listener = copy;
		return true;
	}
	case ComponentType::FlyCamera:
	{
		FlyCameraComponent* flyCamera = entity.TryGetComponent<FlyCameraComponent>();
		if (flyCamera == nullptr)
		{
			return false;
		}
		FlyCameraComponent copy = *flyCamera;
		applyFlyCamera(L, index, copy);
		clampFlyCamera(copy);
		*flyCamera = copy;
		return true;
	}
	case ComponentType::LuaScript:
	{
		LuaScriptComponent* script = entity.TryGetComponent<LuaScriptComponent>();
		if (script == nullptr)
		{
			return false;
		}
		LuaScriptComponent copy = *script;
		applyLuaScript(L, index, copy);
		*script = copy;
		return true;
	}
	case ComponentType::None:
		break;
	}
	return false;
}

bool entityAddComponent(Entity entity, ComponentType type)
{
	if (entityHasComponent(entity, type))
	{
		return false;
	}
	switch (type)
	{
	case ComponentType::Camera:
		entity.AddComponent<CameraComponent>();
		return true;
	case ComponentType::Light:
		entity.AddComponent<LightComponent>();
		return true;
	case ComponentType::MeshRenderer:
		entity.AddComponent<MeshRendererComponent>();
		return true;
	case ComponentType::RigidBody:
		entity.AddComponent<RigidBodyComponent>();
		return true;
	case ComponentType::Collider:
		entity.AddComponent<ColliderComponent>();
		return true;
	case ComponentType::AudioSource:
		entity.AddComponent<AudioSourceComponent>();
		return true;
	case ComponentType::AudioListener:
		entity.AddComponent<AudioListenerComponent>();
		return true;
	case ComponentType::FlyCamera:
		entity.AddComponent<FlyCameraComponent>();
		return true;
	case ComponentType::LuaScript:
		entity.AddComponent<LuaScriptComponent>();
		return true;
	case ComponentType::None:
		break;
	}
	return false;
}

bool entityRemoveComponent(Entity entity, ComponentType type)
{
	if (!entityHasComponent(entity, type))
	{
		return false;
	}
	switch (type)
	{
	case ComponentType::Camera:
		entity.RemoveComponent<CameraComponent>();
		return true;
	case ComponentType::Light:
		entity.RemoveComponent<LightComponent>();
		return true;
	case ComponentType::MeshRenderer:
		entity.RemoveComponent<MeshRendererComponent>();
		return true;
	case ComponentType::RigidBody:
		entity.RemoveComponent<RigidBodyComponent>();
		return true;
	case ComponentType::Collider:
		entity.RemoveComponent<ColliderComponent>();
		return true;
	case ComponentType::AudioSource:
		entity.RemoveComponent<AudioSourceComponent>();
		return true;
	case ComponentType::AudioListener:
		entity.RemoveComponent<AudioListenerComponent>();
		return true;
	case ComponentType::FlyCamera:
		entity.RemoveComponent<FlyCameraComponent>();
		return true;
	case ComponentType::LuaScript:
		entity.RemoveComponent<LuaScriptComponent>();
		return true;
	case ComponentType::None:
		break;
	}
	return false;
}

// ------------------------------------------------------------------
// Key names for engine.input.isKeyDown
// ------------------------------------------------------------------

struct KeyNameEntry
{
	std::string_view Name;
	KeyCode Key;
};

constexpr KeyNameEntry c_KeyNames[] = {
	{ "space", KeyCode::Space },
	{ "apostrophe", KeyCode::Apostrophe },
	{ "comma", KeyCode::Comma },
	{ "minus", KeyCode::Minus },
	{ "period", KeyCode::Period },
	{ "slash", KeyCode::Slash },
	{ "0", KeyCode::D0 },
	{ "1", KeyCode::D1 },
	{ "2", KeyCode::D2 },
	{ "3", KeyCode::D3 },
	{ "4", KeyCode::D4 },
	{ "5", KeyCode::D5 },
	{ "6", KeyCode::D6 },
	{ "7", KeyCode::D7 },
	{ "8", KeyCode::D8 },
	{ "9", KeyCode::D9 },
	{ "semicolon", KeyCode::Semicolon },
	{ "equal", KeyCode::Equal },
	{ "a", KeyCode::A },
	{ "b", KeyCode::B },
	{ "c", KeyCode::C },
	{ "d", KeyCode::D },
	{ "e", KeyCode::E },
	{ "f", KeyCode::F },
	{ "g", KeyCode::G },
	{ "h", KeyCode::H },
	{ "i", KeyCode::I },
	{ "j", KeyCode::J },
	{ "k", KeyCode::K },
	{ "l", KeyCode::L },
	{ "m", KeyCode::M },
	{ "n", KeyCode::N },
	{ "o", KeyCode::O },
	{ "p", KeyCode::P },
	{ "q", KeyCode::Q },
	{ "r", KeyCode::R },
	{ "s", KeyCode::S },
	{ "t", KeyCode::T },
	{ "u", KeyCode::U },
	{ "v", KeyCode::V },
	{ "w", KeyCode::W },
	{ "x", KeyCode::X },
	{ "y", KeyCode::Y },
	{ "z", KeyCode::Z },
	{ "leftbracket", KeyCode::LeftBracket },
	{ "backslash", KeyCode::Backslash },
	{ "rightbracket", KeyCode::RightBracket },
	{ "graveaccent", KeyCode::GraveAccent },
	{ "escape", KeyCode::Escape },
	{ "enter", KeyCode::Enter },
	{ "tab", KeyCode::Tab },
	{ "backspace", KeyCode::Backspace },
	{ "insert", KeyCode::Insert },
	{ "delete", KeyCode::Delete },
	{ "right", KeyCode::Right },
	{ "left", KeyCode::Left },
	{ "down", KeyCode::Down },
	{ "up", KeyCode::Up },
	{ "pageup", KeyCode::PageUp },
	{ "pagedown", KeyCode::PageDown },
	{ "home", KeyCode::Home },
	{ "end", KeyCode::End },
	{ "capslock", KeyCode::CapsLock },
	{ "scrolllock", KeyCode::ScrollLock },
	{ "numlock", KeyCode::NumLock },
	{ "printscreen", KeyCode::PrintScreen },
	{ "pause", KeyCode::Pause },
	{ "f1", KeyCode::F1 },
	{ "f2", KeyCode::F2 },
	{ "f3", KeyCode::F3 },
	{ "f4", KeyCode::F4 },
	{ "f5", KeyCode::F5 },
	{ "f6", KeyCode::F6 },
	{ "f7", KeyCode::F7 },
	{ "f8", KeyCode::F8 },
	{ "f9", KeyCode::F9 },
	{ "f10", KeyCode::F10 },
	{ "f11", KeyCode::F11 },
	{ "f12", KeyCode::F12 },
	{ "keypad0", KeyCode::Keypad0 },
	{ "keypad1", KeyCode::Keypad1 },
	{ "keypad2", KeyCode::Keypad2 },
	{ "keypad3", KeyCode::Keypad3 },
	{ "keypad4", KeyCode::Keypad4 },
	{ "keypad5", KeyCode::Keypad5 },
	{ "keypad6", KeyCode::Keypad6 },
	{ "keypad7", KeyCode::Keypad7 },
	{ "keypad8", KeyCode::Keypad8 },
	{ "keypad9", KeyCode::Keypad9 },
	{ "keypaddecimal", KeyCode::KeypadDecimal },
	{ "keypaddivide", KeyCode::KeypadDivide },
	{ "keypadmultiply", KeyCode::KeypadMultiply },
	{ "keypadsubtract", KeyCode::KeypadSubtract },
	{ "keypadadd", KeyCode::KeypadAdd },
	{ "keypadenter", KeyCode::KeypadEnter },
	{ "keypadequal", KeyCode::KeypadEqual },
	{ "leftshift", KeyCode::LeftShift },
	{ "leftcontrol", KeyCode::LeftControl },
	{ "leftalt", KeyCode::LeftAlt },
	{ "leftsuper", KeyCode::LeftSuper },
	{ "rightshift", KeyCode::RightShift },
	{ "rightcontrol", KeyCode::RightControl },
	{ "rightalt", KeyCode::RightAlt },
	{ "rightsuper", KeyCode::RightSuper },
	{ "menu", KeyCode::Menu },
};

bool parseKeyCode(std::string_view name, KeyCode& out)
{
	const std::string normalised = lowerAscii(name);
	for (const KeyNameEntry& entry : c_KeyNames)
	{
		if (normalised == entry.Name)
		{
			out = entry.Key;
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------
// Lua helpers
// ------------------------------------------------------------------

std::string errorText(lua_State* L, int index)
{
	const int absolute = lua_absindex(L, index);
	if (lua_type(L, absolute) == LUA_TSTRING || lua_type(L, absolute) == LUA_TNUMBER)
	{
		size_t length = 0;
		const char* text = lua_tolstring(L, absolute, &length);
		if (text != nullptr)
		{
			return std::string(text, length);
		}
	}
	return std::format("(error object is a {})", luaL_typename(L, absolute));
}

std::string luaValueToText(lua_State* L, int index)
{
	switch (lua_type(L, index))
	{
	case LUA_TNIL:
	case LUA_TNONE:
		return "nil";
	case LUA_TBOOLEAN:
		return lua_toboolean(L, index) != 0 ? "true" : "false";
	case LUA_TNUMBER:
	case LUA_TSTRING:
	{
		size_t length = 0;
		const char* text = lua_tolstring(L, index, &length);
		return text != nullptr ? std::string(text, length) : std::string();
	}
	default:
		return std::format("<{}>", luaL_typename(L, index));
	}
}

int fetchCallback(lua_State* L, int environmentIndex, const char* name)
{
	lua_getfield(L, environmentIndex, name);
	if (lua_isfunction(L, -1) == 0)
	{
		lua_pop(L, 1);
		return LUA_NOREF;
	}
	return luaL_ref(L, LUA_REGISTRYINDEX);
}

// Formats through Lua's string.format, so scripts use the formatting rules
// of the language. A malformed format string falls back to a plain
// concatenation instead of raising from inside a log call.
void logFromLua(lua_State* L, LogLevel level)
{
	int firstArgument = 1;
	if (lua_istable(L, 1) != 0)
	{
		firstArgument = 2; // engine.log:info(...) passes the log table implicitly
	}

	const int lastArgument = lua_gettop(L);
	std::string message;
	bool formatted = false;
	if (lastArgument >= firstArgument)
	{
		lua_getglobal(L, "string");
		if (lua_istable(L, -1) != 0)
		{
			lua_getfield(L, -1, "format");
			if (lua_isfunction(L, -1) != 0)
			{
				lua_remove(L, -2); // drop the string table; string.format is on top
				lua_pushvalue(L, firstArgument);
				for (int argument = firstArgument + 1; argument <= lastArgument; ++argument)
				{
					lua_pushvalue(L, argument);
				}
				const int argumentCount = lastArgument - firstArgument + 1;
				if (lua_pcall(L, argumentCount, 1, 0) == LUA_OK)
				{
					size_t length = 0;
					const char* text = lua_tolstring(L, -1, &length);
					if (text != nullptr)
					{
						message.assign(text, length);
						formatted = true;
					}
					lua_pop(L, 1);
				}
				else
				{
					lua_pop(L, 1); // drop the formatting error
				}
			}
			else
			{
				lua_pop(L, 2);
			}
		}
		else
		{
			lua_pop(L, 1);
		}
	}

	if (!formatted)
	{
		message.clear();
		for (int argument = firstArgument; argument <= lastArgument; ++argument)
		{
			if (argument != firstArgument)
			{
				message.append(" ");
			}
			message.append(luaValueToText(L, argument));
		}
	}

	Log::Write(level, "Lua", message);
}
} // namespace

struct LuaEngine::Impl
{
	// One running or loading script bound to an entity. The Lua function and
	// environment handles are registry references; PropertiesRef points at the
	// instance table exposed as engine.properties.
	struct Instance
	{
		AssetHandle Script;
		// The handle the instance was created from. A scene load reuses entity
		// UUIDs, so a different handle under the same UUID means the entity the
		// script belonged to is gone.
		ecs::EntityHandle Handle{};
		std::filesystem::path Path;
		// Content fingerprint of the last compiled version. Timestamps alone are
		// not reliable on Windows (the last-write clock has a coarse tick), so the
		// poll compares a hash of the file bytes instead.
		u64 LastContentHash = 0;
		bool HasContentHash = false;
		bool Loaded = false;
		bool HasStarted = false;
		bool Disabled = false;
		i32 ConsecutiveErrors = 0;
		bool MissingPathLogged = false;
		bool MissingFileLogged = false;
		int OnStartRef = LUA_NOREF;
		int OnUpdateRef = LUA_NOREF;
		int OnDestroyRef = LUA_NOREF;
		int EnvironmentRef = LUA_NOREF;
		int PropertiesRef = LUA_NOREF;
	};

	// A compiled chunk that has not been adopted by an instance yet, so a
	// failed reload cannot damage the script that is currently running.
	struct PreparedScript
	{
		int EnvironmentRef = LUA_NOREF;
		int OnStartRef = LUA_NOREF;
		int OnUpdateRef = LUA_NOREF;
		int OnDestroyRef = LUA_NOREF;
	};

	enum class CallbackKind : u8
	{
		OnStart,
		OnUpdate,
		OnDestroy
	};

	enum class DispatchResult : u8
	{
		NotCalled,
		Completed,
		Failed,
		Disabled
	};

	lua_State* State = nullptr;
	Scene* OwnerScene = nullptr;
	bool Initialized = false;

	std::filesystem::path ScriptRoot;
	std::unordered_map<AssetHandle, std::filesystem::path, AssetHandleHash> ScriptPaths;
	std::unordered_map<Uuid, Instance, UuidHash> Instances;

	AudioEngine* Audio = nullptr;
	PhysicsWorld* Physics = nullptr;
	std::unordered_map<std::string, AssetHandle> SoundHandles;

	f64 ElapsedTime = 0.0;
	f32 DeltaTime = 0.0f;
	f32 ReloadCheckInterval = LuaEngine::c_DefaultReloadCheckInterval;
	f32 ReloadCheckAccumulator = 0.0f;
	u64 ErrorCount = 0;
	int EngineTableRef = LUA_NOREF;

	bool initialize(Scene& scene);
	void shutdown();
	void update(Scene& scene, Timestep timestep);
	void installBindings();
	void updateTimeFields();
	void retireStaleInstances();
	void updateInstance(const Uuid& id, bool pollReload);

	std::filesystem::path resolveScriptPath(const AssetHandle& script) const;
	bool ensureScript(const Uuid& id, Entity entity, bool pollReload);
	bool
	loadScript(const std::filesystem::path& path, PreparedScript& out, const Uuid& id, std::string_view entityName);
	void releaseScript(Instance& instance);
	void releaseProperties(Instance& instance);
	void releaseInstance(Instance& instance);
	void releasePrepared(PreparedScript& prepared);
	void buildProperties(Instance& instance, const JsonValue& properties);
	void syncProperties(const Uuid& id, int propertiesRef);

	DispatchResult dispatch(const Uuid& id, CallbackKind kind);
	void logCallbackError(const Uuid& id, CallbackKind kind, std::string_view path, std::string_view message);
	void destroyInstance(const Uuid& id, bool callOnDestroy);
	void destroyEntityWithScripts(Entity entity);

	Instance* findInstance(const Uuid& id);
	const Instance* findInstance(const Uuid& id) const;
	PhysicsWorld* resolvePhysicsWorld();

	static Impl& self(lua_State* L);
	static Scene& requireScene(lua_State* L);
	Entity resolveEntityArgument(lua_State* L, int index);

	static int tracebackHandler(lua_State* L);
	static int panicHandler(lua_State* L);

	static int l_scene_index(lua_State* L);
	static int l_scene_newindex(lua_State* L);

	static int l_engine_createEntity(lua_State* L);
	static int l_engine_destroyEntity(lua_State* L);
	static int l_engine_findEntity(lua_State* L);
	static int l_engine_findEntityByName(lua_State* L);
	static int l_engine_entities(lua_State* L);
	static int l_engine_playSound(lua_State* L);

	static int l_input_isKeyDown(lua_State* L);
	static int l_input_mousePosition(lua_State* L);

	static int l_log_info(lua_State* L);
	static int l_log_warn(lua_State* L);
	static int l_log_error(lua_State* L);

	static int l_physics_raycast(lua_State* L);

	static int l_entity_uuid(lua_State* L);
	static int l_entity_name(lua_State* L);
	static int l_entity_setName(lua_State* L);
	static int l_entity_isValid(lua_State* L);
	static int l_entity_isActive(lua_State* L);
	static int l_entity_setActive(lua_State* L);
	static int l_entity_destroy(lua_State* L);
	static int l_entity_position(lua_State* L);
	static int l_entity_setPosition(lua_State* L);
	static int l_entity_rotation(lua_State* L);
	static int l_entity_setRotation(lua_State* L);
	static int l_entity_scale(lua_State* L);
	static int l_entity_setScale(lua_State* L);
	static int l_entity_worldPosition(lua_State* L);
	static int l_entity_parent(lua_State* L);
	static int l_entity_children(lua_State* L);
	static int l_entity_getComponent(lua_State* L);
	static int l_entity_setComponent(lua_State* L);
	static int l_entity_hasComponent(lua_State* L);
	static int l_entity_addComponent(lua_State* L);
	static int l_entity_removeComponent(lua_State* L);
	static int l_entity_tostring(lua_State* L);
};

LuaEngine::Impl& LuaEngine::Impl::self(lua_State* L)
{
	auto** pointer = static_cast<Impl**>(lua_getextraspace(L));
	if (*pointer == nullptr)
	{
		luaL_error(L, "the Aegis scripting state is not attached to this Lua state");
		std::abort(); // luaL_error never returns; keep the compiler happy
	}
	return **pointer;
}

Scene& LuaEngine::Impl::requireScene(lua_State* L)
{
	Impl& engine = self(L);
	if (engine.OwnerScene == nullptr)
	{
		luaL_error(L, "the scripting engine is not bound to a scene");
		std::abort();
	}
	return *engine.OwnerScene;
}

Entity LuaEngine::Impl::resolveEntityArgument(lua_State* L, int index)
{
	const EntityUserdata* userdata = checkEntityUserdata(L, index);
	const Uuid id = userdata->Id;
	if (OwnerScene == nullptr)
	{
		return Entity();
	}
	return OwnerScene->FindEntity(id);
}

int LuaEngine::Impl::tracebackHandler(lua_State* L)
{
	const char* message = lua_tostring(L, 1);
	if (message == nullptr)
	{
		lua_pushliteral(L, "(non-string error object)");
		return 1;
	}
	luaL_traceback(L, L, message, 1);
	return 1;
}

int LuaEngine::Impl::panicHandler(lua_State* L)
{
	const char* message = lua_tostring(L, -1);
	AE_CORE_CRITICAL("Scripting: unprotected Lua error: {}",
	                 message != nullptr ? std::string_view(message) : std::string_view("(non-string error object)"));
	std::abort();
}

bool LuaEngine::Impl::initialize(Scene& scene)
{
	if (Initialized)
	{
		if (OwnerScene == &scene)
		{
			return true;
		}
		shutdown();
	}

	State = luaL_newstate();
	if (State == nullptr)
	{
		AE_CORE_ERROR("Scripting: failed to create a Lua state");
		return false;
	}

	*static_cast<Impl**>(lua_getextraspace(State)) = this;
	lua_atpanic(State, &Impl::panicHandler);
	luaL_openlibs(State);
	installBindings();

	OwnerScene = &scene;
	DeltaTime = 0.0f;
	ElapsedTime = 0.0;
	ReloadCheckAccumulator = 0.0f;
	ErrorCount = 0;
	Initialized = true;

	AE_CORE_INFO("Scripting: {} initialized", LUA_RELEASE);
	return true;
}

void LuaEngine::Impl::shutdown()
{
	if (State == nullptr)
	{
		Instances.clear();
		Initialized = false;
		OwnerScene = nullptr;
		return;
	}

	// Give every running script its OnDestroy while the scene is still alive.
	std::vector<Uuid> ids;
	ids.reserve(Instances.size());
	for (const auto& entry : Instances)
	{
		ids.push_back(entry.first);
	}
	for (const Uuid& id : ids)
	{
		destroyInstance(id, true);
	}
	Instances.clear();

	luaL_unref(State, LUA_REGISTRYINDEX, EngineTableRef);
	EngineTableRef = LUA_NOREF;
	SoundHandles.clear();
	*static_cast<Impl**>(lua_getextraspace(State)) = nullptr;
	lua_close(State);
	State = nullptr;
	OwnerScene = nullptr;
	Initialized = false;

	AE_CORE_INFO("Scripting: Lua state shut down");
}

void LuaEngine::Impl::update(Scene& scene, Timestep timestep)
{
	if (!Initialized || State == nullptr)
	{
		return;
	}
	if (&scene != OwnerScene)
	{
		AE_CORE_WARN("Scripting: OnUpdate received a scene the engine was not initialized with; rebinding");
		OwnerScene = &scene;
	}

	DeltaTime = timestep.GetSeconds();
	ElapsedTime += static_cast<f64>(DeltaTime);
	updateTimeFields();

	// Removals that did not go through a script are noticed here, one update
	// later; script-driven destruction runs OnDestroy immediately.
	retireStaleInstances();

	ReloadCheckAccumulator += Max(DeltaTime, 0.0f);
	const bool checkReload = ReloadCheckInterval <= 0.0f || ReloadCheckAccumulator >= ReloadCheckInterval;
	if (checkReload)
	{
		ReloadCheckAccumulator = 0.0f;
	}

	// Snapshot the UUIDs first: callbacks may spawn or destroy entities, and the
	// registry forbids structural changes while a component pool is iterated.
	std::vector<Uuid> scripted;
	scripted.reserve(Instances.size() + 4);
	scene.GetRegistry().Each<LuaScriptComponent>(
	    [&](ecs::EntityHandle handle, LuaScriptComponent&)
	    {
		    const Entity entity(handle, &scene);
		    if (entity.IsValid())
		    {
			    scripted.push_back(entity.GetUuid());
		    }
	    });

	for (const Uuid& id : scripted)
	{
		Entity entity = scene.FindEntity(id);
		if (!entity.IsValid() || !entity.HasComponent<LuaScriptComponent>())
		{
			continue;
		}
		const LuaScriptComponent& component = entity.GetComponent<LuaScriptComponent>();
		if (!component.Enabled)
		{
			destroyInstance(id, true);
			continue;
		}
		updateInstance(id, checkReload);
	}
}

void LuaEngine::Impl::updateTimeFields()
{
	if (State == nullptr || EngineTableRef == LUA_NOREF)
	{
		return;
	}
	const int base = lua_gettop(State);
	lua_rawgeti(State, LUA_REGISTRYINDEX, EngineTableRef);
	lua_getfield(State, -1, "time");
	if (lua_istable(State, -1) != 0)
	{
		lua_pushnumber(State, static_cast<lua_Number>(DeltaTime));
		lua_setfield(State, -2, "delta");
		lua_pushnumber(State, static_cast<lua_Number>(ElapsedTime));
		lua_setfield(State, -2, "elapsed");
	}
	lua_settop(State, base);
}

void LuaEngine::Impl::retireStaleInstances()
{
	if (Instances.empty() || OwnerScene == nullptr)
	{
		return;
	}

	std::vector<Uuid> stale;
	stale.reserve(Instances.size());
	for (const auto& entry : Instances)
	{
		const Entity entity = OwnerScene->FindEntity(entry.first);
		if (!entity.IsValid() || !entity.HasComponent<LuaScriptComponent>())
		{
			stale.push_back(entry.first);
		}
	}
	for (const Uuid& id : stale)
	{
		const Instance* instance = findInstance(id);
		if (instance == nullptr)
		{
			continue;
		}
		const Entity entity = OwnerScene->FindEntity(id);
		const bool reused = entity.IsValid() && entity.GetHandle() != instance->Handle;
		destroyInstance(id, !reused);
	}
}

LuaEngine::Impl::Instance* LuaEngine::Impl::findInstance(const Uuid& id)
{
	const auto it = Instances.find(id);
	return it == Instances.end() ? nullptr : &it->second;
}

const LuaEngine::Impl::Instance* LuaEngine::Impl::findInstance(const Uuid& id) const
{
	const auto it = Instances.find(id);
	return it == Instances.end() ? nullptr : &it->second;
}

std::filesystem::path LuaEngine::Impl::resolveScriptPath(const AssetHandle& script) const
{
	if (!script.IsValid())
	{
		return {};
	}
	const auto registered = ScriptPaths.find(script);
	if (registered != ScriptPaths.end())
	{
		return registered->second;
	}
	if (ScriptRoot.empty())
	{
		return {};
	}
	// Project convention: scripts are stored next to the project under the
	// asset id, so a scene can reference a script without a registration step.
	return ScriptRoot / (script.Id.ToString() + ".lua");
}

void LuaEngine::Impl::releaseScript(Instance& instance)
{
	if (State != nullptr)
	{
		luaL_unref(State, LUA_REGISTRYINDEX, instance.OnStartRef);
		luaL_unref(State, LUA_REGISTRYINDEX, instance.OnUpdateRef);
		luaL_unref(State, LUA_REGISTRYINDEX, instance.OnDestroyRef);
		luaL_unref(State, LUA_REGISTRYINDEX, instance.EnvironmentRef);
	}
	instance.OnStartRef = LUA_NOREF;
	instance.OnUpdateRef = LUA_NOREF;
	instance.OnDestroyRef = LUA_NOREF;
	instance.EnvironmentRef = LUA_NOREF;
	instance.Loaded = false;
}

void LuaEngine::Impl::releaseProperties(Instance& instance)
{
	if (State != nullptr)
	{
		luaL_unref(State, LUA_REGISTRYINDEX, instance.PropertiesRef);
	}
	instance.PropertiesRef = LUA_NOREF;
}

void LuaEngine::Impl::releaseInstance(Instance& instance)
{
	releaseScript(instance);
	releaseProperties(instance);
}

void LuaEngine::Impl::releasePrepared(PreparedScript& prepared)
{
	if (State != nullptr)
	{
		luaL_unref(State, LUA_REGISTRYINDEX, prepared.EnvironmentRef);
		luaL_unref(State, LUA_REGISTRYINDEX, prepared.OnStartRef);
		luaL_unref(State, LUA_REGISTRYINDEX, prepared.OnUpdateRef);
		luaL_unref(State, LUA_REGISTRYINDEX, prepared.OnDestroyRef);
	}
	prepared = PreparedScript();
}

void LuaEngine::Impl::buildProperties(Instance& instance, const JsonValue& properties)
{
	releaseProperties(instance);
	if (State == nullptr)
	{
		return;
	}
	pushJsonValue(State, properties.IsObject() ? properties : JsonValue::MakeObject());
	instance.PropertiesRef = luaL_ref(State, LUA_REGISTRYINDEX);
}

void LuaEngine::Impl::syncProperties(const Uuid& id, int propertiesRef)
{
	if (State == nullptr || propertiesRef == LUA_NOREF || OwnerScene == nullptr)
	{
		return;
	}
	Instance* instance = findInstance(id);
	if (instance == nullptr || instance->PropertiesRef != propertiesRef)
	{
		// The instance was replaced or destroyed while the callback ran.
		return;
	}
	Entity entity = OwnerScene->FindEntity(id);
	LuaScriptComponent* component = entity.TryGetComponent<LuaScriptComponent>();
	if (component == nullptr)
	{
		return;
	}

	const int base = lua_gettop(State);
	lua_rawgeti(State, LUA_REGISTRYINDEX, propertiesRef);
	JsonValue properties = readJsonValue(State, base + 1);
	lua_settop(State, base);

	// The component contract stores a JSON object; an array-valued table is
	// dropped rather than written as a document the serializer cannot load.
	if (properties.IsObject())
	{
		component->Properties = std::move(properties);
	}
}

bool LuaEngine::Impl::loadScript(const std::filesystem::path& path,
                                 PreparedScript& out,
                                 const Uuid& id,
                                 std::string_view entityName)
{
	if (State == nullptr)
	{
		return false;
	}

	std::string source;
	if (!FileSystem::TryReadTextFile(path, source))
	{
		AE_CORE_ERROR("Scripting: cannot read script '{}' for entity '{}' ({})",
		              FileSystem::ToUtf8(path),
		              entityName,
		              id.ToString());
		return false;
	}

	// Lua does not skip the UTF-8 byte order mark that Windows editors like to
	// write; strip it before handing the buffer over.
	if (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
	    static_cast<unsigned char>(source[1]) == 0xBB && static_cast<unsigned char>(source[2]) == 0xBF)
	{
		source.erase(0, 3);
	}

	const std::string pathText = FileSystem::ToUtf8(path);
	const std::string chunkName = "@" + pathText;
	const int base = lua_gettop(State);

	if (luaL_loadbufferx(State, source.data(), source.size(), chunkName.c_str(), nullptr) != LUA_OK)
	{
		const std::string message = errorText(State, -1);
		lua_settop(State, base);
		AE_CORE_ERROR("Scripting: entity '{}' ({}) failed to compile '{}':\n{}",
		              entityName,
		              id.ToString(),
		              pathText,
		              message);
		return false;
	}

	// A fresh environment per instance keeps two entities that run the same
	// file from sharing globals, while lookups fall through to the real globals.
	lua_newtable(State);        // base+2 environment
	lua_newtable(State);        // base+3 metatable
	lua_pushglobaltable(State); // base+4 _G
	lua_setfield(State, base + 3, "__index");
	lua_setmetatable(State, base + 2);

	lua_pushvalue(State, base + 2);
	out.EnvironmentRef = luaL_ref(State, LUA_REGISTRYINDEX);
	lua_pushvalue(State, base + 2);
	if (lua_setupvalue(State, base + 1, 1) == nullptr)
	{
		lua_settop(State, base);
		releasePrepared(out);
		AE_CORE_ERROR("Scripting: entity '{}' ({}) script '{}' has no _ENV upvalue",
		              entityName,
		              id.ToString(),
		              pathText);
		return false;
	}

	// The environment sits above the chunk, so lift a copy of the chunk over it
	// before lua_pcall: pcall calls the value on top of the stack.
	lua_pushvalue(State, base + 1);
	if (lua_pcall(State, 0, 0, 0) != LUA_OK)
	{
		const std::string message = errorText(State, -1);
		lua_settop(State, base);
		releasePrepared(out);
		AE_CORE_ERROR("Scripting: entity '{}' ({}) top-level error in '{}':\n{}",
		              entityName,
		              id.ToString(),
		              pathText,
		              message);
		return false;
	}

	const int environmentIndex = base + 2;
	out.OnStartRef = fetchCallback(State, environmentIndex, "OnStart");
	out.OnUpdateRef = fetchCallback(State, environmentIndex, "OnUpdate");
	out.OnDestroyRef = fetchCallback(State, environmentIndex, "OnDestroy");
	lua_settop(State, base);
	return true;
}

	namespace
	{
		// FNV-1a over the script bytes. Used instead of the file timestamp because
		// Windows updates the last-write clock on a coarse tick, which made two
		// quick saves look identical and silently skip the reload.
		bool computeFileHash(const FileSystem::Path& path, u64& outHash)
		{
			std::vector<u8> bytes;
			if (!FileSystem::TryReadBinaryFile(path, bytes))
			{
				return false;
			}
			u64 hash = 1469598103934665603ull;
			for (const u8 byte : bytes)
			{
				hash ^= byte;
				hash *= 1099511628211ull;
			}
			outHash = hash;
			return true;
		}
	}

	bool LuaEngine::Impl::ensureScript(const Uuid& id, Entity entity, bool pollReload)
{
	Instance* instance = findInstance(id);
	if (instance != nullptr && instance->Handle != entity.GetHandle())
	{
		// The UUID now belongs to a different entity (a scene load reuses UUIDs).
		// The old chunk is dropped without OnDestroy: the entity value the
		// callback would receive already points at the new entity.
		releaseInstance(*instance);
		Instances.erase(id);
		instance = nullptr;
	}
	if (instance == nullptr)
	{
		instance = &Instances.try_emplace(id).first->second;
		instance->Handle = entity.GetHandle();
	}

	const LuaScriptComponent* scriptComponent = entity.TryGetComponent<LuaScriptComponent>();
	if (scriptComponent == nullptr)
	{
		return false;
	}
	const std::filesystem::path resolved = resolveScriptPath(scriptComponent->Script);
	if (!resolved.empty() && resolved != instance->Path)
	{
		instance->Path = resolved;
		instance->HasContentHash = false;
		instance->MissingPathLogged = false;
		instance->MissingFileLogged = false;
	}
	if (instance->Path.empty())
	{
		if (!instance->MissingPathLogged)
		{
			instance->MissingPathLogged = true;
			AE_CORE_ERROR("Scripting: entity {} has no script path; register the asset with "
			              "LuaEngine::RegisterScript or set a script root",
			              describeEntity(OwnerScene, id));
		}
		return false;
	}

	if (!pollReload && instance->HasContentHash)
	{
		return instance->Loaded;
	}

	const std::string pathText = FileSystem::ToUtf8(instance->Path);
	u64 contentHash = 0;
	if (!computeFileHash(instance->Path, contentHash))
	{
		const bool wasLoaded = instance->Loaded;
		if (!instance->MissingFileLogged)
		{
			instance->MissingFileLogged = true;
			AE_CORE_ERROR("Scripting: entity {} script '{}' does not exist", describeEntity(OwnerScene, id), pathText);
		}
		instance->HasContentHash = false;
		// A script whose file vanished keeps running the chunk it already has;
		// deleting a file must not silently disable a live entity.
		return wasLoaded;
	}
	instance->MissingFileLogged = false;

	if (instance->HasContentHash && contentHash == instance->LastContentHash)
	{
		return instance->Loaded;
	}

	// Compile the new chunk before touching the running one: a syntax error
	// typed into the editor must not take down a script that works.
	PreparedScript prepared;
	if (!loadScript(instance->Path, prepared, id, entityNameFor(OwnerScene, id)))
	{
		instance->LastContentHash = contentHash;
		instance->HasContentHash = true;
		return instance->Loaded;
	}

	const bool replacingChunk = instance->Loaded;
	if (replacingChunk && instance->HasStarted)
	{
		dispatch(id, CallbackKind::OnDestroy);
	}
	instance = findInstance(id);
	if (instance == nullptr)
	{
		// OnDestroy destroyed the entity; the prepared chunk has no home.
		releasePrepared(prepared);
		return false;
	}

	if (replacingChunk)
	{
		AE_CORE_INFO("Scripting: reloaded '{}' for entity {}", pathText, describeEntity(OwnerScene, id));
	}

	releaseScript(*instance);
	instance->EnvironmentRef = prepared.EnvironmentRef;
	instance->OnStartRef = prepared.OnStartRef;
	instance->OnUpdateRef = prepared.OnUpdateRef;
	instance->OnDestroyRef = prepared.OnDestroyRef;
	prepared = PreparedScript();
	instance->Loaded = true;
	instance->HasStarted = false;
	instance->Disabled = false;
	instance->ConsecutiveErrors = 0;
	instance->LastContentHash = contentHash;
	instance->HasContentHash = true;

	// Properties survive a reload: the table is rebuilt from the JSON the
	// previous callbacks synced into the component.
	releaseProperties(*instance);
	const Entity current = OwnerScene->FindEntity(id);
	const LuaScriptComponent* component = current.TryGetComponent<LuaScriptComponent>();
	buildProperties(*instance, component != nullptr ? component->Properties : JsonValue::MakeObject());
	return true;
}

void LuaEngine::Impl::updateInstance(const Uuid& id, bool pollReload)
{
	Entity entity = OwnerScene != nullptr ? OwnerScene->FindEntity(id) : Entity();
	if (!entity.IsValid())
	{
		return;
	}
	const LuaScriptComponent* component = entity.TryGetComponent<LuaScriptComponent>();
	if (component == nullptr || !component->Enabled)
	{
		return;
	}

	if (!ensureScript(id, entity, pollReload))
	{
		return;
	}

	Instance* instance = findInstance(id);
	if (instance == nullptr || instance->Disabled)
	{
		return;
	}
	if (!instance->HasStarted)
	{
		// Set before the call so a throwing OnStart is not retried every frame.
		instance->HasStarted = true;
		const DispatchResult result = dispatch(id, CallbackKind::OnStart);
		if (result == DispatchResult::Failed || result == DispatchResult::Disabled)
		{
			return;
		}
	}

	if (OwnerScene->FindEntity(id).IsValid())
	{
		dispatch(id, CallbackKind::OnUpdate);
	}
}

LuaEngine::Impl::DispatchResult LuaEngine::Impl::dispatch(const Uuid& id, CallbackKind kind)
{
	if (State == nullptr)
	{
		return DispatchResult::NotCalled;
	}
	Instance* instance = findInstance(id);
	if (instance == nullptr)
	{
		return DispatchResult::NotCalled;
	}

	const int functionRef = kind == CallbackKind::OnStart
	                            ? instance->OnStartRef
	                            : (kind == CallbackKind::OnUpdate ? instance->OnUpdateRef : instance->OnDestroyRef);
	if (functionRef == LUA_NOREF)
	{
		return DispatchResult::NotCalled;
	}
	const int propertiesRef = instance->PropertiesRef;
	const std::string path = FileSystem::ToUtf8(instance->Path);
	const int base = lua_gettop(State);

	// engine.properties is swapped per callback and restored afterwards, so a
	// nested call (OnDestroy from entity:destroy()) sees its own table.
	lua_rawgeti(State, LUA_REGISTRYINDEX, EngineTableRef); // base+1 engine
	lua_getfield(State, base + 1, "properties");           // base+2 previous
	if (propertiesRef != LUA_NOREF)
	{
		lua_rawgeti(State, LUA_REGISTRYINDEX, propertiesRef); // base+3 table
	}
	else
	{
		lua_createtable(State, 0, 0);
	}
	lua_setfield(State, base + 1, "properties");

	lua_pushcfunction(State, &Impl::tracebackHandler); // base+3 message handler
	const int messageHandler = base + 3;
	lua_rawgeti(State, LUA_REGISTRYINDEX, functionRef); // base+4 function
	pushEntityUserdata(State, id);                      // base+5 argument

	int argumentCount = 1;
	if (kind == CallbackKind::OnUpdate)
	{
		lua_pushnumber(State, static_cast<lua_Number>(DeltaTime));
		++argumentCount;
	}

	const int status = lua_pcall(State, argumentCount, 0, messageHandler);
	std::string message;
	if (status != LUA_OK)
	{
		message = errorText(State, -1);
	}

	// Restore the previous properties table before dropping the frame.
	lua_pushvalue(State, base + 2);
	lua_setfield(State, base + 1, "properties");
	lua_settop(State, base);

	if (status == LUA_OK)
	{
		Instance* current = findInstance(id);
		if (current != nullptr)
		{
			current->ConsecutiveErrors = 0;
		}
	}
	else
	{
		++ErrorCount;
		logCallbackError(id, kind, path, message);
		Instance* current = findInstance(id);
		if (current != nullptr)
		{
			++current->ConsecutiveErrors;
			if (current->ConsecutiveErrors >= LuaEngine::c_MaxConsecutiveErrors && !current->Disabled)
			{
				current->Disabled = true;
				AE_CORE_ERROR("Scripting: entity {} script '{}' disabled after {} consecutive errors",
				              describeEntity(OwnerScene, id),
				              path,
				              LuaEngine::c_MaxConsecutiveErrors);
			}
		}
	}

	// Persist whatever the callback left in engine.properties.
	syncProperties(id, propertiesRef);

	Instance* result = findInstance(id);
	if (result == nullptr)
	{
		return DispatchResult::NotCalled;
	}
	if (result->Disabled)
	{
		return DispatchResult::Disabled;
	}
	return status == LUA_OK ? DispatchResult::Completed : DispatchResult::Failed;
}

void LuaEngine::Impl::logCallbackError(const Uuid& id,
                                       CallbackKind kind,
                                       std::string_view path,
                                       std::string_view message)
{
	const char* callback =
	    kind == CallbackKind::OnStart ? "OnStart" : (kind == CallbackKind::OnUpdate ? "OnUpdate" : "OnDestroy");
	AE_CORE_ERROR("Scripting: entity {} {} failed in '{}':\n{}",
	              describeEntity(OwnerScene, id),
	              callback,
	              path,
	              message);
}

void LuaEngine::Impl::destroyInstance(const Uuid& id, bool callOnDestroy)
{
	Instance* instance = findInstance(id);
	if (instance == nullptr)
	{
		return;
	}
	const bool callScript = callOnDestroy && instance->Loaded && instance->HasStarted;
	if (callScript)
	{
		dispatch(id, CallbackKind::OnDestroy);
	}
	instance = findInstance(id);
	if (instance == nullptr)
	{
		return;
	}
	releaseInstance(*instance);
	Instances.erase(id);
}

void LuaEngine::Impl::destroyEntityWithScripts(Entity entity)
{
	if (OwnerScene == nullptr || !entity.IsValid())
	{
		return;
	}

	// Mirror Scene::DestroyEntity's descendant-first order so every script sees
	// its OnDestroy while the entity it belongs to still exists.
	if (const TransformComponent* transform = entity.TryGetComponent<TransformComponent>())
	{
		const std::vector<Uuid> children = transform->Children;
		for (const Uuid& childId : children)
		{
			Entity child = OwnerScene->FindEntity(childId);
			if (child.IsValid())
			{
				destroyEntityWithScripts(child);
			}
		}
	}

	destroyInstance(entity.GetUuid(), true);
	OwnerScene->DestroyEntity(entity);
}

PhysicsWorld* LuaEngine::Impl::resolvePhysicsWorld()
{
	if (Physics != nullptr)
	{
		return Physics;
	}
	if (OwnerScene == nullptr)
	{
		return nullptr;
	}
	// PhysicsWorld is a SceneSystem; the scene owns the instance, so the
	// bindings work without a separate host wiring step.
	for (SceneSystem* system : OwnerScene->GetSystems())
	{
		if (system == nullptr)
		{
			continue;
		}
		if (auto* world = dynamic_cast<PhysicsWorld*>(system))
		{
			return world;
		}
	}
	return nullptr;
}

void LuaEngine::Impl::installBindings()
{
	const int base = lua_gettop(State);

	// Entity userdata: one metatable shared by every entity value.
	luaL_newmetatable(State, c_EntityMetatableName); // base+1
	lua_createtable(State, 0, 21);                   // base+2 methods
	setFunctionField(State, base + 2, "uuid", &Impl::l_entity_uuid);
	setFunctionField(State, base + 2, "name", &Impl::l_entity_name);
	setFunctionField(State, base + 2, "setName", &Impl::l_entity_setName);
	setFunctionField(State, base + 2, "isValid", &Impl::l_entity_isValid);
	setFunctionField(State, base + 2, "isActive", &Impl::l_entity_isActive);
	setFunctionField(State, base + 2, "setActive", &Impl::l_entity_setActive);
	setFunctionField(State, base + 2, "destroy", &Impl::l_entity_destroy);
	setFunctionField(State, base + 2, "position", &Impl::l_entity_position);
	setFunctionField(State, base + 2, "setPosition", &Impl::l_entity_setPosition);
	setFunctionField(State, base + 2, "rotation", &Impl::l_entity_rotation);
	setFunctionField(State, base + 2, "setRotation", &Impl::l_entity_setRotation);
	setFunctionField(State, base + 2, "scale", &Impl::l_entity_scale);
	setFunctionField(State, base + 2, "setScale", &Impl::l_entity_setScale);
	setFunctionField(State, base + 2, "worldPosition", &Impl::l_entity_worldPosition);
	setFunctionField(State, base + 2, "parent", &Impl::l_entity_parent);
	setFunctionField(State, base + 2, "children", &Impl::l_entity_children);
	setFunctionField(State, base + 2, "getComponent", &Impl::l_entity_getComponent);
	setFunctionField(State, base + 2, "setComponent", &Impl::l_entity_setComponent);
	setFunctionField(State, base + 2, "hasComponent", &Impl::l_entity_hasComponent);
	setFunctionField(State, base + 2, "addComponent", &Impl::l_entity_addComponent);
	setFunctionField(State, base + 2, "removeComponent", &Impl::l_entity_removeComponent);
	lua_setfield(State, base + 1, "__index");
	setFunctionField(State, base + 1, "__tostring", &Impl::l_entity_tostring);
	lua_pop(State, 1);

	// The global engine table.
	lua_createtable(State, 0, 12); // base+1
	setFunctionField(State, base + 1, "createEntity", &Impl::l_engine_createEntity);
	setFunctionField(State, base + 1, "destroyEntity", &Impl::l_engine_destroyEntity);
	setFunctionField(State, base + 1, "findEntity", &Impl::l_engine_findEntity);
	setFunctionField(State, base + 1, "findEntityByName", &Impl::l_engine_findEntityByName);
	setFunctionField(State, base + 1, "entities", &Impl::l_engine_entities);
	setFunctionField(State, base + 1, "playSound", &Impl::l_engine_playSound);

	// scene is a proxy table so engine.scene.name reads and writes the Scene.
	lua_newtable(State);          // base+2 proxy
	lua_createtable(State, 0, 2); // base+3 metatable
	setFunctionField(State, base + 3, "__index", &Impl::l_scene_index);
	setFunctionField(State, base + 3, "__newindex", &Impl::l_scene_newindex);
	lua_setmetatable(State, base + 2);
	lua_setfield(State, base + 1, "scene");

	lua_createtable(State, 0, 2); // base+2 time
	setNumberField(State, base + 2, "delta", 0.0);
	setNumberField(State, base + 2, "elapsed", 0.0);
	lua_setfield(State, base + 1, "time");

	lua_createtable(State, 0, 2); // base+2 input
	setFunctionField(State, base + 2, "isKeyDown", &Impl::l_input_isKeyDown);
	setFunctionField(State, base + 2, "mousePosition", &Impl::l_input_mousePosition);
	lua_setfield(State, base + 1, "input");

	lua_createtable(State, 0, 3); // base+2 log
	setFunctionField(State, base + 2, "info", &Impl::l_log_info);
	setFunctionField(State, base + 2, "warn", &Impl::l_log_warn);
	setFunctionField(State, base + 2, "error", &Impl::l_log_error);
	lua_setfield(State, base + 1, "log");

	lua_createtable(State, 0, 1); // base+2 physics
	setFunctionField(State, base + 2, "raycast", &Impl::l_physics_raycast);
	lua_setfield(State, base + 1, "physics");

	// Placeholder so engine.properties is a table before the first callback.
	lua_newtable(State);
	lua_setfield(State, base + 1, "properties");

	lua_pushvalue(State, base + 1);
	lua_setglobal(State, "engine");
	EngineTableRef = luaL_ref(State, LUA_REGISTRYINDEX);
	lua_settop(State, base);
}

// ----------------------------------------------------------------------
// engine.scene
// ----------------------------------------------------------------------

int LuaEngine::Impl::l_scene_index(lua_State* L)
{
	const char* key = luaL_checkstring(L, 2);
	if (std::string_view(key) == "name")
	{
		const std::string& name = requireScene(L).GetName();
		lua_pushlstring(L, name.data(), name.size());
		return 1;
	}
	lua_pushnil(L);
	return 1;
}

int LuaEngine::Impl::l_scene_newindex(lua_State* L)
{
	const char* key = luaL_checkstring(L, 2);
	if (std::string_view(key) == "name")
	{
		const char* value = luaL_checkstring(L, 3);
		requireScene(L).SetName(value);
		return 0;
	}
	AE_CORE_WARN("Scripting: engine.scene has no writable field '{}'", key);
	return 0;
}

// ----------------------------------------------------------------------
// engine.*
// ----------------------------------------------------------------------

int LuaEngine::Impl::l_engine_createEntity(lua_State* L)
{
	Scene& scene = requireScene(L);
	const char* name = luaL_optstring(L, 1, "Entity");
	const Entity entity = scene.CreateEntity(name);
	pushEntityUserdata(L, entity.GetUuid());
	return 1;
}

int LuaEngine::Impl::l_engine_destroyEntity(lua_State* L)
{
	Impl& engine = self(L);
	const Entity entity = engine.resolveEntityArgument(L, 1);
	if (!entity.IsValid())
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	engine.destroyEntityWithScripts(entity);
	lua_pushboolean(L, 1);
	return 1;
}

int LuaEngine::Impl::l_engine_findEntity(lua_State* L)
{
	Scene& scene = requireScene(L);
	size_t length = 0;
	const char* text = luaL_checklstring(L, 1, &length);
	const Uuid uuid = Uuid::FromString(std::string_view(text, length));
	if (uuid.IsNull())
	{
		lua_pushnil(L);
		return 1;
	}
	const Entity entity = scene.FindEntity(uuid);
	if (!entity.IsValid())
	{
		lua_pushnil(L);
		return 1;
	}
	pushEntityUserdata(L, uuid);
	return 1;
}

int LuaEngine::Impl::l_engine_findEntityByName(lua_State* L)
{
	Scene& scene = requireScene(L);
	const char* name = luaL_checkstring(L, 1);
	const Entity entity = scene.FindEntity(std::string_view(name));
	if (!entity.IsValid())
	{
		lua_pushnil(L);
		return 1;
	}
	pushEntityUserdata(L, entity.GetUuid());
	return 1;
}

int LuaEngine::Impl::l_engine_entities(lua_State* L)
{
	Scene& scene = requireScene(L);
	const std::vector<Entity> entities = scene.GetEntities();
	lua_createtable(L, static_cast<int>(entities.size()), 0);
	int position = 1;
	for (const Entity& entity : entities)
	{
		pushEntityUserdata(L, entity.GetUuid());
		lua_rawseti(L, -2, static_cast<lua_Integer>(position));
		++position;
	}
	return 1;
}

int LuaEngine::Impl::l_engine_playSound(lua_State* L)
{
	Impl& engine = self(L);
	if (engine.Audio == nullptr)
	{
		lua_pushboolean(L, 0);
		return 1;
	}

	size_t length = 0;
	const char* rawPath = luaL_checklstring(L, 1, &length);
	const auto rawVolume = static_cast<f32>(luaL_optnumber(L, 2, 1.0));
	const std::string path(rawPath, length);

	auto found = engine.SoundHandles.find(path);
	if (found == engine.SoundHandles.end())
	{
		AssetHandle handle;
		handle.Id = Uuid::Generate();
		handle.Type = AssetType::Audio;
		found = engine.SoundHandles.emplace(path, handle).first;
	}

	const AssetHandle clip = found->second;
	if (!engine.Audio->IsClipRegistered(clip))
	{
		if (!engine.Audio->RegisterClip(clip, FileSystem::FromUtf8(path)))
		{
			AE_CORE_WARN("Scripting: engine.playSound could not load '{}'", path);
			lua_pushboolean(L, 0);
			return 1;
		}
	}
	lua_pushboolean(L, engine.Audio->Play(clip, rawVolume) ? 1 : 0);
	return 1;
}

// ----------------------------------------------------------------------
// engine.input
// ----------------------------------------------------------------------

int LuaEngine::Impl::l_input_isKeyDown(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);
	KeyCode key = KeyCode::Unknown;
	if (!parseKeyCode(name, key))
	{
		return luaL_error(L, "engine.input.isKeyDown: unknown key name '%s'", name);
	}
	lua_pushboolean(L, Input::IsKeyPressed(key) ? 1 : 0);
	return 1;
}

int LuaEngine::Impl::l_input_mousePosition(lua_State* L)
{
	const std::pair<f32, f32> position = Input::GetMousePosition();
	lua_createtable(L, 0, 2);
	setNumberField(L, -1, "x", position.first);
	setNumberField(L, -1, "y", position.second);
	return 1;
}

// ----------------------------------------------------------------------
// engine.log
// ----------------------------------------------------------------------

int LuaEngine::Impl::l_log_info(lua_State* L)
{
	logFromLua(L, LogLevel::Info);
	return 0;
}

int LuaEngine::Impl::l_log_warn(lua_State* L)
{
	logFromLua(L, LogLevel::Warn);
	return 0;
}

int LuaEngine::Impl::l_log_error(lua_State* L)
{
	logFromLua(L, LogLevel::Error);
	return 0;
}

// ----------------------------------------------------------------------
// engine.physics
// ----------------------------------------------------------------------

int LuaEngine::Impl::l_physics_raycast(lua_State* L)
{
	Impl& engine = self(L);
	glm::vec3 origin(0.0f);
	glm::vec3 direction(0.0f);
	if (!readVector3(L, 1, origin))
	{
		return luaL_error(L, "engine.physics.raycast expects an origin table {x=,y=,z=}");
	}
	if (!readVector3(L, 2, direction))
	{
		return luaL_error(L, "engine.physics.raycast expects a direction table {x=,y=,z=}");
	}
	const auto maxDistance = static_cast<f32>(luaL_checknumber(L, 3));

	Scene* scene = engine.OwnerScene;
	PhysicsWorld* world = engine.resolvePhysicsWorld();
	if (world == nullptr || scene == nullptr)
	{
		lua_pushnil(L);
		return 1;
	}

	Uuid hitEntity;
	glm::vec3 hitPoint(0.0f);
	if (!world->Raycast(*scene, origin, direction, maxDistance, hitEntity, hitPoint))
	{
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, 0, 2);
	const std::string uuid = hitEntity.ToString();
	lua_pushlstring(L, uuid.data(), uuid.size());
	lua_setfield(L, -2, "entityUuid");
	pushVector3(L, hitPoint);
	lua_setfield(L, -2, "point");
	return 1;
}

// ----------------------------------------------------------------------
// entity userdata methods
// ----------------------------------------------------------------------

int LuaEngine::Impl::l_entity_uuid(lua_State* L)
{
	const EntityUserdata* userdata = checkEntityUserdata(L, 1);
	const std::string uuid = userdata->Id.ToString();
	lua_pushlstring(L, uuid.data(), uuid.size());
	return 1;
}

int LuaEngine::Impl::l_entity_name(lua_State* L)
{
	const Entity entity = self(L).resolveEntityArgument(L, 1);
	const std::string& name = entity.GetName();
	lua_pushlstring(L, name.data(), name.size());
	return 1;
}

int LuaEngine::Impl::l_entity_setName(lua_State* L)
{
	Impl& engine = self(L);
	const char* name = luaL_checkstring(L, 2);
	Entity entity = engine.resolveEntityArgument(L, 1);
	if (!entity.IsValid())
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	entity.SetName(name);
	lua_pushboolean(L, 1);
	return 1;
}

int LuaEngine::Impl::l_entity_isValid(lua_State* L)
{
	lua_pushboolean(L, self(L).resolveEntityArgument(L, 1).IsValid() ? 1 : 0);
	return 1;
}

int LuaEngine::Impl::l_entity_isActive(lua_State* L)
{
	const Entity entity = self(L).resolveEntityArgument(L, 1);
	lua_pushboolean(L, entity.IsValid() && entity.IsActive() ? 1 : 0);
	return 1;
}

int LuaEngine::Impl::l_entity_setActive(lua_State* L)
{
	if (lua_isboolean(L, 2) == 0)
	{
		return luaL_error(L, "entity:setActive expects a boolean");
	}
	const bool active = lua_toboolean(L, 2) != 0;
	Entity entity = self(L).resolveEntityArgument(L, 1);
	if (!entity.IsValid())
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	entity.SetActive(active);
	lua_pushboolean(L, 1);
	return 1;
}

int LuaEngine::Impl::l_entity_destroy(lua_State* L)
{
	Impl& engine = self(L);
	const Entity entity = engine.resolveEntityArgument(L, 1);
	if (!entity.IsValid())
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	engine.destroyEntityWithScripts(entity);
	lua_pushboolean(L, 1);
	return 1;
}

int LuaEngine::Impl::l_entity_position(lua_State* L)
{
	const Entity entity = self(L).resolveEntityArgument(L, 1);
	const TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
	pushVector3(L, transform != nullptr ? transform->LocalPosition : glm::vec3(0.0f));
	return 1;
}

int LuaEngine::Impl::l_entity_setPosition(lua_State* L)
{
	Impl& engine = self(L);
	glm::vec3 position(0.0f);
	if (!readVector3(L, 2, position))
	{
		return luaL_error(L, "entity:setPosition expects a table {x=,y=,z=}");
	}
	TransformComponent* transform = engine.resolveEntityArgument(L, 1).TryGetComponent<TransformComponent>();
	if (transform == nullptr)
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	transform->LocalPosition = position;
	lua_pushboolean(L, 1);
	return 1;
}

int LuaEngine::Impl::l_entity_rotation(lua_State* L)
{
	const Entity entity = self(L).resolveEntityArgument(L, 1);
	const TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
	pushVector3(L, transform != nullptr ? transform->GetLocalEulerAngles() : glm::vec3(0.0f));
	return 1;
}

int LuaEngine::Impl::l_entity_setRotation(lua_State* L)
{
	Impl& engine = self(L);
	glm::vec3 rotation(0.0f);
	if (!readVector3(L, 2, rotation))
	{
		return luaL_error(L, "entity:setRotation expects euler angles in degrees {x=,y=,z=}");
	}
	TransformComponent* transform = engine.resolveEntityArgument(L, 1).TryGetComponent<TransformComponent>();
	if (transform == nullptr)
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	transform->SetLocalEulerAngles(rotation);
	lua_pushboolean(L, 1);
	return 1;
}

int LuaEngine::Impl::l_entity_scale(lua_State* L)
{
	const Entity entity = self(L).resolveEntityArgument(L, 1);
	const TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
	pushVector3(L, transform != nullptr ? transform->LocalScale : glm::vec3(1.0f));
	return 1;
}

int LuaEngine::Impl::l_entity_setScale(lua_State* L)
{
	Impl& engine = self(L);
	glm::vec3 scale(1.0f);
	if (!readVector3(L, 2, scale))
	{
		return luaL_error(L, "entity:setScale expects a table {x=,y=,z=}");
	}
	TransformComponent* transform = engine.resolveEntityArgument(L, 1).TryGetComponent<TransformComponent>();
	if (transform == nullptr)
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	transform->LocalScale = scale;
	lua_pushboolean(L, 1);
	return 1;
}

int LuaEngine::Impl::l_entity_worldPosition(lua_State* L)
{
	const Entity entity = self(L).resolveEntityArgument(L, 1);
	const TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
	pushVector3(L, transform != nullptr ? transform->WorldPosition : glm::vec3(0.0f));
	return 1;
}

int LuaEngine::Impl::l_entity_parent(lua_State* L)
{
	Impl& engine = self(L);
	Entity entity = engine.resolveEntityArgument(L, 1);
	if (!entity.IsValid())
	{
		lua_pushnil(L);
		return 1;
	}

	if (lua_gettop(L) < 2)
	{
		const Entity parent = entity.GetParent();
		if (parent.IsValid())
		{
			pushEntityUserdata(L, parent.GetUuid());
		}
		else
		{
			lua_pushnil(L);
		}
		return 1;
	}

	if (lua_isnoneornil(L, 2) != 0)
	{
		entity.ClearParent();
		lua_pushboolean(L, 1);
		return 1;
	}

	const Entity parent = engine.resolveEntityArgument(L, 2);
	if (!parent.IsValid())
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	entity.SetParent(parent);
	lua_pushboolean(L, 1);
	return 1;
}

int LuaEngine::Impl::l_entity_children(lua_State* L)
{
	const Entity entity = self(L).resolveEntityArgument(L, 1);
	const std::vector<Entity> children = entity.GetChildren();
	lua_createtable(L, static_cast<int>(children.size()), 0);
	int position = 1;
	for (const Entity& child : children)
	{
		pushEntityUserdata(L, child.GetUuid());
		lua_rawseti(L, -2, static_cast<lua_Integer>(position));
		++position;
	}
	return 1;
}

int LuaEngine::Impl::l_entity_getComponent(lua_State* L)
{
	Impl& engine = self(L);
	const char* typeName = luaL_checkstring(L, 2);
	const ComponentType type = componentTypeFromName(typeName);
	if (type == ComponentType::None)
	{
		return luaL_error(L, "unknown component type '%s'", typeName);
	}
	const Entity entity = engine.resolveEntityArgument(L, 1);
	if (!entity.IsValid())
	{
		lua_pushnil(L);
		return 1;
	}
	pushComponentTable(L, entity, type);
	return 1;
}

int LuaEngine::Impl::l_entity_setComponent(lua_State* L)
{
	Impl& engine = self(L);
	const char* typeName = luaL_checkstring(L, 2);
	const ComponentType type = componentTypeFromName(typeName);
	if (type == ComponentType::None)
	{
		return luaL_error(L, "unknown component type '%s'", typeName);
	}
	if (lua_istable(L, 3) == 0)
	{
		return luaL_error(L, "entity:setComponent expects a table");
	}
	const Entity entity = engine.resolveEntityArgument(L, 1);
	if (!entity.IsValid())
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, applyComponentTable(L, 3, entity, type) ? 1 : 0);
	return 1;
}

int LuaEngine::Impl::l_entity_hasComponent(lua_State* L)
{
	Impl& engine = self(L);
	const char* typeName = luaL_checkstring(L, 2);
	const ComponentType type = componentTypeFromName(typeName);
	if (type == ComponentType::None)
	{
		return luaL_error(L, "unknown component type '%s'", typeName);
	}
	const Entity entity = engine.resolveEntityArgument(L, 1);
	lua_pushboolean(L, entityHasComponent(entity, type) ? 1 : 0);
	return 1;
}

int LuaEngine::Impl::l_entity_addComponent(lua_State* L)
{
	Impl& engine = self(L);
	const char* typeName = luaL_checkstring(L, 2);
	const ComponentType type = componentTypeFromName(typeName);
	if (type == ComponentType::None)
	{
		return luaL_error(L, "unknown component type '%s'", typeName);
	}
	const Entity entity = engine.resolveEntityArgument(L, 1);
	if (!entity.IsValid())
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, entityAddComponent(entity, type) ? 1 : 0);
	return 1;
}

int LuaEngine::Impl::l_entity_removeComponent(lua_State* L)
{
	Impl& engine = self(L);
	const char* typeName = luaL_checkstring(L, 2);
	const ComponentType type = componentTypeFromName(typeName);
	if (type == ComponentType::None)
	{
		return luaL_error(L, "unknown component type '%s'", typeName);
	}
	const Entity entity = engine.resolveEntityArgument(L, 1);
	if (!entity.IsValid())
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, entityRemoveComponent(entity, type) ? 1 : 0);
	return 1;
}

int LuaEngine::Impl::l_entity_tostring(lua_State* L)
{
	const EntityUserdata* userdata = checkEntityUserdata(L, 1);
	Impl& engine = self(L);
	const Entity entity = engine.OwnerScene != nullptr ? engine.OwnerScene->FindEntity(userdata->Id) : Entity();
	const std::string text = entity.IsValid()
	                             ? std::format("Entity '{}' ({})", entity.GetName(), userdata->Id.ToString())
	                             : std::format("Entity <invalid> ({})", userdata->Id.ToString());
	lua_pushlstring(L, text.data(), text.size());
	return 1;
}

// ----------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------

LuaEngine::LuaEngine() : m_Impl(CreateScope<Impl>()) {}

LuaEngine::~LuaEngine()
{
	Shutdown();
}

bool LuaEngine::Initialize(Scene& scene)
{
	return m_Impl->initialize(scene);
}

void LuaEngine::Shutdown()
{
	m_Impl->shutdown();
}

bool LuaEngine::IsInitialized() const
{
	return m_Impl->Initialized;
}

void LuaEngine::OnUpdate(Scene& scene, Timestep timestep)
{
	m_Impl->update(scene, timestep);
}

void LuaEngine::RegisterScript(AssetHandle script, const std::filesystem::path& path)
{
	if (!script.IsValid())
	{
		AE_CORE_WARN("Scripting: cannot register a script with a null asset handle");
		return;
	}
	if (path.empty())
	{
		m_Impl->ScriptPaths.erase(script);
		return;
	}
	m_Impl->ScriptPaths[script] = FileSystem::Normalize(FileSystem::Absolute(path));
}

void LuaEngine::UnregisterScript(AssetHandle script)
{
	m_Impl->ScriptPaths.erase(script);
}

void LuaEngine::ClearScripts()
{
	m_Impl->ScriptPaths.clear();
}

void LuaEngine::SetScriptRoot(std::filesystem::path root)
{
	m_Impl->ScriptRoot = root.empty() ? std::filesystem::path() : FileSystem::Normalize(FileSystem::Absolute(root));
}

const std::filesystem::path& LuaEngine::GetScriptRoot() const
{
	return m_Impl->ScriptRoot;
}

void LuaEngine::SetAudioEngine(AudioEngine* audioEngine)
{
	m_Impl->Audio = audioEngine;
}

AudioEngine* LuaEngine::GetAudioEngine() const
{
	return m_Impl->Audio;
}

void LuaEngine::SetPhysicsWorld(PhysicsWorld* physicsWorld)
{
	m_Impl->Physics = physicsWorld;
}

PhysicsWorld* LuaEngine::GetPhysicsWorld() const
{
	return m_Impl->Physics;
}

void LuaEngine::SetReloadCheckInterval(f32 seconds)
{
	m_Impl->ReloadCheckInterval = Max(seconds, 0.0f);
}

f32 LuaEngine::GetReloadCheckInterval() const
{
	return m_Impl->ReloadCheckInterval;
}

usize LuaEngine::GetInstanceCount() const
{
	return m_Impl->Instances.size();
}

f64 LuaEngine::GetElapsedTime() const
{
	return m_Impl->ElapsedTime;
}

u64 LuaEngine::GetErrorCount() const
{
	return m_Impl->ErrorCount;
}

} // namespace aegis