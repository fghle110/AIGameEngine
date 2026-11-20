# Scripting

Aegis embeds [Lua 5.4](https://www.lua.org/) (pinned in `cmake/Dependencies.cmake`)
through `Engine/src/Aegis/Scripting/LuaEngine.h`. One entity with a
`LuaScriptComponent` runs one script instance; scripts drive game logic through
a single global `engine` table and an entity value type.

`LuaEngine` is a `SceneSystem`: the runtime, the editor and the tests compose it
the same way, and nothing in the engine creates it implicitly.

## Quick start

```cpp
#include "Aegis/Scene/Scene.h"
#include "Aegis/Scripting/LuaEngine.h"

aegis::Scene scene("Level");
aegis::LuaEngine scripting;
scripting.Initialize(scene);
scene.AddSystem(&scripting);              // update order 100, after physics

// An entity's LuaScriptComponent holds an asset handle. A host that already
// knows where the file lives registers it once:
aegis::Entity player = scene.CreateEntity("Player");
aegis::LuaScriptComponent& component = player.AddComponent<aegis::LuaScriptComponent>();
component.Script = playerScript;          // handle from the asset manager
scripting.RegisterScript(component.Script, "Assets/Scripts/player.lua");

// Once per frame. Scene::OnUpdate drives transforms, the systems, then
// transforms again, so scripts see the physics result of their own frame.
scene.OnUpdate(aegis::Timestep(1.0f / 60.0f));

// On project/scene teardown, before the scene is destroyed.
scene.RemoveSystem(&scripting);
scripting.Shutdown();
```

`Initialize` opens the Lua standard libraries and installs the bindings; it is
idempotent for the same scene and shuts the previous state down when called for
another one. `Shutdown` runs `OnDestroy` for every started script, closes the
state and keeps the registered script paths, so a host can switch scenes
without registering the assets again.

Optional host wiring:

| Call | Effect |
| --- | --- |
| `SetAudioEngine(AudioEngine*)` | enables `engine.playSound` |
| `SetPhysicsWorld(PhysicsWorld*)` | overrides the physics world used by `engine.physics.raycast` (by default the world registered with the scene is used) |
| `RegisterScript(handle, path)` | maps a script asset handle to a file |
| `SetScriptRoot(path)` | enables the `<root>/<asset uuid>.lua` convention |
| `SetReloadCheckInterval(seconds)` | hot-reload poll interval; 0 checks every update |

## Script resolution

A `LuaScriptComponent` stores an asset handle, never a path. For every instance
the engine resolves the file in this order:

1. **Registered path** - `RegisterScript` stores the handle in a path cache. The
   path is made absolute and normalised when it is registered. Registering a
   different path for a running script reloads it; registering an empty path
   removes the entry.
2. **Project convention** - when a script root is set and the handle is not
   registered, the engine looks for `<scriptRoot>/<asset uuid>.lua` (the UUID of
   the *script asset*, not of the entity). This lets a project ship scripts
   named after their asset id without a per-asset registration step.
3. Nothing resolves -> an error naming the entity is logged **once** and the
   script is not started. The instance is retried when a registration or a file
   appears, without repeating the log line.

A handle with a null UUID never resolves; an entity whose component carries no
script asset is reported the same way.

## Lifecycle

For every enabled entity with a `LuaScriptComponent`:

| Callback | When |
| --- | --- |
| `OnStart(entity)` | once, on the first update after the chunk was loaded |
| `OnUpdate(entity, dt)` | every update after `OnStart`, with the frame time in seconds |
| `OnDestroy(entity)` | before the script instance goes away |

All three callbacks are optional. An instance goes away when:

- the entity is destroyed by a script (`entity:destroy()` / `engine.destroyEntity`):
  `OnDestroy` runs immediately, descendants first, and then the entity is removed;
- the entity is destroyed by the host (`Scene::DestroyEntity`, scene clear, scene load):
  the retirement sweep at the start of the next `LuaEngine` update runs
  `OnDestroy`;
- the `LuaScriptComponent` is removed or `Enabled` becomes false: `OnDestroy`
  runs immediately and the instance is dropped. Re-enabling the component starts
  a new instance (and a new `OnStart`);
- the chunk is reloaded: the old chunk gets `OnDestroy`, the new one `OnStart`.
- the entity UUID is reused (a scene load recreates entities with their persisted
  UUIDs): the old instance is dropped without `OnDestroy` - the entity value
  would already point at the new entity - and the new entity starts a fresh
  instance, so `OnStart` runs again.

**Environment.** Every instance gets its own `_ENV` table whose metatable falls
back to the real globals. Two entities running the same file do not share
top-level `local`/global state, and both still reach the standard libraries and
`engine`.

**Scripts spawned at runtime** get their instance on the next update; a script
that calls `engine.createEntity` does not recurse into a nested update.

**Properties.** `engine.properties` is a per-instance table initialised from
`LuaScriptComponent::Properties`. After every callback the table is written back
into the component's `JsonValue`, so values set by the script serialise with the
scene and survive a reload. The table has to be string-keyed to round-trip;
non-JSON values (functions, userdata, cyclic tables) are dropped.

## Script API

The bindings live in the global `engine` table.

### Scene and entities

| Call | Returns | Notes |
| --- | --- | --- |
| `engine.scene.name` | string | read/write, maps to `Scene::GetName`/`SetName` |
| `engine.createEntity(name)` | entity | `name` optional, defaults to `"Entity"` |
| `engine.destroyEntity(entity)` | bool | runs `OnDestroy` for scripted entities (children first) |
| `engine.findEntity(uuidString)` | entity or nil | accepts the 8-4-4-4-12 form |
| `engine.findEntityByName(name)` | entity or nil | |
| `engine.entities()` | array | every entity in the scene |

Entity values carry the entity UUID. A stale value is safe: methods report
`false`/zero values instead of touching a dead entity.

| Method | Returns | Notes |
| --- | --- | --- |
| `entity:uuid()` | string | |
| `entity:name()` / `entity:setName(s)` | string / bool | |
| `entity:isValid()` | bool | |
| `entity:isActive()` / `entity:setActive(b)` | bool / bool | |
| `entity:destroy()` | bool | same path as `engine.destroyEntity` |
| `entity:position()` / `entity:setPosition(t)` | table / bool | local position |
| `entity:rotation()` / `entity:setRotation(t)` | table / bool | euler angles in degrees (XYZ order) |
| `entity:scale()` / `entity:setScale(t)` | table / bool | local scale |
| `entity:worldPosition()` | table | from the last `Scene::UpdateTransforms` |
| `entity:parent()` | entity or nil | |
| `entity:parent(entity)` / `entity:parent(nil)` | bool | set or clear the parent |
| `entity:children()` | array | |
| `entity:getComponent(type)` | table or nil | snapshot of the component |
| `entity:setComponent(type, table)` | bool | applies the fields present in the table |
| `entity:hasComponent(type)` | bool | |
| `entity:addComponent(type)` | bool | false when it already exists |
| `entity:removeComponent(type)` | bool | |

Vectors are tables. Output is always `{ x = , y = , z = }` (`{ x, y, z, w }`
for colours); input accepts the same names, the colour aliases `r/g/b/a` and
the array form `{ 1.0, 2.0, 3.0 }`. Missing components of a supplied vector
default to 0 (alpha defaults to 1). An unknown component type raises a Lua
error instead of silently returning nil.

### Components

Component tables use the same field names as the serialised scene. Values are
validated and clamped when written back.

| Type | Fields |
| --- | --- |
| `camera` | `projection` (`perspective`/`orthographic`), `fieldOfView` (`fov` alias), `nearClip`, `farClip`, `orthographicSize`, `primary`, `clearColor` |
| `light` | `type` (`directional`/`point`/`spot`), `color`, `intensity`, `range`, `innerConeDegrees`, `outerConeDegrees`, `castShadows` |
| `meshRenderer` | `mesh`, `materials`, `visible`, `castShadows`, `receiveShadows` |
| `rigidBody` | `type` (`static`/`kinematic`/`dynamic`), `mass`, `friction`, `restitution`, `linearDamping`, `angularDamping`, `useGravity` |
| `collider` | `shape` (`box`/`sphere`/`capsule`/`mesh`), `halfExtents`, `radius`, `halfHeight`, `offset`, `isTrigger` |
| `audioSource` | `clip`, `volume`, `pitch`, `loop`, `playOnStart`, `spatial`, `minDistance`, `maxDistance` |
| `audioListener` | `primary` |
| `flyCamera` | `active`, `moveSpeed`, `fastMultiplier`, `lookSensitivity`, `speedDamping`, `velocity` |
| `luaScript` | `script`, `enabled`, `properties` |

Asset references are `{ id = "<uuid>"|nil, type = "<asset type>" }`. A bare UUID
string is accepted on input and keeps the current asset type.

Clamps:

| Component | Rule |
| --- | --- |
| camera | `fieldOfView` 1-179, `nearClip` >= 0.001, `farClip` > `nearClip`, `orthographicSize` >= 0.001, `clearColor` components 0-1 |
| light | `intensity`/`range` >= 0, cones 0-89.9 degrees with inner <= outer |
| rigidBody | `mass` >= 0.001, friction/damping >= 0, restitution 0-1 |
| collider | half extents and radius >= 0.001, half height >= 0 |
| audioSource | volume 0-4 and pitch 0.01-4 through `AudioEngine::ClampVolume/ClampPitch`, distances >= 0 with max >= min |
| flyCamera | speeds and sensitivity >= 0 |

A component table is a snapshot: read it, change fields, write it back with
`setComponent`. Fields omitted from the table keep their current value.
Non-finite numbers are ignored rather than stored.

`camera.fieldOfView` also accepts the shorter name `fov`. A table
read back from `getComponent` carries both names, so when they disagree the
value that differs from the component's current value wins; if both changed,
`fieldOfView` wins.

### Time, input, log and physics

| Call | Returns | Notes |
| --- | --- | --- |
| `engine.time.delta` | number | the frame time in seconds |
| `engine.time.elapsed` | number | seconds since `Initialize` |
| `engine.input.isKeyDown(name)` | bool | key name from `KeyCodeToString`, case-insensitive (`"W"`, `"Space"`, `"LeftShift"`, `"F5"`, ...); unknown names raise a Lua error |
| `engine.input.mousePosition()` | table | `{ x, y }` in window pixels, `{0, 0}` without a window |
| `engine.log.info(fmt, ...)` | - | formats through Lua `string.format`, logger name `Lua` |
| `engine.log.warn(fmt, ...)` | - | |
| `engine.log.error(fmt, ...)` | - | |
| `engine.physics.raycast(origin, direction, maxDistance)` | table or nil | `{ entityUuid = "<uuid>", point = { x, y, z } }`; nil on a miss, without physics or with a degenerate ray |
| `engine.playSound(path, volume)` | bool | resolves and caches the path, then `AudioEngine::Play`; false without an audio engine or a loadable clip |

Input is polled through `Input`, so it is only meaningful while a window is
open; headless tests get `false` and `{0, 0}` instead of a failure.

## Hot reload

The engine polls the script file's modification time every
`SetReloadCheckInterval` seconds (default 0.25; 0 polls every update; tests and
the editor use 0). A changed file is compiled **before** the running chunk is
touched:

- compile and top-level execution succeed -> the old chunk receives `OnDestroy`,
  the new one is installed and receives `OnStart` on the same update. The reload
  is logged at info level.
- the new file does not compile -> the error is logged, the timestamp is
  recorded and the previous chunk keeps running. Editing the file again retries.

Properties survive a reload because they live in the component's JSON, not in
the discarded Lua state.

## Errors

Every callback runs under `lua_pcall` with a traceback message handler. On
failure the engine logs the entity name and UUID, the callback, the script path
and the traceback, then keeps going. An instance that fails
`LuaEngine::c_MaxConsecutiveErrors` (5) times in a row is disabled: it is not
called again until the file reloads or the component is re-enabled, which
prevents a broken `OnUpdate` from flooding the log every frame. A successful
callback resets the counter. `LuaEngine::GetErrorCount()` exposes the total for
the editor.

Load failures are reported once per file state: a missing file, an unresolvable
path or a syntax error logs a single error instead of one per frame.

Unprotected Lua errors cannot happen through the API; a panic handler logs a
critical message and aborts, which indicates an engine bug rather than a script
error.

## Limitations

- `Scene` has no entity-destroyed hook, so `OnDestroy` for host-side removal
  (delete in the editor, scene clear, scene load) runs on the **next** scripting
  update, after the entity is gone; the callback sees an invalid entity value.
  Script-driven destruction is immediate and ordered before removal.
- A scene load (or any destroy-and-recreate that reuses an entity UUID) resets
  that entity's script state: the old chunk is dropped, the new entity starts a
  fresh instance, and the serialised `LuaScriptComponent::Properties` are
  restored with the scene.
- One `LuaEngine` drives one scene at a time. `Initialize` with another scene
  shuts the previous state down; adding the same engine to two scenes logs a
  warning and rebinds on the next update.
- Properties are written back after each callback; editing
  `LuaScriptComponent::Properties` from C++ while an instance runs is overwritten
  by the script's table on the next callback.
- `engine.properties` must be a table with string keys to serialise. Arrays at
  the top level and non-JSON values are dropped on write-back.
- `worldPosition()` reflects the last `Scene::UpdateTransforms`; a position set by
  a script becomes visible through it on the next update.
- Scripts are trusted code: the whole standard library is available and there
  is no sandbox, no instruction budget and no debugger. A long-running loop
  stalls the frame, and `os`/`io` can touch the machine.
- Scripts run on the main thread, synchronously, in `SceneSystem` order 100.
- `require` uses Lua's default `package.path`, which is relative to the process
  working directory; project-relative module lookup is not implemented.
- Asset references from Lua are opaque `{ id, type }` values. There is no asset
  manager integration yet; `engine.playSound` loads clips directly from the path.

## Tests

`Tests/Scripting/LuaEngineTests.cpp` runs as part of `AegisTests` and covers
`OnStart`/`OnUpdate`/`OnDestroy`, transform and component bindings, spawning and
destroying, the property round trip, reload, syntax and runtime errors,
missing-path handling, the script-root convention, the reused-UUID rule and the
physics raycast (asserted gracefully when no physics device is available). Scripts are written to
`TestAssets/Scripting/<case>/` inside the build tree.