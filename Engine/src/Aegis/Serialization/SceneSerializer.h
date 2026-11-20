#pragma once

// Versioned, human-readable serialisation for scenes and prefab entities.
//
// A scene document (version 1) looks like this:
//
//   {
//     "version": 1,
//     "scene": {
//       "uuid": "<uuid>",
//       "name": "<scene name>",
//       "environment": { "ambientColor": [r, g, b], "exposure": 1.0, ... }
//     },
//     "entities": [ <entity object>, ... ]
//   }
//
// An entity object is the unit shared by scene documents and prefab documents:
//
//   {
//     "uuid": "<uuid>",
//     "name": "<entity name>",
//     "active": true,
//     "transform": {
//       "position": [x, y, z],
//       "rotation": [x, y, z, w],
//       "scale": [x, y, z],
//       "parent": "<uuid>|null",
//       "children": ["<uuid>", ...]
//     },
//     "camera": { ... },          // optional, one member per component type
//     "light": { ... },
//     "meshRenderer": { "mesh": { "id": "<uuid>", "type": "mesh" }, "materials": [ ... ], ... },
//     "rigidBody": { ... },
//     "collider": { ... },
//     "luaScript": { "script": { ... }, "enabled": true, "properties": { ... } },
//     "audioSource": { ... },
//     "audioListener": { ... },
//     "flyCamera": { ... }
//   }
//
// Asset handles are always { "id": "<uuid>|null", "type": "<asset type>" }; the
// type uses the same lowercase names as AssetTypeToString(). Only local
// transforms are stored: world transforms are derived by Scene::UpdateTransforms
// after loading. Entity references are UUID strings.
//
// Compatibility rules: members that this version does not know are ignored on
// load (forward compatibility), unknown document versions are rejected with a
// readable error, and a failed load never throws - it leaves the scene empty
// with its previous name, UUID and environment restored.

#include "Aegis/Core/Base.h"
#include "Aegis/Core/Json.h"
#include "Aegis/Scene/Scene.h"

#include <filesystem>
#include <string>

namespace aegis
{
	class SceneSerializer
	{
	public:
		// Format version written by Serialize(). The loader accepts this version
		// only; bumping it requires a migration path for the previous version.
		static constexpr i32 c_CurrentVersion = 1;

		SceneSerializer() = delete;

		// Serialises the whole scene into a document that can be written with
		// JsonValue::Dump(4). Entity order follows the scene's creation order.
		[[nodiscard]] static JsonValue Serialize(const Scene& scene);

		// Clears the scene and rebuilds it from the document. Returns false and
		// writes a readable message to outError (when provided) for malformed input;
		// the function never throws. World transforms are recomputed before returning.
		[[nodiscard]] static bool Deserialize(Scene& scene, const JsonValue& document, std::string* outError = nullptr);

		// Writes the scene as indented JSON. Returns false when the file cannot be
		// written (the reason is logged).
		[[nodiscard]] static bool Save(const Scene& scene, const std::filesystem::path& path);

		// Reads and deserialises a scene file. Parse and format errors are reported
		// through outError and never escape as exceptions.
		[[nodiscard]] static bool Load(Scene& scene, const std::filesystem::path& path,
			std::string* outError = nullptr);

		// Prefab helpers. SerializeEntity writes a versioned prefab document
		// {"version": 1, "entity": <entity object>}; the entity object carries no
		// hierarchy links so a prefab never references entities outside itself.
		// DeserializeEntity instantiates that object as a new entity with a fresh
		// UUID, no parent and no children. A bare entity object (one with "uuid" or
		// "transform" but no "entity" wrapper) is also accepted.
		[[nodiscard]] static JsonValue SerializeEntity(const Entity& entity);
		static Entity DeserializeEntity(Scene& scene, const JsonValue& document);
	};
}
