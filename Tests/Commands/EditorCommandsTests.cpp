#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Commands/CommandBus.h"
#include "Aegis/Core/Config.h"
#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Json.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Scene/Components.h"
#include "Aegis/Scene/Entity.h"
#include "Aegis/Scene/Scene.h"
#include "Aegis/Serialization/SceneSerializer.h"
#include "Editor/EditorCommands.h"

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

using namespace aegis;

namespace
{
JsonValue makeParams(std::initializer_list<std::pair<std::string, JsonValue>> values)
{
	JsonValue params = JsonValue::MakeObject();
	for (const auto& [key, value] : values)
	{
		params.Set(key, value);
	}
	return params;
}

JsonValue numberArray(std::initializer_list<f32> values)
{
	JsonValue array = JsonValue::MakeArray();
	for (const f32 value : values)
	{
		array.Push(value);
	}
	return array;
}

JsonValue uuidJson(const Uuid& uuid)
{
	return JsonValue(uuid.ToString());
}

bool containsString(const JsonValue& array, std::string_view text)
{
	if (!array.IsArray())
	{
		return false;
	}
	for (usize index = 0; index < array.GetSize(); ++index)
	{
		if (array[index].IsString() && array[index].AsString() == text)
		{
			return true;
		}
	}
	return false;
}

// Exercises the real editor command handlers against a real Scene and a real
// CommandBus registration. The renderer is null: nothing here needs a GPU.
struct EditorFixture
{
	EditorFixture()
	{
		ScenePtr = CreateScope<Scene>("Command Test Scene");
		ScenePtr->CreateEntity("Existing");

		Context.Scene = ScenePtr.get();
		Context.Selection = &Selection;
		Context.Renderer = nullptr;
		Context.ProjectRoot = FileSystem::ToUtf8(FileSystem::GetCurrentWorkingDirectory());
		Context.ScreenshotRequest = [this](const std::string& path) { ScreenshotPath = path; };
		Context.SaveScene = [this](std::string* outMessage)
		{
			const std::filesystem::path path =
			    FileSystem::GetCurrentWorkingDirectory() / "TestAssets" / "editor-commands-callback.aescene";
			if (!SceneSerializer::Save(*ScenePtr, path))
			{
				if (outMessage != nullptr)
				{
					*outMessage = "callback save failed";
				}
				return false;
			}
			if (outMessage != nullptr)
			{
				*outMessage = FileSystem::ToUtf8(path);
			}
			return true;
		};
		Context.LoadScene = [this](const std::string& path, std::string* outError)
		{
			LoadedPath = path;
			return SceneSerializer::Load(*ScenePtr, FileSystem::FromUtf8(path), outError);
		};

		FileSystem::CreateDirectories(FileSystem::GetCurrentWorkingDirectory() / "TestAssets");
		RegisterEditorCommands(Context);
	}

	CommandResult Execute(std::string_view name, const JsonValue& params = JsonValue())
	{
		return CommandBus::Get().Execute(name, params);
	}

	Uuid CreateEntity(std::string_view name)
	{
		const CommandResult result = Execute("scene.createEntity", makeParams({ { "name", std::string(name) } }));
		REQUIRE(result.Success);
		return Uuid::FromString(result.Value.GetString("uuid"));
	}

	CommandResult ExecuteForEntity(std::string_view command, const Uuid& uuid, const JsonValue& extra = JsonValue())
	{
		JsonValue params = extra.IsObject() ? extra : JsonValue::MakeObject();
		params.Set("uuid", uuidJson(uuid));
		return Execute(command, params);
	}

	Scope<Scene> ScenePtr;
	Uuid Selection = Uuid::Null();
	EditorCommandContext Context;
	std::string ScreenshotPath;
	std::string LoadedPath;
};
} // namespace

TEST_CASE("EditorCommands: project.info reports scene state and renderer availability")
{
	EditorFixture fixture;
	const CommandResult result = fixture.Execute("project.info");
	REQUIRE(result.Success);
	CHECK(result.Value.GetString("sceneName") == "Command Test Scene");
	CHECK(result.Value.GetString("sceneUuid") == fixture.ScenePtr->GetUuid().ToString());
	CHECK(result.Value.GetUInt("entityCount") == 1);
	CHECK(result.Value.GetString("engineVersion") == std::string(c_EngineVersion));
	CHECK(result.Value.GetString("gpuName") == "unavailable");
	CHECK(result.Value.GetUInt("width") == 0);
	CHECK(result.Value.GetUInt("height") == 0);
	CHECK_FALSE(result.Value.GetBool("rendererInitialized"));
	REQUIRE(result.Value.Find("selection") != nullptr);
	CHECK(result.Value["selection"].IsNull());
}

TEST_CASE("EditorCommands: scene.list reports entities and components")
{
	EditorFixture fixture;
	const Uuid cameraUuid = fixture.CreateEntity("Command Camera");
	REQUIRE(fixture.ExecuteForEntity("component.add", cameraUuid, makeParams({ { "type", "camera" } })).Success);

	const CommandResult result = fixture.Execute("scene.list");
	REQUIRE(result.Success);
	const JsonValue& entities = result.Value;
	REQUIRE(entities.IsArray());
	REQUIRE(entities.GetSize() == 2);

	bool foundCamera = false;
	for (usize index = 0; index < entities.GetSize(); ++index)
	{
		const JsonValue& entry = entities[index];
		if (entry.GetString("uuid") != cameraUuid.ToString())
		{
			continue;
		}
		foundCamera = true;
		CHECK(entry.GetString("name") == "Command Camera");
		CHECK(entry.GetBool("active", false));
		CHECK(entry["parent"].IsNull());
		CHECK(containsString(entry["components"], "camera"));
	}
	CHECK(foundCamera);
}

TEST_CASE("EditorCommands: entity lifecycle, hierarchy and scene.get")
{
	EditorFixture fixture;
	const Uuid parentUuid = fixture.CreateEntity("Parent");
	const Uuid childUuid = fixture.CreateEntity("Child");

	CommandResult result =
	    fixture.ExecuteForEntity("scene.rename", childUuid, makeParams({ { "name", "Renamed Child" } }));
	REQUIRE(result.Success);
	CHECK(result.Value.GetString("name") == "Renamed Child");
	CHECK(fixture.ScenePtr->FindEntity(childUuid).GetName() == "Renamed Child");

	result = fixture.ExecuteForEntity("scene.setActive", childUuid, makeParams({ { "active", false } }));
	REQUIRE(result.Success);
	CHECK_FALSE(result.Value.GetBool("active", true));
	CHECK_FALSE(fixture.ScenePtr->FindEntity(childUuid).IsActive());

	result = fixture.ExecuteForEntity("scene.reparent", childUuid, makeParams({ { "parent", uuidJson(parentUuid) } }));
	REQUIRE(result.Success);
	CHECK(result.Value.GetString("parent") == parentUuid.ToString());
	CHECK(fixture.ScenePtr->FindEntity(childUuid).GetParent().GetUuid() == parentUuid);

	const CommandResult dump = fixture.ExecuteForEntity("scene.get", childUuid);
	REQUIRE(dump.Success);
	CHECK(dump.Value.GetString("uuid") == childUuid.ToString());
	CHECK(dump.Value.GetString("name") == "Renamed Child");
	CHECK_FALSE(dump.Value.GetBool("active", true));
	CHECK(dump.Value["transform"].GetString("parent") == parentUuid.ToString());
	CHECK(dump.Value["transform"].Contains("eulerAngles"));
	CHECK(dump.Value.Contains("components"));

	result = fixture.ExecuteForEntity("scene.reparent", parentUuid, makeParams({ { "parent", uuidJson(childUuid) } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("cycle") != std::string::npos);

	result = fixture.Execute("scene.destroyEntity", makeParams({ { "uuid", uuidJson(parentUuid) } }));
	REQUIRE(result.Success);
	CHECK(fixture.ScenePtr->GetEntityCount() == 1);
	CHECK_FALSE(fixture.ScenePtr->FindEntity(childUuid).IsValid());

	result = fixture.Execute("scene.get", makeParams({ { "uuid", uuidJson(childUuid) } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find(childUuid.ToString()) != std::string::npos);
}

TEST_CASE("EditorCommands: transform.set validates and updates local transforms")
{
	EditorFixture fixture;
	const Uuid entityUuid = fixture.CreateEntity("Transformable");

	CommandResult result = fixture.ExecuteForEntity("transform.set",
	                                                entityUuid,
	                                                makeParams({ { "position", numberArray({ 1.0f, 2.0f, 3.0f }) },
	                                                             { "rotation", numberArray({ 0.0f, 90.0f, 0.0f }) },
	                                                             { "scale", numberArray({ 2.0f, 3.0f, 4.0f }) } }));
	REQUIRE(result.Success);

	Entity entity = fixture.ScenePtr->FindEntity(entityUuid);
	REQUIRE(entity.HasComponent<TransformComponent>());
	TransformComponent* transform = entity.TryGetComponent<TransformComponent>();
	REQUIRE(transform != nullptr);
	CHECK(transform->LocalPosition.x == doctest::Approx(1.0f));
	CHECK(transform->LocalPosition.y == doctest::Approx(2.0f));
	CHECK(transform->LocalPosition.z == doctest::Approx(3.0f));
	CHECK(transform->GetLocalEulerAngles().y == doctest::Approx(90.0f).epsilon(0.001));
	CHECK(transform->LocalScale.z == doctest::Approx(4.0f));

	result = fixture.ExecuteForEntity(
	    "transform.set",
	    entityUuid,
	    makeParams({ { "rotationQuaternion", numberArray({ 0.0f, 0.0f, 0.7071068f, 0.7071068f }) } }));
	REQUIRE(result.Success);
	CHECK(transform->LocalRotation.z == doctest::Approx(0.7071068f).epsilon(0.001));
	CHECK(transform->LocalRotation.w == doctest::Approx(0.7071068f).epsilon(0.001));

	const glm::vec3 previousPosition = transform->LocalPosition;
	result = fixture.ExecuteForEntity("transform.set",
	                                  entityUuid,
	                                  makeParams({ { "position", numberArray({ 9.0f, 9.0f }) } }));
	CHECK_FALSE(result.Success);
	CHECK(transform->LocalPosition == previousPosition);

	result =
	    fixture.ExecuteForEntity("transform.set",
	                             entityUuid,
	                             makeParams({ { "rotation", numberArray({ 0.0f, 10.0f, 0.0f }) },
	                                          { "rotationQuaternion", numberArray({ 0.0f, 0.0f, 0.0f, 1.0f }) } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("not both") != std::string::npos);

	result = fixture.ExecuteForEntity("transform.set", entityUuid);
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("position") != std::string::npos);

	result = fixture.Execute(
	    "transform.set",
	    makeParams({ { "uuid", uuidJson(Uuid::Generate()) }, { "position", numberArray({ 1.0f, 1.0f, 1.0f }) } }));
	CHECK_FALSE(result.Success);
}

TEST_CASE("EditorCommands: component add, get, set, setMany and remove")
{
	EditorFixture fixture;
	const Uuid entityUuid = fixture.CreateEntity("Component Host");

	CommandResult result = fixture.ExecuteForEntity("component.add", entityUuid, makeParams({ { "type", "camera" } }));
	REQUIRE(result.Success);
	CHECK(result.Value.GetFloat("fieldOfView") == doctest::Approx(45.0f));

	result = fixture.ExecuteForEntity("component.add", entityUuid, makeParams({ { "type", "camera" } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("already has") != std::string::npos);

	result = fixture.ExecuteForEntity("component.get", entityUuid, makeParams({ { "type", "camera" } }));
	REQUIRE(result.Success);
	CHECK(result.Value.GetString("projection") == "perspective");

	result = fixture.ExecuteForEntity(
	    "component.set",
	    entityUuid,
	    makeParams({ { "type", "camera" }, { "property", "fieldOfView" }, { "value", 60.0f } }));
	REQUIRE(result.Success);
	CHECK(result.Value.GetFloat("fieldOfView") == doctest::Approx(60.0f));

	result = fixture.ExecuteForEntity(
	    "component.set",
	    entityUuid,
	    makeParams({ { "type", "camera" }, { "property", "fieldOfView" }, { "value", 200.0f } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("179") != std::string::npos);
	CHECK(fixture.ScenePtr->FindEntity(entityUuid).GetComponent<CameraComponent>().FieldOfView ==
	      doctest::Approx(60.0f));

	result = fixture.ExecuteForEntity(
	    "component.setMany",
	    entityUuid,
	    makeParams(
	        { { "type", "camera" }, { "values", makeParams({ { "nearClip", 0.5f }, { "farClip", 100.0f } }) } }));
	REQUIRE(result.Success);
	CHECK(result.Value.GetFloat("nearClip") == doctest::Approx(0.5f));
	CHECK(result.Value.GetFloat("farClip") == doctest::Approx(100.0f));

	// The second property would violate nearClip < farClip, so the whole set must
	// be rejected without touching fieldOfView.
	result = fixture.ExecuteForEntity(
	    "component.setMany",
	    entityUuid,
	    makeParams(
	        { { "type", "camera" }, { "values", makeParams({ { "fieldOfView", 90.0f }, { "nearClip", 5000.0f } }) } }));
	CHECK_FALSE(result.Success);
	const CameraComponent& camera = fixture.ScenePtr->FindEntity(entityUuid).GetComponent<CameraComponent>();
	CHECK(camera.FieldOfView == doctest::Approx(60.0f));
	CHECK(camera.NearClip == doctest::Approx(0.5f));

	result = fixture.ExecuteForEntity("component.add", entityUuid, makeParams({ { "type", "light" } }));
	REQUIRE(result.Success);
	result =
	    fixture.ExecuteForEntity("component.set",
	                             entityUuid,
	                             makeParams({ { "type", "light" }, { "property", "intensity" }, { "value", -1.0f } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("at least 0") != std::string::npos);

	result = fixture.ExecuteForEntity(
	    "component.set",
	    entityUuid,
	    makeParams({ { "type", "light" }, { "property", "outerConeDegrees" }, { "value", 10.0f } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("innerConeDegrees") != std::string::npos);

	result =
	    fixture.ExecuteForEntity("component.set",
	                             entityUuid,
	                             makeParams({ { "type", "camera" }, { "property", "wibble" }, { "value", 1.0f } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("fieldOfView") != std::string::npos);

	result = fixture.ExecuteForEntity("component.add", entityUuid, makeParams({ { "type", "sparkles" } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("camera") != std::string::npos);
	CHECK(result.Error.find("flyCamera") != std::string::npos);

	result = fixture.ExecuteForEntity("component.remove", entityUuid, makeParams({ { "type", "camera" } }));
	REQUIRE(result.Success);
	CHECK_FALSE(fixture.ScenePtr->FindEntity(entityUuid).HasComponent<CameraComponent>());
	result = fixture.ExecuteForEntity("component.get", entityUuid, makeParams({ { "type", "camera" } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("no 'camera' component") != std::string::npos);
	result = fixture.ExecuteForEntity("component.remove", entityUuid, makeParams({ { "type", "camera" } }));
	CHECK_FALSE(result.Success);
}

TEST_CASE("EditorCommands: component validation covers every supported type")
{
	EditorFixture fixture;
	const Uuid entityUuid = fixture.CreateEntity("Validation Host");
	const std::pair<std::string_view, std::string_view> types[] = {
		{ "camera", "fieldOfView" },    { "light", "intensity" },   { "meshRenderer", "visible" },
		{ "rigidBody", "mass" },        { "collider", "radius" },   { "audioSource", "volume" },
		{ "audioListener", "primary" }, { "luaScript", "enabled" }, { "flyCamera", "moveSpeed" },
	};
	for (const auto& [type, property] : types)
	{
		CAPTURE(type);
		REQUIRE(fixture.ExecuteForEntity("component.add", entityUuid, makeParams({ { "type", std::string(type) } }))
		            .Success);
		REQUIRE(fixture.ExecuteForEntity("component.get", entityUuid, makeParams({ { "type", std::string(type) } }))
		            .Value.Contains(property));
	}
	CHECK(fixture.ScenePtr->FindEntity(entityUuid).GetComponent<AudioSourceComponent>().Volume ==
	      doctest::Approx(1.0f));
}

TEST_CASE("EditorCommands: selection, screenshot and stats")
{
	EditorFixture fixture;
	const Uuid entityUuid = fixture.CreateEntity("Selected Entity");

	CommandResult result = fixture.Execute("editor.selection.set", makeParams({ { "uuid", uuidJson(entityUuid) } }));
	REQUIRE(result.Success);
	CHECK(fixture.Selection == entityUuid);

	result = fixture.Execute("editor.selection.get");
	REQUIRE(result.Success);
	CHECK(result.Value.GetString("uuid") == entityUuid.ToString());

	result = fixture.Execute("editor.selection.set", makeParams({ { "uuid", JsonValue(nullptr) } }));
	REQUIRE(result.Success);
	CHECK(fixture.Selection.IsNull());
	CHECK(result.Value["uuid"].IsNull());

	result = fixture.Execute("editor.selection.set", makeParams({ { "uuid", uuidJson(Uuid::Generate()) } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("no entity") != std::string::npos);

	result = fixture.Execute("editor.selection.set", makeParams({ { "uuid", "not-a-uuid" } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("not a valid UUID") != std::string::npos);

	result = fixture.Execute("editor.screenshot", makeParams({ { "path", "shots/command-test.png" } }));
	REQUIRE(result.Success);
	CHECK(result.Value.GetBool("requested"));
	CHECK(fixture.ScreenshotPath.find("command-test.png") != std::string::npos);
	CHECK(result.Value.GetString("path") == fixture.ScreenshotPath);

	result = fixture.Execute("editor.screenshot");
	CHECK_FALSE(result.Success);

	result = fixture.Execute("editor.stats");
	REQUIRE(result.Success);
	CHECK(result.Value["fps"].IsNull());
	CHECK(result.Value.GetString("gpuName") == "unavailable");
	CHECK(result.Value.GetUInt("entityCount") == 2);
}

TEST_CASE("EditorCommands: console.logs returns recent messages")
{
	EditorFixture fixture;
	Log::ClearRecentMessages();
	AE_CORE_INFO("EditorCommands console marker {}", 42);

	const CommandResult result = fixture.Execute("console.logs", makeParams({ { "limit", 10 } }));
	REQUIRE(result.Success);
	REQUIRE(result.Value.IsArray());
	CHECK(result.Value.GetSize() <= 10);

	bool foundMarker = false;
	for (usize index = 0; index < result.Value.GetSize(); ++index)
	{
		const JsonValue& entry = result.Value[index];
		CHECK(entry.Contains("level"));
		CHECK(entry.Contains("logger"));
		CHECK(entry.Contains("timestamp"));
		CHECK_FALSE(entry.GetString("timestamp").empty());
		if (entry.GetString("message").find("EditorCommands console marker 42") != std::string::npos)
		{
			foundMarker = true;
			CHECK(entry.GetString("level") == "INFO");
		}
	}
	CHECK(foundMarker);

	const CommandResult badLimit = fixture.Execute("console.logs", makeParams({ { "limit", 0 } }));
	CHECK_FALSE(badLimit.Success);
	CHECK(badLimit.Error.find("between 1 and") != std::string::npos);
}

TEST_CASE("EditorCommands: scene snapshot, save, new and load round-trip")
{
	EditorFixture fixture;
	const Uuid markerUuid = fixture.CreateEntity("Snapshot Marker");

	CommandResult result = fixture.Execute("scene.snapshot");
	REQUIRE(result.Success);
	CHECK(result.Value.GetInt("version") == SceneSerializer::c_CurrentVersion);
	const JsonValue& entities = result.Value["entities"];
	REQUIRE(entities.IsArray());
	CHECK(entities.GetSize() == 2);
	bool markerFound = false;
	for (usize index = 0; index < entities.GetSize(); ++index)
	{
		if (entities[index].GetString("uuid") == markerUuid.ToString())
		{
			markerFound = true;
			CHECK(entities[index].GetString("name") == "Snapshot Marker");
		}
	}
	CHECK(markerFound);

	const std::filesystem::path scenePath =
	    FileSystem::GetCurrentWorkingDirectory() / "TestAssets" / "editor-commands-roundtrip.aescene";
	FileSystem::RemoveFile(scenePath);
	result = fixture.Execute("scene.save", makeParams({ { "path", FileSystem::ToUtf8(scenePath) } }));
	REQUIRE(result.Success);
	CHECK(result.Value.GetBool("saved"));
	CHECK(FileSystem::IsFile(scenePath));

	const Uuid originalSceneUuid = fixture.ScenePtr->GetUuid();
	result = fixture.Execute("scene.new", makeParams({ { "name", "Fresh Scene" } }));
	REQUIRE(result.Success);
	CHECK(fixture.ScenePtr->GetName() == "Fresh Scene");
	CHECK(fixture.ScenePtr->GetUuid() != originalSceneUuid);
	CHECK(fixture.ScenePtr->GetEntityCount() == 1);
	CHECK(fixture.ScenePtr->GetPrimaryCamera().IsValid());
	CHECK(fixture.Selection.IsNull());

	result = fixture.Execute("scene.load", makeParams({ { "path", FileSystem::ToUtf8(scenePath) } }));
	REQUIRE(result.Success);
	CHECK(fixture.LoadedPath == FileSystem::ToUtf8(scenePath));
	CHECK(fixture.ScenePtr->GetName() == "Command Test Scene");
	CHECK(fixture.ScenePtr->FindEntity(markerUuid).IsValid());

	result = fixture.Execute("scene.save");
	REQUIRE(result.Success);
	CHECK(result.Value.GetBool("saved"));
	CHECK(FileSystem::IsFile(FileSystem::GetCurrentWorkingDirectory() / "TestAssets" /
	                         "editor-commands-callback.aescene"));

	result = fixture.Execute("scene.load");
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("path") != std::string::npos);
}

TEST_CASE("EditorCommands: scene.load falls back to the serializer without editor callbacks")
{
	EditorFixture fixture;
	const Uuid markerUuid = fixture.CreateEntity("Fallback Marker");
	const std::filesystem::path scenePath =
	    FileSystem::GetCurrentWorkingDirectory() / "TestAssets" / "editor-commands-fallback.aescene";
	FileSystem::RemoveFile(scenePath);
	REQUIRE(fixture.Execute("scene.save", makeParams({ { "path", FileSystem::ToUtf8(scenePath) } })).Success);

	// A headless context: no renderer, no save/load/screenshot callbacks. The
	// command layer must still load a scene file directly.
	EditorCommandContext bareContext;
	bareContext.Scene = fixture.ScenePtr.get();
	bareContext.Selection = &fixture.Selection;
	bareContext.ProjectRoot = FileSystem::ToUtf8(FileSystem::GetCurrentWorkingDirectory());
	RegisterEditorCommands(bareContext);

	REQUIRE(fixture.Execute("scene.new", makeParams({ { "name", "Reset Scene" } })).Success);
	CHECK_FALSE(fixture.ScenePtr->FindEntity(markerUuid).IsValid());

	CommandResult result = fixture.Execute("scene.load", makeParams({ { "path", FileSystem::ToUtf8(scenePath) } }));
	REQUIRE(result.Success);
	CHECK(fixture.ScenePtr->FindEntity(markerUuid).IsValid());

	result = fixture.Execute("scene.save");
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("SaveScene") != std::string::npos);

	result = fixture.Execute("editor.screenshot", makeParams({ { "path", "shots/fallback.png" } }));
	CHECK_FALSE(result.Success);
	CHECK(result.Error.find("ScreenshotRequest") != std::string::npos);
}
