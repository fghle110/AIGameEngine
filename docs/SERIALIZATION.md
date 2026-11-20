# Scene and prefab serialisation

`SceneSerializer` (`Aegis/Serialization/SceneSerializer.h`) writes and reads the
engine's human-readable scene and prefab formats. Both use the engine JSON DOM
(`Aegis/Core/Json.h`) and are written with `JsonValue::Dump(4)`; member order is
the DOM's `std::map` order, so serialising the same scene twice produces
byte-identical text.

## Scene document (version 1)

```json
{
  "version": 1,
  "scene": {
    "uuid": "b0e0...",
    "name": "Feature Test",
    "environment": {
      "ambientColor": [0.06, 0.07, 0.09],
      "ambientIntensity": 1.0,
      "hdriPath": "",
      "hdriIntensity": 1.0,
      "exposure": 1.0,
      "enableIBL": true,
      "enableShadows": true,
      "enableSSAO": true,
      "enableBloom": true,
      "enableFXAA": true,
      "shadowDistance": 60.0,
      "ssaoRadius": 0.6,
      "bloomThreshold": 1.1,
      "bloomIntensity": 0.35
    }
  },
  "entities": [ <entity object>, ... ]
}
```

## Entity object

```json
{
  "uuid": "<uuid>",
  "name": "Cube",
  "active": true,
  "transform": {
    "position": [x, y, z],
    "rotation": [x, y, z, w],
    "scale": [x, y, z],
    "parent": "<uuid>|null",
    "children": ["<uuid>", ...]
  },
  "camera": { ... },
  "light": { ... },
  "meshRenderer": {
    "mesh": { "id": "<uuid>|null", "type": "mesh" },
    "materials": [ { "id": "<uuid>", "type": "material" } ],
    "visible": true,
    "castShadows": true,
    "receiveShadows": true
  },
  "rigidBody": { "type": "static|kinematic|dynamic", "mass": 1.0, ... },
  "collider": { "shape": "box|sphere|capsule|mesh", "halfExtents": [x, y, z], ... },
  "luaScript": { "script": { ... }, "enabled": true, "properties": { ... } },
  "audioSource": { "clip": { ... }, "volume": 1.0, ... },
  "audioListener": { "primary": false },
  "flyCamera": { "active": false, "moveSpeed": 12.0, ... }
}
```

Every component member except `transform` is optional; missing members fall back
to the component's default values, so hand-authored documents stay small. The
component keys are written only when the entity owns the component.

Asset handles are `{"id": "<uuid>"|null, "type": "<asset type>"}`; the type
string is the lowercase name produced by `AssetTypeToString()` (`mesh`,
`material`, `script`, `audio`, ...) and is read back with
`AssetTypeFromString()`.

Only **local** transforms are stored. `SceneSerializer::Deserialize` restores the
parent links, rebuilds every child list, and then calls
`Scene::UpdateTransforms()`, so world transforms are always derived from the
loaded data instead of being trusted from the file.

## Loading rules

1. The scene is cleared first; `scene`, `name`, `uuid` and `environment` are
   applied from the document.
2. All entities are created before any hierarchy link is restored, so a child may
   appear before its parent in the document.
3. Parent links are taken from each entity's `transform.parent`. The
   `transform.children` array is treated as a hint for ordering: child lists are
   rebuilt from the parent links, using the authored order where the document is
   consistent.
4. A parent UUID that does not exist (or a link that would create a cycle) is
   dropped with a warning; the entity stays at the root.
5. Duplicate entity UUIDs, an invalid or missing `uuid`, and non-object
   entity/component entries are hard errors.
6. On any error `Deserialize` returns `false`, writes a readable message through
   `outError` (never throws) and leaves the scene empty with its previous name,
   UUID and environment restored.

Unknown members are ignored on load, which keeps newer documents readable by
older builds. As the in-memory scene has no place to keep them, unknown members
are **dropped** when the scene is written again (the omission is intentional, per
AGENTS.md section 7). Keeping them would require a side-car or a raw-document
field on `Scene`, which neither exists today.

## Versions

`version` is mandatory and currently `1`. Any other value is rejected with
"unsupported scene format version ...". Version 1 is the initial published
format; when semantics change, bump `SceneSerializer::c_CurrentVersion` and keep
a loader path for version 1 (AGENTS.md section 7).

## Prefab documents

`SceneSerializer::SerializeEntity` writes a versioned prefab document:

```json
{
  "version": 1,
  "entity": { <entity object without hierarchy> }
}
```

The embedded entity object carries `"parent": null` and an empty `"children"`
array, so a prefab never references entities outside itself. The stored `uuid`
identifies the authored entity inside the prefab file only.

`SceneSerializer::DeserializeEntity(Scene&, const JsonValue&)` instantiates that
entity object as a **new** entity with a freshly generated UUID, no parent and no
children, restores all components and updates transforms. A bare entity object
(one with `uuid` or `transform` at the root) is also accepted. Malformed prefab
documents are logged and yield an invalid `Entity`.

## Numbers

Component floats are stored through the JSON DOM as f64, which is the DOM's only
number type. An f32 value therefore keeps its exact value but may be printed as
its full decimal expansion (`0.11f` is written `0.10999999940395355`). Loading
casts back to f32, so component values round trip bit-exactly and repeated saves
of the same scene are byte-identical.

## Files

`Save` writes `Dump(4)` plus a trailing newline, creating parent directories as
needed. `Load` reports unreadable files and parse errors through `outError`;
neither function throws across the module boundary.
