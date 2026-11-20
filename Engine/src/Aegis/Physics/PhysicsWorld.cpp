#include "Aegis/Physics/PhysicsWorld.h"

// Jolt headers do not include Jolt.h themselves; it has to come first. The
// warning state is restored right after the includes so engine code keeps the
// repository warning level.
#include <Jolt/Jolt.h>

JPH_SUPPRESS_WARNING_PUSH
JPH_SUPPRESS_WARNINGS

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

JPH_SUPPRESS_WARNING_POP

#include "Aegis/Core/Log.h"
#include "Aegis/Scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aegis
{
namespace
{
// --- Tuning ---------------------------------------------------------
// Jolt is stable at 60 Hz collision steps; longer frames are split instead
// of being simulated in one large step so fast bodies cannot tunnel.
constexpr f32 c_MaxStepSeconds = 1.0f / 60.0f;
constexpr i32 c_MaxCollisionSteps = 4;

constexpr f32 c_ColliderMinExtent = 0.001f;
constexpr f32 c_MinBodyMass = 0.001f;
constexpr f32 c_TransformTolerance = 0.0001f;
constexpr f32 c_MinRotationDot = 0.99995f;

constexpr usize c_TempAllocatorSize = 16u * 1024u * 1024u;
constexpr JPH::uint c_MaxBodies = 65536;
constexpr JPH::uint c_MaxBodyPairs = 65536;
constexpr JPH::uint c_MaxContactConstraints = 10240;

// --- Collision layers -----------------------------------------------
// Static bodies live in their own broad phase tree, everything that can
// move (dynamic and kinematic) shares the other one.
namespace Layers
{
constexpr JPH::ObjectLayer c_NonMoving = 0;
constexpr JPH::ObjectLayer c_Moving = 1;
constexpr JPH::ObjectLayer c_Count = 2;
} // namespace Layers

namespace BroadPhaseLayers
{
constexpr JPH::BroadPhaseLayer c_NonMoving(0);
constexpr JPH::BroadPhaseLayer c_Moving(1);
constexpr JPH::uint c_Count = 2;
} // namespace BroadPhaseLayers

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
	BroadPhaseLayerInterfaceImpl()
	{
		m_ObjectToBroadPhase[Layers::c_NonMoving] = BroadPhaseLayers::c_NonMoving;
		m_ObjectToBroadPhase[Layers::c_Moving] = BroadPhaseLayers::c_Moving;
	}

	JPH::uint GetNumBroadPhaseLayers() const override
	{
		return BroadPhaseLayers::c_Count;
	}

	JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
	{
		AE_ASSERT(layer < Layers::c_Count, "Unknown physics object layer {}", layer);
		return m_ObjectToBroadPhase[layer];
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
	{
		switch (static_cast<JPH::BroadPhaseLayer::Type>(layer))
		{
		case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::c_NonMoving):
			return "NON_MOVING";
		case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::c_Moving):
			return "MOVING";
		default:
			AE_ASSERT(false,
			          "Unknown physics broad phase layer {}",
			          static_cast<i32>(static_cast<JPH::BroadPhaseLayer::Type>(layer)));
			return "INVALID";
		}
	}
#endif

private:
	JPH::BroadPhaseLayer m_ObjectToBroadPhase[Layers::c_Count];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
	bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
	{
		switch (layer)
		{
		case Layers::c_NonMoving:
			return broadPhaseLayer == BroadPhaseLayers::c_Moving;
		case Layers::c_Moving:
			return true;
		default:
			AE_ASSERT(false, "Unknown physics object layer {}", layer);
			return false;
		}
	}
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
{
public:
	bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
	{
		switch (first)
		{
		case Layers::c_NonMoving:
			return second == Layers::c_Moving;
		case Layers::c_Moving:
			return true;
		default:
			AE_ASSERT(false, "Unknown physics object layer {}", first);
			return false;
		}
	}
};

// --- Conversions ----------------------------------------------------
JPH::Vec3 ToJoltVec3(const glm::vec3& value)
{
	return JPH::Vec3(value.x, value.y, value.z);
}

JPH::RVec3 ToJoltRVec3(const glm::vec3& value)
{
	return JPH::RVec3(value.x, value.y, value.z);
}

JPH::Quat ToJoltQuat(const glm::quat& value)
{
	const glm::quat normalized = glm::normalize(value);
	return JPH::Quat(normalized.x, normalized.y, normalized.z, normalized.w);
}

glm::vec3 FromJoltVec3(const JPH::Vec3& value)
{
	return glm::vec3(static_cast<f32>(value.GetX()), static_cast<f32>(value.GetY()), static_cast<f32>(value.GetZ()));
}

glm::vec3 FromJoltRVec3(const JPH::RVec3& value)
{
	return glm::vec3(static_cast<f32>(value.GetX()), static_cast<f32>(value.GetY()), static_cast<f32>(value.GetZ()));
}

glm::quat FromJoltQuat(const JPH::Quat& value)
{
	// Jolt stores quaternions as (x, y, z, w), glm as (w, x, y, z).
	return glm::normalize(glm::quat(value.GetW(), value.GetX(), value.GetY(), value.GetZ()));
}

JPH::EMotionType ToJoltMotionType(BodyType type)
{
	switch (type)
	{
	case BodyType::Static:
		return JPH::EMotionType::Static;
	case BodyType::Kinematic:
		return JPH::EMotionType::Kinematic;
	case BodyType::Dynamic:
		return JPH::EMotionType::Dynamic;
	default:
		AE_ASSERT(false, "Unknown physics body type {}", static_cast<i32>(type));
		return JPH::EMotionType::Static;
	}
}

JPH::ObjectLayer ToObjectLayer(BodyType type)
{
	return type == BodyType::Static ? Layers::c_NonMoving : Layers::c_Moving;
}

i32 DetermineJobThreadCount()
{
	const u32 hardwareThreads = std::thread::hardware_concurrency();
	return hardwareThreads > 1 ? static_cast<i32>(hardwareThreads - 1) : 1;
}

// --- Jolt global state ----------------------------------------------
// Jolt keeps process-wide registration (factory, type registry, allocator
// hooks). Worlds share it through a reference count so several scenes or
// tests can create and destroy physics worlds independently.
std::mutex s_JoltMutex;
i32 s_JoltRegistrationCount = 0;

void JoltTraceImpl(const char* format, ...)
{
	char buffer[1024];
	va_list arguments;
	va_start(arguments, format);
	std::vsnprintf(buffer, sizeof(buffer), format, arguments);
	va_end(arguments);
	AE_CORE_WARN("[Jolt] {}", buffer);
}

bool JoltAssertFailedImpl(const char* expression, const char* message, const char* file, JPH::uint line)
{
	AE_CORE_CRITICAL("[Jolt] assertion '{}' failed at {}:{}: {}",
	                 expression,
	                 file,
	                 line,
	                 message != nullptr ? message : "no message");

	// Report instead of breaking: headsless test runs have no debugger
	// attached and must record the failure rather than die on a breakpoint.
	return false;
}

void AcquireJolt()
{
	std::lock_guard<std::mutex> lock(s_JoltMutex);
	if (s_JoltRegistrationCount++ > 0)
	{
		return;
	}

	JPH::RegisterDefaultAllocator();
	JPH::Trace = &JoltTraceImpl;
#ifdef JPH_ENABLE_ASSERTS
	JPH::AssertFailed = &JoltAssertFailedImpl;
#endif
	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();
}

void ReleaseJolt()
{
	std::lock_guard<std::mutex> lock(s_JoltMutex);
	if (s_JoltRegistrationCount == 0 || --s_JoltRegistrationCount > 0)
	{
		return;
	}

	JPH::UnregisterTypes();
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;
	JPH::Trace = nullptr;
#ifdef JPH_ENABLE_ASSERTS
	JPH::AssertFailed = nullptr;
#endif
}

// --- Collider description -------------------------------------------
// Everything that decides how the Jolt body is built. Two states that
// compare equal can share a body; when they differ the body is recreated.
struct BodyState
{
	BodyType Type = BodyType::Static;
	f32 Mass = 1.0f;
	f32 Friction = 0.5f;
	f32 Restitution = 0.0f;
	f32 LinearDamping = 0.05f;
	f32 AngularDamping = 0.05f;
	bool UseGravity = true;
	ColliderShape Shape = ColliderShape::Box;
	glm::vec3 HalfExtents{ 0.5f };
	f32 Radius = 0.5f;
	f32 HalfHeight = 0.5f;
	glm::vec3 Offset{ 0.0f };
	bool IsTrigger = false;
	glm::vec3 WorldScale{ 1.0f };

	bool operator==(const BodyState& other) const = default;
};

BodyState BuildBodyState(const RigidBodyComponent& rigidBody,
                         const ColliderComponent& collider,
                         const TransformComponent& transform)
{
	BodyState state;
	state.Type = rigidBody.Type;
	state.Mass = rigidBody.Mass;
	state.Friction = rigidBody.Friction;
	state.Restitution = rigidBody.Restitution;
	state.LinearDamping = rigidBody.LinearDamping;
	state.AngularDamping = rigidBody.AngularDamping;
	state.UseGravity = rigidBody.UseGravity;
	state.Shape = collider.Shape;
	state.HalfExtents = collider.HalfExtents;
	state.Radius = collider.Radius;
	state.HalfHeight = collider.HalfHeight;
	state.Offset = collider.Offset;
	state.IsTrigger = collider.IsTrigger;
	state.WorldScale = transform.WorldScale;
	return state;
}

glm::vec3 AbsScale(const glm::vec3& scale)
{
	return glm::vec3(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z));
}

JPH::RefConst<JPH::Shape> CreateColliderShape(const Uuid& entityId, const BodyState& state)
{
	const glm::vec3 scale = AbsScale(state.WorldScale);
	JPH::RefConst<JPH::Shape> shape;

	switch (state.Shape)
	{
	case ColliderShape::Sphere:
	{
		const f32 radius = Max(state.Radius * Max(Max(scale.x, scale.y), scale.z), c_ColliderMinExtent);
		shape = new JPH::SphereShape(radius);
		break;
	}
	case ColliderShape::Capsule:
	{
		// Jolt capsules are Y aligned: the cylindrical half height follows the
		// Y scale, the radius the widest of X and Z.
		const f32 halfHeight = Max(state.HalfHeight * scale.y, c_ColliderMinExtent);
		const f32 radius = Max(state.Radius * Max(scale.x, scale.z), c_ColliderMinExtent);
		shape = new JPH::CapsuleShape(halfHeight, radius);
		break;
	}
	case ColliderShape::Box:
	case ColliderShape::Mesh:
	{
		if (state.Shape == ColliderShape::Mesh)
		{
			// A mesh collider would need the mesh asset; the component does
			// not reference one yet, so the entity stays collidable through
			// its box half extents instead of silently losing collision.
			AE_CORE_WARN("Physics: entity {} uses an unsupported Mesh collider, falling back to its box half extents",
			             entityId.ToString());
		}

		glm::vec3 halfExtents = state.HalfExtents * scale;
		halfExtents.x = Max(halfExtents.x, c_ColliderMinExtent);
		halfExtents.y = Max(halfExtents.y, c_ColliderMinExtent);
		halfExtents.z = Max(halfExtents.z, c_ColliderMinExtent);
		shape = new JPH::BoxShape(ToJoltVec3(halfExtents));
		break;
	}
	default:
		AE_ASSERT(false, "Unknown collider shape {}", static_cast<i32>(state.Shape));
		shape = new JPH::BoxShape(JPH::Vec3(c_ColliderMinExtent, c_ColliderMinExtent, c_ColliderMinExtent));
		break;
	}

	if (glm::dot(state.Offset, state.Offset) > 0.0f)
	{
		// Keep the body origin at the entity origin and put the collider at
		// its offset, so the COM stays correct and the transform round trip
		// is exact.
		shape = new JPH::RotatedTranslatedShape(ToJoltVec3(state.Offset * state.WorldScale),
		                                        JPH::Quat::sIdentity(),
		                                        shape.GetPtr());
	}

	return shape;
}

// --- Trigger contacts -----------------------------------------------
struct TriggerPair
{
	JPH::uint32 First = 0;
	JPH::uint32 Second = 0;
};

// Records sensor overlaps from physics worker threads and hands them to the
// main thread after the step. Scene access from a contact callback would
// race with the simulation, so nothing but this queue is touched here.
class ContactListenerImpl final : public JPH::ContactListener
{
public:
	void OnContactAdded(const JPH::Body& body1,
	                    const JPH::Body& body2,
	                    const JPH::ContactManifold&,
	                    JPH::ContactSettings&) override
	{
		if (!body1.IsSensor() && !body2.IsSensor())
		{
			return;
		}

		const TriggerPair pair{ body1.GetID().GetIndexAndSequenceNumber(), body2.GetID().GetIndexAndSequenceNumber() };

		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Pending.push_back(pair);
	}

	std::vector<TriggerPair> Drain()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return std::exchange(m_Pending, {});
	}

private:
	std::mutex m_Mutex;
	std::vector<TriggerPair> m_Pending;
};
} // namespace

// The simulation lives behind this type so PhysicsWorld.h stays free of Jolt
// includes; only this translation unit sees the Jolt world.
struct PhysicsWorld::Impl
{
	explicit Impl(const glm::vec3& gravity)
	{
		System.Init(c_MaxBodies,
		            0,
		            c_MaxBodyPairs,
		            c_MaxContactConstraints,
		            BroadPhaseLayerInterface,
		            ObjectVsBroadPhaseLayerFilter,
		            ObjectLayerPairFilter);
		System.SetGravity(ToJoltVec3(gravity));
		System.SetContactListener(&ContactListener);
	}

	Impl(const Impl&) = delete;
	Impl& operator=(const Impl&) = delete;

	// Records a body for the entity. Returns false when Jolt refused the body,
	// which only happens when the body capacity of the world is exhausted.
	bool CreateBody(const Uuid& entityId, const TransformComponent& transform, const BodyState& state)
	{
		JPH::RefConst<JPH::Shape> shape = CreateColliderShape(entityId, state);

		JPH::BodyCreationSettings settings(shape,
		                                   ToJoltRVec3(transform.WorldPosition),
		                                   ToJoltQuat(transform.WorldRotation),
		                                   ToJoltMotionType(state.Type),
		                                   ToObjectLayer(state.Type));
		settings.mFriction = Max(state.Friction, 0.0f);
		settings.mRestitution = Max(state.Restitution, 0.0f);
		settings.mLinearDamping = Max(state.LinearDamping, 0.0f);
		settings.mAngularDamping = Max(state.AngularDamping, 0.0f);
		settings.mGravityFactor = state.UseGravity ? 1.0f : 0.0f;
		settings.mIsSensor = state.IsTrigger;
		// A sensor has to opt in to see kinematic and static bodies, otherwise a
		// trigger volume would only report dynamic overlaps.
		settings.mCollideKinematicVsNonDynamic = state.IsTrigger;

		if (state.Type != BodyType::Static)
		{
			// Use the component mass and let Jolt rescale the inertia of the
			// collider to it.
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = Max(state.Mass, c_MinBodyMass);
		}

		JPH::BodyInterface& bodyInterface = System.GetBodyInterface();
		JPH::Body* body = bodyInterface.CreateBody(settings);
		if (body == nullptr)
		{
			AE_CORE_ERROR("Physics: could not create a body for entity {}; the world body limit is exhausted",
			              entityId.ToString());
			return false;
		}

		const JPH::BodyID bodyId = body->GetID();
		bodyInterface.AddBody(bodyId,
		                      state.Type == BodyType::Dynamic ? JPH::EActivation::Activate
		                                                      : JPH::EActivation::DontActivate);

		const u32 packedId = bodyId.GetIndexAndSequenceNumber();
		BodyIds.emplace(entityId, packedId);
		BodyStates.emplace(entityId, state);
		BodyOwners.emplace(packedId, entityId);
		return true;
	}

	void DestroyBody(const Uuid& entityId)
	{
		const auto entry = BodyIds.find(entityId);
		if (entry == BodyIds.end())
		{
			return;
		}

		const JPH::BodyID bodyId(entry->second);
		JPH::BodyInterface& bodyInterface = System.GetBodyInterface();
		if (bodyInterface.IsAdded(bodyId))
		{
			bodyInterface.RemoveBody(bodyId);
		}
		bodyInterface.DestroyBody(bodyId);

		BodyOwners.erase(entry->second);
		BodyStates.erase(entityId);
		BodyIds.erase(entry);
	}

	void DestroyAllBodies()
	{
		std::vector<Uuid> entities;
		entities.reserve(BodyIds.size());
		for (const auto& entry : BodyIds)
		{
			entities.push_back(entry.first);
		}

		for (const Uuid& entityId : entities)
		{
			DestroyBody(entityId);
		}

		AE_ASSERT(BodyIds.empty() && BodyStates.empty() && BodyOwners.empty(),
		          "Physics world still tracks bodies after destroying all of them");
	}

	// Creates, replaces and removes bodies so the world matches the scene.
	void Reconcile(Scene& scene)
	{
		std::unordered_set<Uuid, UuidHash> present;
		present.reserve(scene.GetEntityCount());

		scene.GetRegistry().Each<IdComponent>(
		    [&](ecs::EntityHandle handle, IdComponent&)
		    {
			    Entity entity(handle, &scene);
			    if (!entity.IsActive())
			    {
				    return;
			    }

			    TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
			    RigidBodyComponent* rigidBody = entity.TryGetComponent<RigidBodyComponent>();
			    ColliderComponent* collider = entity.TryGetComponent<ColliderComponent>();
			    if (transform == nullptr || rigidBody == nullptr || collider == nullptr)
			    {
				    return;
			    }

			    const Uuid entityId = entity.GetUuid();
			    present.insert(entityId);

			    const BodyState state = BuildBodyState(*rigidBody, *collider, *transform);
			    const auto body = BodyIds.find(entityId);
			    if (body == BodyIds.end())
			    {
				    CreateBody(entityId, *transform, state);
				    return;
			    }

			    const auto applied = BodyStates.find(entityId);
			    if (applied == BodyStates.end() || !(applied->second == state))
			    {
				    // The components changed: replace the body so the simulation
				    // matches them instead of silently using stale settings.
				    DestroyBody(entityId);
				    CreateBody(entityId, *transform, state);
			    }
		    });

		std::vector<Uuid> stale;
		stale.reserve(BodyIds.size());
		for (const auto& entry : BodyIds)
		{
			if (present.find(entry.first) == present.end())
			{
				stale.push_back(entry.first);
			}
		}

		for (const Uuid& entityId : stale)
		{
			DestroyBody(entityId);
		}
	}

	// Static and kinematic bodies follow the authored transform; only dynamic
	// bodies are driven by the simulation.
	void PushEntityTransforms(Scene& scene)
	{
		JPH::BodyInterface& bodyInterface = System.GetBodyInterface();

		for (const auto& entry : BodyIds)
		{
			const auto state = BodyStates.find(entry.first);
			if (state == BodyStates.end() || state->second.Type == BodyType::Dynamic)
			{
				continue;
			}

			Entity entity = scene.FindEntity(entry.first);
			const TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
			if (transform == nullptr)
			{
				continue;
			}

			const glm::vec3 targetPosition = transform->WorldPosition;
			const glm::quat targetRotation = glm::normalize(transform->WorldRotation);

			const JPH::BodyID bodyId(entry.second);
			const bool positionMatches =
			    glm::length(FromJoltRVec3(bodyInterface.GetPosition(bodyId)) - targetPosition) <= c_TransformTolerance;
			const bool rotationMatches =
			    std::abs(glm::dot(FromJoltQuat(bodyInterface.GetRotation(bodyId)), targetRotation)) >= c_MinRotationDot;
			if (positionMatches && rotationMatches)
			{
				continue;
			}

			// Every write invalidates the broad phase entry and wakes the island,
			// so untouched static geometry is left alone above.
			bodyInterface.SetPositionAndRotation(bodyId,
			                                     ToJoltRVec3(targetPosition),
			                                     ToJoltQuat(targetRotation),
			                                     state->second.Type == BodyType::Kinematic
			                                         ? JPH::EActivation::Activate
			                                         : JPH::EActivation::DontActivate);
		}
	}

	void PullDynamicTransforms(Scene& scene)
	{
		JPH::BodyInterface& bodyInterface = System.GetBodyInterface();

		for (const auto& entry : BodyIds)
		{
			const auto state = BodyStates.find(entry.first);
			if (state == BodyStates.end() || state->second.Type != BodyType::Dynamic)
			{
				continue;
			}

			Entity entity = scene.FindEntity(entry.first);
			TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
			if (transform == nullptr)
			{
				continue;
			}

			const JPH::BodyID bodyId(entry.second);
			// The body origin, not the centre of mass, maps to the entity
			// transform; a collider offset must not shift the entity.
			const glm::vec3 worldPosition = FromJoltRVec3(bodyInterface.GetPosition(bodyId));
			const glm::quat worldRotation = FromJoltQuat(bodyInterface.GetRotation(bodyId));
			ApplyWorldTransform(scene, *transform, worldPosition, worldRotation);
		}
	}

	// Dynamic bodies write world space results back into local transforms so the
	// scene hierarchy keeps deciding where the entity ends up.
	void ApplyWorldTransform(Scene& scene,
	                         TransformComponent& transform,
	                         const glm::vec3& worldPosition,
	                         const glm::quat& worldRotation)
	{
		if (transform.Parent.IsNull())
		{
			transform.LocalPosition = worldPosition;
			transform.LocalRotation = worldRotation;
			return;
		}

		const Entity parent = scene.FindEntity(transform.Parent);
		const TransformComponent* parentTransform = parent.TryGetComponent<TransformComponent>();
		if (parentTransform == nullptr)
		{
			transform.LocalPosition = worldPosition;
			transform.LocalRotation = worldRotation;
			return;
		}

		const glm::mat4 parentWorld = parentTransform->GetWorldTransform();
		const glm::mat4 world = glm::translate(glm::mat4(1.0f), worldPosition) * glm::mat4_cast(worldRotation);
		const glm::mat4 local = glm::inverse(parentWorld) * world;

		glm::vec3 scale(1.0f);
		glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 position(0.0f);
		glm::vec3 skew(0.0f);
		glm::vec4 perspective(0.0f);
		if (glm::decompose(local, scale, rotation, position, skew, perspective))
		{
			transform.LocalPosition = position;
			transform.LocalRotation = glm::normalize(rotation);
			return;
		}

		// A degenerate parent scale cannot be inverted; keep the simulated world
		// transform so the body does not jump.
		transform.LocalPosition = worldPosition;
		transform.LocalRotation = worldRotation;
	}

	// Runs Jolt once. Trigger overlaps are only queued for a scene driven step:
	// without OnUpdate there is no scene to resolve the body ids against, so
	// they are dropped instead of piling up in the listener.
	void Advance(f32 deltaTime, bool keepTriggerEvents)
	{
		// Jolt assumes roughly 60 Hz collision steps. Splitting a long frame
		// keeps fast bodies from tunnelling; the cap avoids an unbounded
		// catch-up spiral.
		const i32 collisionSteps =
		    Clamp(static_cast<i32>(std::ceil(deltaTime / c_MaxStepSeconds)), 1, c_MaxCollisionSteps);
		const JPH::EPhysicsUpdateError error = System.Update(deltaTime, collisionSteps, &TempAllocator, &JobSystem);
		if (error != JPH::EPhysicsUpdateError::None)
		{
			AE_CORE_ERROR("Physics: simulation reported update error {} (contact buffers are full)",
			              static_cast<u32>(error));
		}

		if (!keepTriggerEvents)
		{
			ContactListener.Drain();
		}
	}

	// Turns the pairs recorded by the contact listener during the step into
	// callbacks on the main thread.
	void DispatchTriggers(Scene& scene, const TriggerCallback& callback)
	{
		const std::vector<TriggerPair> pairs = ContactListener.Drain();
		if (pairs.empty() || !callback)
		{
			return;
		}

		std::unordered_set<u64> reported;
		reported.reserve(pairs.size());

		for (const TriggerPair& pair : pairs)
		{
			// A pair can be reported once per sub shape; collapse it to one event.
			const u64 key = (static_cast<u64>(pair.First) << 32) | static_cast<u64>(pair.Second);
			if (!reported.insert(key).second)
			{
				continue;
			}

			const auto firstOwner = BodyOwners.find(pair.First);
			const auto secondOwner = BodyOwners.find(pair.Second);
			if (firstOwner == BodyOwners.end() || secondOwner == BodyOwners.end())
			{
				continue;
			}

			const Entity first = scene.FindEntity(firstOwner->second);
			const Entity second = scene.FindEntity(secondOwner->second);
			if (!first.IsValid() || !second.IsValid())
			{
				continue;
			}

			callback(first, second);
		}
	}

	// Members are created in declaration order; the physics system is destroyed
	// first, so every interface it references outlives it.
	JPH::TempAllocatorImpl TempAllocator{ c_TempAllocatorSize };
	JPH::JobSystemThreadPool JobSystem{ static_cast<JPH::uint>(JPH::cMaxPhysicsJobs),
		                                static_cast<JPH::uint>(JPH::cMaxPhysicsBarriers),
		                                DetermineJobThreadCount() };
	BroadPhaseLayerInterfaceImpl BroadPhaseLayerInterface;
	ObjectVsBroadPhaseLayerFilterImpl ObjectVsBroadPhaseLayerFilter;
	ObjectLayerPairFilterImpl ObjectLayerPairFilter;
	ContactListenerImpl ContactListener;
	JPH::PhysicsSystem System;

	std::unordered_map<Uuid, u32, UuidHash> BodyIds;
	std::unordered_map<Uuid, BodyState, UuidHash> BodyStates;
	std::unordered_map<u32, Uuid> BodyOwners;
};

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld()
{
	Shutdown();
}

bool PhysicsWorld::Initialize(const glm::vec3& gravity)
{
	if (m_Impl)
	{
		return true;
	}

	AcquireJolt();

	try
	{
		m_Impl = CreateScope<Impl>(gravity);
	}
	catch (const std::bad_alloc&)
	{
		ReleaseJolt();
		AE_CORE_ERROR("Physics: could not allocate the Jolt simulation");
		return false;
	}

	m_Gravity = gravity;
	AE_CORE_INFO("Physics: Jolt world initialized with {} worker threads", DetermineJobThreadCount());
	return true;
}

void PhysicsWorld::Shutdown()
{
	if (!m_Impl)
	{
		return;
	}

	m_Impl->DestroyAllBodies();
	m_Impl.reset();
	ReleaseJolt();
	AE_CORE_INFO("Physics: Jolt world shut down");
}

bool PhysicsWorld::IsInitialized() const
{
	return m_Impl != nullptr;
}

void PhysicsWorld::Step(Timestep timestep)
{
	if (!m_Impl)
	{
		return;
	}

	const f32 deltaTime = timestep.GetSeconds();
	if (!(deltaTime > 0.0f))
	{
		return;
	}

	m_Impl->Advance(deltaTime, false);
}

void PhysicsWorld::OnUpdate(Scene& scene, Timestep timestep)
{
	if (!m_Impl)
	{
		return;
	}

	m_Impl->Reconcile(scene);
	m_Impl->PushEntityTransforms(scene);

	const f32 deltaTime = timestep.GetSeconds();
	if (deltaTime > 0.0f)
	{
		m_Impl->Advance(deltaTime, true);
	}

	m_Impl->PullDynamicTransforms(scene);
	m_Impl->DispatchTriggers(scene, m_TriggerCallback);
}

void PhysicsWorld::Rebuild(Scene& scene)
{
	if (!m_Impl)
	{
		return;
	}

	// Bodies are created from world transforms, so refresh them even when Rebuild
	// is called outside Scene::OnUpdate.
	scene.UpdateTransforms();
	m_Impl->DestroyAllBodies();
	m_Impl->Reconcile(scene);
	AE_CORE_DEBUG("Physics: rebuilt {} bodies", GetBodyCount());
}

std::string_view PhysicsWorld::GetName() const
{
	return "Physics";
}

i32 PhysicsWorld::GetUpdateOrder() const
{
	return -100;
}

bool PhysicsWorld::Raycast(Scene& scene,
                           const glm::vec3& origin,
                           const glm::vec3& direction,
                           f32 maxDistance,
                           Uuid& outEntity,
                           glm::vec3& outHitPoint) const
{
	outEntity = Uuid::Null();
	outHitPoint = origin;

	if (!m_Impl || !(maxDistance > 0.0f))
	{
		return false;
	}

	const f32 directionLength = glm::length(direction);
	if (!(directionLength > 0.0f))
	{
		return false;
	}

	// Jolt stores direction and length in one vector.
	const glm::vec3 rayVector = direction / directionLength * maxDistance;
	const JPH::RRayCast ray(ToJoltRVec3(origin), ToJoltVec3(rayVector));

	JPH::RayCastResult hit;
	if (!m_Impl->System.GetNarrowPhaseQuery().CastRay(ray, hit))
	{
		return false;
	}

	const auto owner = m_Impl->BodyOwners.find(hit.mBodyID.GetIndexAndSequenceNumber());
	if (owner == m_Impl->BodyOwners.end())
	{
		return false;
	}

	// Bodies outlive their entity by at most one update; never hand a stale UUID
	// to the caller.
	if (!scene.FindEntity(owner->second).IsValid())
	{
		return false;
	}

	outEntity = owner->second;
	outHitPoint = FromJoltRVec3(ray.GetPointOnRay(hit.mFraction));
	return true;
}

bool PhysicsWorld::GetLinearVelocity(const Uuid& entity, glm::vec3& outVelocity) const
{
	outVelocity = glm::vec3(0.0f);

	if (!m_Impl)
	{
		return false;
	}

	const auto entry = m_Impl->BodyIds.find(entity);
	if (entry == m_Impl->BodyIds.end())
	{
		return false;
	}

	outVelocity = FromJoltVec3(m_Impl->System.GetBodyInterface().GetLinearVelocity(JPH::BodyID(entry->second)));
	return true;
}

void PhysicsWorld::SetGravity(const glm::vec3& gravity)
{
	m_Gravity = gravity;
	if (m_Impl)
	{
		m_Impl->System.SetGravity(ToJoltVec3(gravity));
	}
}

glm::vec3 PhysicsWorld::GetGravity() const
{
	return m_Gravity;
}

void PhysicsWorld::SetTriggerCallback(TriggerCallback callback)
{
	m_TriggerCallback = std::move(callback);
}

usize PhysicsWorld::GetBodyCount() const
{
	return m_Impl ? m_Impl->BodyIds.size() : 0;
}
} // namespace aegis
