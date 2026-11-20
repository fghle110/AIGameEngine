#pragma once

// Plain-data components. Behaviour lives in systems (scene update), the
// renderer, physics, audio and Lua, never inside these structs.

#include "Aegis/Assets/AssetHandle.h"
#include "Aegis/Core/Base.h"
#include "Aegis/Core/Json.h"
#include "Aegis/Core/Uuid.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace aegis
{
	struct IdComponent
	{
		Uuid Id = Uuid::Null();
	};

	struct TagComponent
	{
		std::string Tag = "Entity";
	};

	struct ActiveComponent
	{
		bool Active = true;
	};

	// Local transform plus the world transform maintained by the scene update.
	// Parent/child links use UUIDs so they survive serialisation and reordering.
	struct TransformComponent
	{
		glm::vec3 LocalPosition{ 0.0f };
		glm::quat LocalRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 LocalScale{ 1.0f };

		glm::vec3 WorldPosition{ 0.0f };
		glm::quat WorldRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 WorldScale{ 1.0f };

		Uuid Parent = Uuid::Null();
		std::vector<Uuid> Children;

		glm::mat4 GetLocalTransform() const;
		glm::mat4 GetWorldTransform() const;

		// Rotation helpers for the editor (degrees, XYZ order).
		glm::vec3 GetLocalEulerAngles() const;
		void SetLocalEulerAngles(const glm::vec3& degrees);
	};

	enum class CameraProjection : u8
	{
		Perspective = 0,
		Orthographic
	};

	struct CameraComponent
	{
		CameraProjection Projection = CameraProjection::Perspective;
		f32 FieldOfView = 45.0f;      // degrees
		f32 NearClip = 0.1f;
		f32 FarClip = 1000.0f;
		f32 OrthographicSize = 10.0f; // half height in world units
		bool Primary = false;
		glm::vec4 ClearColor{ 0.05f, 0.06f, 0.08f, 1.0f };
	};

	enum class LightType : u8
	{
		Directional = 0,
		Point,
		Spot
	};

	struct LightComponent
	{
		LightType Type = LightType::Directional;
		glm::vec3 Color{ 1.0f };
		f32 Intensity = 1.0f;
		f32 Range = 15.0f;
		f32 InnerConeDegrees = 20.0f;
		f32 OuterConeDegrees = 35.0f;
		bool CastShadows = true;
	};

	struct MeshRendererComponent
	{
		AssetHandle Mesh;
		// One material per submesh. An empty list uses the materials stored in the
		// mesh asset.
		std::vector<AssetHandle> Materials;
		bool Visible = true;
		bool CastShadows = true;
		bool ReceiveShadows = true;
	};

	enum class BodyType : u8
	{
		Static = 0,
		Kinematic,
		Dynamic
	};

	struct RigidBodyComponent
	{
		BodyType Type = BodyType::Static;
		f32 Mass = 1.0f;
		f32 Friction = 0.5f;
		f32 Restitution = 0.0f;
		f32 LinearDamping = 0.05f;
		f32 AngularDamping = 0.05f;
		bool UseGravity = true;
	};

	enum class ColliderShape : u8
	{
		Box = 0,
		Sphere,
		Capsule,
		Mesh
	};

	struct ColliderComponent
	{
		ColliderShape Shape = ColliderShape::Box;
		glm::vec3 HalfExtents{ 0.5f };
		f32 Radius = 0.5f;
		f32 HalfHeight = 0.5f; // cylindrical part of a capsule, in metres
		glm::vec3 Offset{ 0.0f };
		bool IsTrigger = false;
	};

	struct LuaScriptComponent
	{
		AssetHandle Script;
		bool Enabled = true;
		// Values exposed to the editor and persisted with the scene. The script
		// reads and writes these through the "properties" script table.
		JsonValue Properties = JsonValue::MakeObject();
	};

	struct AudioSourceComponent
	{
		AssetHandle Clip;
		f32 Volume = 1.0f;
		f32 Pitch = 1.0f;
		bool Loop = false;
		bool PlayOnStart = true;
		bool Spatial = true;
		f32 MinDistance = 1.0f;
		f32 MaxDistance = 40.0f;
	};

	struct AudioListenerComponent
	{
		bool Primary = false;
	};

	// Camera controller used by the editor viewport and by game scripts that want
	// fly-camera behaviour without writing input code.
	struct FlyCameraComponent
	{
		bool Active = false;
		f32 MoveSpeed = 12.0f;
		f32 FastMultiplier = 4.0f;
		f32 LookSensitivity = 0.12f;
		f32 SpeedDamping = 12.0f;
		glm::vec3 Velocity{ 0.0f };
	};
}
