# Physics

Aegis simulates rigid bodies with [Jolt Physics](https://github.com/jrouwe/JoltPhysics)
(v5.7, pinned in `cmake/Dependencies.cmake`). Jolt is linked **PRIVATE** into the
engine library: no Jolt type appears in a public header, and game code includes
only `Aegis/Physics/PhysicsWorld.h`.

## Quick start

```cpp
#include "Aegis/Physics/PhysicsWorld.h"
#include "Aegis/Scene/Scene.h"

aegis::Scene scene("Level");
aegis::PhysicsWorld physics;
physics.Initialize();              // owns Jolt, starts its worker threads
scene.AddSystem(&physics);         // update order -100, before scripting/audio

// Later, once per frame. Scene::OnUpdate runs UpdateTransforms, the systems and
// UpdateTransforms again, so physics always sees fresh world matrices.
scene.OnUpdate(aegis::Timestep(1.0f / 60.0f));
```

An entity simulates when it has a `TransformComponent`, a `RigidBodyComponent`
and a `ColliderComponent`. Bodies are created, replaced and destroyed by the
world itself; nothing has to be registered manually. Destroying an entity,
removing either component or deactivating the entity retires its body on the
next update.

## Update order inside the system

1. **Reconcile** – create bodies for new entities, destroy bodies whose entity is
   gone, inactive or lost a component, and recreate bodies whose collider or
   rigid-body settings changed.
2. **Push authored transforms** – static and kinematic bodies receive the
   scene's world position/rotation (only when they actually differ, so untouched
   static geometry is never woken).
3. **Step** – `PhysicsSystem::Update` with a Jolt temporary allocator and job
   system.
4. **Pull dynamic transforms** – dynamic bodies write their origin position and
   rotation back. Root entities write `LocalPosition`/`LocalRotation`
   directly; parented entities are converted through the inverse of the parent's
   world matrix, so the hierarchy still decides where the entity ends up.
5. **Dispatch triggers** – sensor overlaps recorded during the step are reported
   to the game callback on the main thread.

## Component mapping

| Component field | Jolt |
| --- | --- |
| `BodyType::Static` | `EMotionType::Static`, non-moving object layer |
| `BodyType::Kinematic` | `EMotionType::Kinematic`, moving layer |
| `BodyType::Dynamic` | `EMotionType::Dynamic`, moving layer |
| `Mass` | overrides mass, inertia rescaled from the collider |
| `Friction`, `Restitution` | clamped to >= 0 and applied |
| `LinearDamping`, `AngularDamping` | clamped to >= 0 and applied |
| `UseGravity` | `GravityFactor` 1 or 0 |
| `ColliderShape::Box` | `BoxShape(HalfExtents)` |
| `ColliderShape::Sphere` | `SphereShape(Radius)` |
| `ColliderShape::Capsule` | `CapsuleShape(HalfHeight, Radius)`, Y aligned |
| `ColliderShape::Mesh` | **falls back to a box** built from `HalfExtents` |
| `Offset` | `RotatedTranslatedShape` wrapper, so the body origin stays at the entity origin |
| `IsTrigger` | Jolt sensor: contacts are reported, no collision response |

Collider dimensions follow the entity's **world scale** (an authored scale is
applied when the body is built). Non-uniform scale is per axis for boxes; spheres
use the largest scale component and capsules use Y for the cylinder and the
largest of X/Z for the radius.

Bodies are capped at 65536 per world, 65536 broad-phase pairs and 10240 contact
constraints, with a 16 MB step allocator.

## Triggers

`IsTrigger` bodies are created as Jolt sensors. Sensor contacts are collected on
the physics worker threads into a small mutex-protected queue and handed to the
main thread after `Update` returns, so the callback may touch the scene:

```cpp
physics.SetTriggerCallback([](aegis::Entity a, aegis::Entity b)
{
    // Both entities are valid; a and b are never guaranteed to be in a
    // particular order.
});
```

Only the start of an overlap is reported (one callback per body pair and step),
not every frame the overlap persists. Regular (non-trigger) contacts are resolved
by the solver but are not surfaced as game events yet. Trigger pairs are kept
for the scene driven `OnUpdate`; a standalone `Step` has no scene to resolve
entities against and discards them.

## Ray casts

```cpp
aegis::Uuid entity;
glm::vec3 point;
if (physics.Raycast(scene, origin, direction, maxDistance, entity, point))
{
    // entity is the closest body's entity, point is in world space
}
```

`direction` does not need to be normalized; `maxDistance` bounds the query.
Misses, a zero direction or distance and an uninitialized world return `false`
and reset the outputs. Ray casts hit trigger colliders as well, and they never
report a UUID whose entity is already gone.

## Stepping and determinism

`Step` feeds the frame delta straight into Jolt and splits frames longer than
1/60 s into at most four collision steps, which keeps fast bodies from tunnelling
through thin geometry during a hitch. Repeated runs with the same scene, the
same creation order and the same timesteps produce identical results; this is
covered by a test that builds the scene twice and compares 60 steps of
simulation.

Jolt's process-wide registration (allocator hooks, factory, type registry) is
reference counted, so several scenes/tests can own a `PhysicsWorld` at the same
time and the last `Shutdown` releases it.

## Known limitations

- **Mesh colliders** are not supported: `ColliderComponent` does not reference a
  mesh asset, so a `Mesh` shape falls back to its box half extents and logs a
  warning. Real mesh colliders need a mesh reference on the component.
- **Dynamic bodies are teleported with `Rebuild(scene)` only.** A dynamic body
  owns its transform: edits to its `LocalPosition`/`LocalRotation` are
  overwritten by the next step. Call `Rebuild` after moving one in the editor.
- Changing any creation-time setting (body type, mass, shape, damping, trigger
  flag, world scale) **recreates the body**, which resets its velocity.
- No joints, constraints, character controller, continuous collision detection or
  contact events for regular collisions yet. Dynamic bodies use Jolt's discrete
  motion quality.
- A trigger callback must not shut the world down from inside the callback.

## Tests

`Tests/Physics/PhysicsTests.cpp` covers the system contract, a dynamic box
falling and settling on a floor, kinematic bodies ignoring gravity, entity and
component removal, ray casts (hit, miss, distances), trigger reporting on the
main thread, determinism across fresh worlds, entities without colliders,
120-step static stability, parented dynamic bodies, body recreation on type
changes, and `UseGravity`/`SetGravity`.
