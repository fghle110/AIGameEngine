# AI control API

The editor is driven through one command registry. Every editor capability —
from the UI and from automation — is a `CommandBus` command; the HTTP server is
only a transport, and the CLI is a thin client for that transport. Adding a UI
action with side effects means adding a command (see `AGENTS.md` §1.4).

| Layer | Source | Responsibility |
| --- | --- | --- |
| Command registry | `Engine/src/Aegis/Commands/CommandBus.{h,cpp}` | Command metadata, execution, history |
| Editor commands | `Editor/src/Editor/EditorCommands.{h,cpp}` | Scenes, entities, components, editor state |
| HTTP transport | `Engine/src/Aegis/Commands/HttpServer.{h,cpp}` | HTTP/1.1 on `127.0.0.1` |
| CLI | `Tools/src/Cli/` | `health`, `commands`, `call` |

## Endpoint contract

The server listens on `http://127.0.0.1:7777` by default
(`c_DefaultCommandPort` in `Aegis/Core/Config.h`). It binds to loopback only.

| Method | Path | Request body | Response body |
| --- | --- | --- | --- |
| `GET` | `/health` | none | `{"ok":true,"status":"running","engine":"Aegis","version":"…","address":"127.0.0.1","port":7777,"pendingRequests":0}` |
| `GET` | `/commands` | none | The command registry array: `[{"name","category","description","undoable","parameters":[{"name","type","required","description"}]}, …]` |
| `POST` | `/command` | `{"command":"<name>","params":{…}}` | `{"ok":true,"command":"<name>","result":…}` or `{"ok":false,"command":"<name>","error":"…"}` |

Any other route returns `404` with a JSON error that lists the valid routes.
A command that fails validation returns HTTP `200` with `"ok": false`; the HTTP
status describes the transport, not the command.

Request bodies are JSON and must be `Content-Length` framed. The body limit is
1 MiB, request line plus headers must fit in 16 KiB, and chunked transfer
encoding is rejected. Connections are one request per connection
(`Connection: close`).

### Threading contract

The HTTP accept/read thread never executes a command. It enqueues the request
and blocks on a condition variable; the editor main thread calls
`HttpServer::ProcessPending()` once per frame, which executes **at most 8**
queued requests through `CommandBus::ExecuteJson()` and fulfils them. Command
handlers therefore always run on the thread that owns the scene, the ECS
registry and the renderer — the same thread the UI runs on.

`Stop()` refuses new connections, answers pending requests with `503`, closes
the listener and joins the network thread. Failed or timed-out requests are
never executed afterwards.

### curl examples

```sh
curl http://127.0.0.1:7777/health
curl http://127.0.0.1:7777/commands

curl -X POST http://127.0.0.1:7777/command \
  -H "Content-Type: application/json" \
  -d '{"command":"project.info"}'

curl -X POST http://127.0.0.1:7777/command \
  -H "Content-Type: application/json" \
  -d '{"command":"scene.createEntity","params":{"name":"Player"}}'

curl -X POST http://127.0.0.1:7777/command \
  -H "Content-Type: application/json" \
  -d '{"command":"transform.set","params":{"uuid":"<entity uuid>","position":[0,2,0]}}'
```

## Command line client

```text
AegisCli health [--url <url>]
AegisCli commands [--url <url>]
AegisCli call <command> [json] [--url <url>]
```

The default URL is `http://127.0.0.1:7777`; `--url` (or `--url=<url>`) targets
another editor. `commands` and `health` pretty-print the JSON response.
`call` accepts an optional `params` object as the second argument; when it is
omitted the command receives `{}`. `AegisCli help` documents all of this.

Exit codes: `0` success, `1` usage/transport/HTTP error, `2` the command itself
returned `"ok": false`.

```sh
AegisCli health
AegisCli commands
AegisCli call project.info
AegisCli call scene.createEntity '{"name":"Player"}'
AegisCli call scene.list --url http://127.0.0.1:9000
```

## Wiring the server

`HttpServer` is engine code and has no renderer dependency. The editor (or a
test harness) owns one instance, starts it after the scene and command context
exist, and drives it from the main loop:

```cpp
aegis::HttpServer commandServer;
commandServer.Start(aegis::c_DefaultCommandPort);   // 0 = ephemeral port

// once per frame, from the main thread:
commandServer.ProcessPending();

// on shutdown:
commandServer.Stop();
```

The editor command context is described by `EditorCommandContext`:

```cpp
aegis::EditorCommandContext context;
context.Scene = m_Scene.get();          // required by every scene command
context.Selection = &m_SelectedEntity;  // editor.selection.*
context.Renderer = m_Renderer.get();    // project.info / editor.stats (optional)
context.ProjectRoot = projectDirectory;  // relative path resolution
context.ScreenshotRequest = [this](const std::string& path) { m_Renderer->RequestScreenshot(path); };
context.SaveScene = [this](std::string* outMessage) { return saveCurrentScene(outMessage); };
context.LoadScene = [this](const std::string& path, std::string* outError) { return loadScene(path, outError); };
aegis::RegisterEditorCommands(context);
```

`RegisterEditorCommands()` is idempotent: calling it again with a new context
replaces the handlers, so the most recent context always wins. All handlers
copy the context and require its pointers to stay valid for the editor's
lifetime.

## Command reference

All parameters are members of the `params` object. `uuid` parameters are entity
UUID strings as produced by `scene.createEntity` / `scene.list`.

| Command | Parameters | Result | Description |
| --- | --- | --- | --- |
| `project.info` | – | scene name/UUID, entity count, engine name/version, GPU, resolution, project root, selection | Reports the current editor state. `gpuName` is `"unavailable"` and the resolution is `0` when no renderer is attached. |
| `scene.list` | – | array of `{uuid,name,active,parent,components:[names]}` | Lists every entity. `components` covers the attachable component types only; id/tag/transform/active are implicit. |
| `scene.get` | `uuid` | entity document | Dumps one entity: `uuid`, `name`, `active`, `transform` (position, rotation quaternion, `eulerAngles`, scale, parent, children), `components` names, plus the component data members named like the scene format (`camera`, `light`, …). |
| `scene.createEntity` | `name` | `{uuid,name,entityCount}` | Creates an empty entity with a transform and an active flag. |
| `scene.destroyEntity` | `uuid` | `{uuid,destroyed,entityCount}` | Destroys the entity and its descendants; clears the selection if it pointed at one of them. |
| `scene.reparent` | `uuid`, `parent` (`uuid\|null`) | `{uuid,parent}` | Reparents or detaches while preserving the world transform; self/cycle/foreign-UUID parents are rejected. |
| `scene.rename` | `uuid`, `name` | `{uuid,name}` | Renames an entity (1–256 characters). |
| `scene.setActive` | `uuid`, `active` (bool) | `{uuid,active}` | Activates or deactivates an entity. |
| `scene.new` | `name` (optional) | scene summary + `cameraUuid` | Clears the scene, assigns a fresh UUID and default environment, sets the name (default `Untitled Scene`) and creates the default primary camera. |
| `scene.save` | `path` (optional) | scene summary + `{saved,path}` | With `path` writes the scene there (relative paths resolve against `ProjectRoot`); without it invokes the editor `SaveScene` callback. |
| `scene.load` | `path` | scene summary + `path` | Loads a scene through the editor `LoadScene` callback, or directly with `SceneSerializer` when no callback is registered. |
| `scene.snapshot` | – | scene document | `SceneSerializer::Serialize()` of the live scene (version, scene metadata, entities). |
| `transform.set` | `uuid`, plus any of `position` [3], `rotation` [3] (euler degrees), `rotationQuaternion` [4] (x,y,z,w), `scale` [3] | entity `transform` object | Sets the local transform. At least one property is required; `rotation` and `rotationQuaternion` are mutually exclusive; quaternions are normalised and must be non-zero. |
| `component.add` | `uuid`, `type` | component data | Adds a component with default values. Fails when the entity already has it. |
| `component.remove` | `uuid`, `type` | `{uuid,type,removed}` | Removes a component; fails when it is absent. |
| `component.get` | `uuid`, `type` | component data | Returns the component's properties in scene-file form. |
| `component.set` | `uuid`, `type`, `property`, `value` | updated component data | Validates the property name, type and range before writing. |
| `component.setMany` | `uuid`, `type`, `values` (object) | updated component data | Validates every property and the component's cross-property invariants, then commits atomically; a failure changes nothing. |
| `editor.selection.get` | – | `{uuid}` (null when nothing is selected) | Returns the editor selection. |
| `editor.selection.set` | `uuid` (`uuid\|null`) | `{uuid}` | Selects an existing entity or clears the selection with `null`. |
| `editor.screenshot` | `path` | `{path,requested}` | Queues a screenshot of the next rendered frame. The file is written asynchronously by the renderer. |
| `editor.stats` | – | `{fps,frameTimeMs,sceneName,entityCount,width,height,gpuName,rendererInitialized,vSync}` | Renderer/scene counters. Frame pacing lives in the main loop, so `fps` and `frameTimeMs` are `null`. |
| `console.logs` | `limit` (optional, 1–2000, default 200) | array of `{level,logger,message,timestamp}` | Most recent log messages, oldest first. |

Unknown commands are rejected by `CommandBus` with the offending name. Unknown
component types and properties are rejected with the valid values in the error
test. Example:

```json
{"ok":false,"command":"component.set","error":"unknown property 'wibble' for component 'camera'; valid properties: projection, fieldOfView, nearClip, farClip, orthographicSize, primary, clearColor"}
```

## Components

Attachable component types:

`camera`, `light`, `meshRenderer`, `rigidBody`, `collider`, `audioSource`,
`audioListener`, `luaScript`, `flyCamera`.

Asset references are `{"id":"<uuid>","type":"<asset type>"}` (or `null` to
clear). The type must match the property; valid asset types are `mesh`,
`texture`, `material`, `shader`, `scene`, `prefab`, `script`, `audio`,
`environment`, `font`, `animation`.

### camera

| Property | Type | Validation |
| --- | --- | --- |
| `projection` | string | `perspective`, `orthographic` |
| `fieldOfView` | number | 1–179 degrees |
| `nearClip` | number | > 0 and < `farClip` |
| `farClip` | number | > `nearClip` |
| `orthographicSize` | number | > 0 |
| `primary` | bool | – |
| `clearColor` | number[4] | each component 0–1 |

### light

| Property | Type | Validation |
| --- | --- | --- |
| `type` | string | `directional`, `point`, `spot` |
| `color` | number[3] | each component ≥ 0 |
| `intensity` | number | ≥ 0 |
| `range` | number | > 0 |
| `innerConeDegrees` | number | 0–179 |
| `outerConeDegrees` | number | 0–179, ≥ `innerConeDegrees` |
| `castShadows` | bool | – |

### meshRenderer

| Property | Type | Validation |
| --- | --- | --- |
| `mesh` | mesh handle or null | asset type `mesh` |
| `materials` | array of material handles | each asset type `material` |
| `visible` | bool | – |
| `castShadows` | bool | – |
| `receiveShadows` | bool | – |

### rigidBody

| Property | Type | Validation |
| --- | --- | --- |
| `type` | string | `static`, `kinematic`, `dynamic` |
| `mass` | number | ≥ 0; > 0 when `type` is `dynamic` |
| `friction` | number | 0–1 |
| `restitution` | number | 0–1 |
| `linearDamping` | number | ≥ 0 |
| `angularDamping` | number | ≥ 0 |
| `useGravity` | bool | – |

### collider

| Property | Type | Validation |
| --- | --- | --- |
| `shape` | string | `box`, `sphere`, `capsule`, `mesh` |
| `halfExtents` | number[3] | each component > 0 |
| `radius` | number | > 0 |
| `halfHeight` | number | ≥ 0 |
| `offset` | number[3] | finite |
| `isTrigger` | bool | – |

### audioSource

| Property | Type | Validation |
| --- | --- | --- |
| `clip` | audio handle or null | asset type `audio` |
| `volume` | number | 0–1 |
| `pitch` | number | 0.01–4 |
| `loop` | bool | – |
| `playOnStart` | bool | – |
| `spatial` | bool | – |
| `minDistance` | number | > 0 |
| `maxDistance` | number | > 0 and ≥ `minDistance` |

### audioListener

| Property | Type | Validation |
| --- | --- | --- |
| `primary` | bool | – |

### luaScript

| Property | Type | Validation |
| --- | --- | --- |
| `script` | script handle or null | asset type `script` |
| `enabled` | bool | – |
| `properties` | object | any JSON object; replaces the exposed script properties |

### flyCamera

| Property | Type | Validation |
| --- | --- | --- |
| `active` | bool | – |
| `moveSpeed` | number | ≥ 0 |
| `fastMultiplier` | number | > 0 |
| `lookSensitivity` | number | ≥ 0 |
| `speedDamping` | number | ≥ 0 |
| `velocity` | number[3] | finite |

## Known limitations

- The server is a local developer tool: loopback only, no authentication, no
  TLS, one request per connection and one connection at a time. Do not expose
  it on a network interface.
- `Content-Length` bodies only; chunked uploads are rejected with `400`.
- `CommandInfo::Undoable` is `false` for every command: the editor has no undo
  stack yet. The flag and the HTTP metadata make the gap visible rather than
  pretending that commands are reversible.
- `editor.stats` reports `fps`/`frameTimeMs` as `null`; frame pacing is owned by
  the main loop and is not yet published to the command context.
- `editor.screenshot` queues a capture; the file appears after the next frame,
  not when the command returns.
- `scene.json` paths resolve against `EditorCommandContext::ProjectRoot` (or the
  process working directory when it is empty); the command never writes outside
  the path the caller requested.
