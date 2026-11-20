#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Json.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Physics/PhysicsWorld.h"
#include "Aegis/Scene/Scene.h"
#include "Aegis/Scripting/LuaEngine.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace aegis;

namespace
{
// Unit tests may only touch the build tree; the test executable's working
// directory is the build's bin folder under CTest.
const std::filesystem::path c_ScriptDirectory = std::filesystem::current_path() / "TestAssets" / "Scripting";

std::filesystem::path PrepareDirectory(std::string_view name)
{
	const std::filesystem::path directory = c_ScriptDirectory / std::string(name);
	FileSystem::DeleteDirectory(directory);
	if (!FileSystem::CreateDirectories(directory))
	{
		throw std::runtime_error("could not create a scripting test directory");
	}
	return directory;
}

std::filesystem::path
WriteScript(const std::filesystem::path& directory, std::string_view fileName, std::string_view source)
{
	const std::filesystem::path path = directory / std::string(fileName);
	if (!FileSystem::WriteTextFile(path, source))
	{
		throw std::runtime_error("could not write a scripting test script");
	}
	return path;
}

std::filesystem::path SingleScript(std::string_view name, std::string_view source)
{
	return WriteScript(PrepareDirectory(name), "script.lua", source);
}

void RewriteScript(const std::filesystem::path& path, std::string_view source)
{
	if (!FileSystem::WriteTextFile(path, source))
	{
		throw std::runtime_error("could not rewrite a scripting test script");
	}
	// File timestamps can be coarse, so push the modification time forward to
	// make the hot-reload change observable even when the write lands in the
	// same clock tick as the previous one.
	std::error_code error;
	const auto current = std::filesystem::last_write_time(path, error);
	if (!error)
	{
		std::filesystem::last_write_time(path, current + std::chrono::seconds(5), error);
	}
}

std::vector<LogMessage> MessagesContaining(std::string_view needle, LogLevel level)
{
	std::vector<LogMessage> messages;
	for (const LogMessage& message : Log::GetRecentMessages())
	{
		if (message.Level == level && std::string_view(message.Message).find(needle) != std::string_view::npos)
		{
			messages.push_back(message);
		}
	}
	return messages;
}

std::size_t CountErrorsContaining(std::string_view needle)
{
	return MessagesContaining(needle, LogLevel::Error).size();
}

i64 IndexOfMessage(std::string_view needle)
{
	const std::vector<LogMessage> messages = Log::GetRecentMessages();
	for (usize index = 0; index < messages.size(); ++index)
	{
		if (std::string_view(messages[index].Message).find(needle) != std::string_view::npos)
		{
			return static_cast<i64>(index);
		}
	}
	return -1;
}

// Drives the engine through the real Scene::OnUpdate path so transforms,
// physics (when present) and scripting run in their registered order.
struct ScriptFixture
{
	Scene Scene;
	LuaEngine Scripting;

	ScriptFixture()
	{
		if (!Scripting.Initialize(Scene))
		{
			throw std::runtime_error("the Lua scripting engine could not be initialized");
		}
		Scripting.SetReloadCheckInterval(0.0f);
		Scene.AddSystem(&Scripting);
	}

	~ScriptFixture()
	{
		Scene.RemoveSystem(&Scripting);
		Scripting.Shutdown();
	}

	Entity AddScriptEntity(const std::filesystem::path& path, const std::string& name = "Scripted")
	{
		Entity entity = Scene.CreateEntity(name);
		LuaScriptComponent& component = entity.AddComponent<LuaScriptComponent>();
		component.Script.Id = Uuid::Generate();
		component.Script.Type = AssetType::Script;
		Scripting.RegisterScript(component.Script, path);
		return entity;
	}

	void Step(f32 delta = 1.0f / 60.0f)
	{
		Scene.OnUpdate(Timestep(delta));
	}
};

f32 NumberProperty(const Entity& entity, std::string_view key)
{
	return entity.GetComponent<LuaScriptComponent>().Properties.GetFloat(key);
}

bool BoolProperty(const Entity& entity, std::string_view key)
{
	return entity.GetComponent<LuaScriptComponent>().Properties.GetBool(key);
}

std::string StringProperty(const Entity& entity, std::string_view key)
{
	return entity.GetComponent<LuaScriptComponent>().Properties.GetString(key);
}
} // namespace

TEST_CASE("Scripting: OnStart runs once and OnUpdate receives the timestep")
{
	ScriptFixture fixture;
	const std::filesystem::path path = SingleScript("OnStartUpdate", R"lua(
function OnStart(entity)
	engine.properties.starts = (engine.properties.starts or 0) + 1
end

function OnUpdate(entity, dt)
	engine.properties.updates = (engine.properties.updates or 0) + 1
	engine.properties.lastDt = dt
end
)lua");

	Entity entity = fixture.AddScriptEntity(path, "Ticker");

	fixture.Step();
	CHECK(NumberProperty(entity, "starts") == doctest::Approx(1.0f));
	CHECK(NumberProperty(entity, "updates") == doctest::Approx(1.0f));
	CHECK(NumberProperty(entity, "lastDt") == doctest::Approx(1.0f / 60.0f));

	fixture.Step();
	CHECK(NumberProperty(entity, "starts") == doctest::Approx(1.0f));
	CHECK(NumberProperty(entity, "updates") == doctest::Approx(2.0f));
	CHECK(fixture.Scripting.GetInstanceCount() == 1);
	CHECK(fixture.Scripting.GetElapsedTime() == doctest::Approx(2.0 / 60.0));
	CHECK(fixture.Scripting.GetErrorCount() == 0);
}

TEST_CASE("Scripting: each instance keeps its own environment")
{
	ScriptFixture fixture;
	const std::filesystem::path path = SingleScript("Isolation", R"lua(
local counter = 0

function OnStart(entity)
	counter = counter + 1
	engine.properties.counter = counter
end
)lua");

	Entity first = fixture.AddScriptEntity(path, "First");
	Entity second = fixture.AddScriptEntity(path, "Second");

	fixture.Step();
	CHECK(NumberProperty(first, "counter") == doctest::Approx(1.0f));
	CHECK(NumberProperty(second, "counter") == doctest::Approx(1.0f));
	CHECK(fixture.Scripting.GetInstanceCount() == 2);

	fixture.Step();
	CHECK(NumberProperty(first, "counter") == doctest::Approx(1.0f));
	CHECK(NumberProperty(second, "counter") == doctest::Approx(1.0f));
}

TEST_CASE("Scripting: a reused entity uuid starts a fresh instance")
{
	ScriptFixture fixture;
	const std::filesystem::path scriptPath = SingleScript("ReusedUuid", R"lua(
function OnStart(entity)
	engine.properties.starts = (engine.properties.starts or 0) + 1
	engine.properties.entityName = entity:name()
end
)lua");

	const Uuid uuid = Uuid::Generate();
	Entity first = fixture.Scene.CreateEntity(uuid, "First");
	LuaScriptComponent& firstComponent = first.AddComponent<LuaScriptComponent>();
	firstComponent.Script.Id = Uuid::Generate();
	firstComponent.Script.Type = AssetType::Script;
	fixture.Scripting.RegisterScript(firstComponent.Script, scriptPath);

	fixture.Step();
	CHECK(NumberProperty(first, "starts") == doctest::Approx(1.0f));

	// A scene load destroys the entities and recreates them with their persisted
	// UUIDs, so the new entity must not inherit the old script instance.
	fixture.Scene.DestroyEntity(first);
	Entity second = fixture.Scene.CreateEntity(uuid, "Second");
	LuaScriptComponent& secondComponent = second.AddComponent<LuaScriptComponent>();
	secondComponent.Script.Id = Uuid::Generate();
	secondComponent.Script.Type = AssetType::Script;
	fixture.Scripting.RegisterScript(secondComponent.Script, scriptPath);

	fixture.Step();
	CHECK(NumberProperty(second, "starts") == doctest::Approx(1.0f));
	CHECK(StringProperty(second, "entityName") == "Second");
	CHECK(fixture.Scripting.GetInstanceCount() == 1);
}

TEST_CASE("Scripting: entity transform and name bindings")
{
	ScriptFixture fixture;
	const std::filesystem::path path = SingleScript("Transform", R"lua(
function OnStart(entity)
	entity:setPosition({ x = 1.5, y = 2.0, z = -3.0 })
	entity:setRotation({ 10, 20, 30 })
	entity:setScale({ x = 2.0, y = 2.0, z = 2.0 })
	entity:setName("Moved")
	engine.properties.uuid = entity:uuid()
	engine.properties.rotationY = entity:rotation().y
	engine.properties.scaleX = entity:scale().x
end

function OnUpdate(entity, dt)
	engine.properties.worldY = entity:worldPosition().y
	engine.properties.active = entity:isActive()
	entity:setActive(false)
	engine.properties.inactive = not entity:isActive()
	entity:setActive(true)
end
)lua");

	Entity entity = fixture.AddScriptEntity(path, "Mover");
	REQUIRE(entity.IsValid());

	fixture.Step();

	const TransformComponent& transform = entity.GetComponent<TransformComponent>();
	CHECK(transform.LocalPosition.x == doctest::Approx(1.5f));
	CHECK(transform.LocalPosition.y == doctest::Approx(2.0f));
	CHECK(transform.LocalPosition.z == doctest::Approx(-3.0f));
	CHECK(transform.LocalScale.x == doctest::Approx(2.0f));
	CHECK(entity.GetName() == "Moved");
	CHECK(StringProperty(entity, "uuid") == entity.GetUuid().ToString());
	CHECK(NumberProperty(entity, "rotationY") == doctest::Approx(20.0f).epsilon(0.001));
	CHECK(NumberProperty(entity, "scaleX") == doctest::Approx(2.0f));
	CHECK(BoolProperty(entity, "active"));

	fixture.Step();
	CHECK(BoolProperty(entity, "inactive"));
	// Scene::OnUpdate refreshes world transforms after the systems run, so the
	// position set during the first frame is visible through worldPosition() from
	// the second frame on.
	CHECK(NumberProperty(entity, "worldY") == doctest::Approx(2.0f));
}

TEST_CASE("Scripting: spawning and destroying entities")
{
	ScriptFixture fixture;
	Entity target = fixture.Scene.CreateEntity("Target");

	const std::filesystem::path path = SingleScript("Spawning", R"lua(
function OnStart(entity)
	local victim = engine.findEntityByName("Target")
	engine.properties.destroyed = engine.destroyEntity(victim)

	local spawned = engine.createEntity("Spawned")
	engine.properties.spawnedUuid = spawned:uuid()
	engine.properties.spawnedValid = spawned:isValid()
	engine.properties.entityCount = #engine.entities()
	engine.properties.foundByUuid = engine.findEntity(spawned:uuid()) ~= nil
	engine.properties.missing = engine.findEntityByName("Nope") == nil
end
)lua");
	Entity scripted = fixture.AddScriptEntity(path, "Spawner");

	fixture.Step();

	CHECK_FALSE(fixture.Scene.FindEntity("Target").IsValid());
	CHECK_FALSE(target.IsValid());

	Entity spawned = fixture.Scene.FindEntity("Spawned");
	REQUIRE(spawned.IsValid());
	CHECK(StringProperty(scripted, "spawnedUuid") == spawned.GetUuid().ToString());
	CHECK(BoolProperty(scripted, "spawnedValid"));
	CHECK(NumberProperty(scripted, "entityCount") == doctest::Approx(2.0f));
	CHECK(BoolProperty(scripted, "foundByUuid"));
	CHECK(BoolProperty(scripted, "missing"));
}

TEST_CASE("Scripting: OnDestroy runs before a script destroys an entity")
{
	ScriptFixture fixture;
	const std::filesystem::path directory = PrepareDirectory("OnDestroy");

	const std::filesystem::path targetScript = WriteScript(directory, "target.lua", R"lua(
function OnStart(entity)
	engine.properties.started = true
end

function OnDestroy(entity)
	engine.log.info("target-ondestroy")
end
)lua");
	const std::filesystem::path destroyerScript = WriteScript(directory, "destroyer.lua", R"lua(
function OnStart(entity)
	local target = engine.findEntityByName("Target")
	target:destroy()
	engine.log.info("destroy-returned")
	engine.properties.targetGone = engine.findEntityByName("Target") == nil
end
)lua");

	Entity target = fixture.AddScriptEntity(targetScript, "Target");
	Entity destroyer = fixture.AddScriptEntity(destroyerScript, "Destroyer");
	REQUIRE(target.IsValid());
	REQUIRE(destroyer.IsValid());

	Log::ClearRecentMessages();
	fixture.Step();

	CHECK_FALSE(fixture.Scene.FindEntity("Target").IsValid());
	CHECK(BoolProperty(destroyer, "targetGone"));
	CHECK(fixture.Scripting.GetInstanceCount() == 1);

	// OnDestroy has to run inside the destroy call, before control returns to the
	// script that asked for it.
	const i64 destroyedAt = IndexOfMessage("target-ondestroy");
	const i64 returnedAt = IndexOfMessage("destroy-returned");
	REQUIRE(destroyedAt >= 0);
	REQUIRE(returnedAt >= 0);
	CHECK(destroyedAt < returnedAt);

	// The same contract holds when the host removes the entity: the retirement
	// sweep at the start of the next update calls OnDestroy exactly once.
	const std::filesystem::path victimScript = WriteScript(directory, "victim.lua", R"lua(
function OnDestroy(entity)
	engine.log.info("victim-ondestroy")
end
)lua");
	Entity victim = fixture.AddScriptEntity(victimScript, "Victim");
	fixture.Step();
	CHECK(IndexOfMessage("victim-ondestroy") < 0);

	Log::ClearRecentMessages();
	fixture.Scene.DestroyEntity(victim);
	fixture.Step();
	CHECK(fixture.Scripting.GetInstanceCount() == 1);
	CHECK(MessagesContaining("victim-ondestroy", LogLevel::Info).size() == 1);
}

TEST_CASE("Scripting: component tables expose validated fields")
{
	ScriptFixture fixture;
	const std::filesystem::path path = SingleScript("Components", R"lua(
function OnStart(entity)
	engine.properties.added = entity:addComponent("camera")
	local camera = entity:getComponent("camera")
	camera.fov = 500
	camera.nearClip = -5
	camera.clearColor = { r = 2, g = 0.5, b = 0.25, a = 1 }
	engine.properties.set = entity:setComponent("camera", camera)

	local stored = entity:getComponent("camera")
	engine.properties.fov = stored.fieldOfView
	engine.properties.nearClip = stored.nearClip
	engine.properties.red = stored.clearColor.x
	engine.properties.hasCamera = entity:hasComponent("Camera")

	engine.properties.partial = entity:setComponent("camera", { primary = true })
	local cameraAfter = entity:getComponent("camera")
	engine.properties.keptFov = cameraAfter.fieldOfView
	engine.properties.primary = cameraAfter.primary

	engine.properties.addedLight = entity:addComponent("light")
	engine.properties.hasLight = entity:hasComponent("light")
	engine.properties.removedLight = entity:removeComponent("light")
	engine.properties.missingLight = entity:getComponent("light") == nil
end
)lua");

	Entity entity = fixture.AddScriptEntity(path, "Components");
	fixture.Step();

	REQUIRE(entity.HasComponent<CameraComponent>());
	const CameraComponent& camera = entity.GetComponent<CameraComponent>();
	CHECK(camera.FieldOfView == doctest::Approx(179.0f));
	CHECK(camera.NearClip == doctest::Approx(0.001f));
	CHECK(camera.ClearColor.x == doctest::Approx(1.0f));
	CHECK(camera.Primary);
	CHECK_FALSE(entity.HasComponent<LightComponent>());

	CHECK(BoolProperty(entity, "added"));
	CHECK(BoolProperty(entity, "set"));
	CHECK(BoolProperty(entity, "hasCamera"));
	CHECK(BoolProperty(entity, "partial"));
	CHECK(BoolProperty(entity, "primary"));
	CHECK(BoolProperty(entity, "addedLight"));
	CHECK(BoolProperty(entity, "hasLight"));
	CHECK(BoolProperty(entity, "removedLight"));
	CHECK(BoolProperty(entity, "missingLight"));
	CHECK(NumberProperty(entity, "fov") == doctest::Approx(179.0f));
	CHECK(NumberProperty(entity, "keptFov") == doctest::Approx(179.0f));
	CHECK(NumberProperty(entity, "nearClip") == doctest::Approx(0.001f));
	CHECK(NumberProperty(entity, "red") == doctest::Approx(1.0f));
}

TEST_CASE("Scripting: properties round trip through JsonValue")
{
	ScriptFixture fixture;
	const std::filesystem::path path = SingleScript("Properties", R"lua(
function OnStart(entity)
	engine.properties.initial = engine.properties.initial + 1
	engine.properties.speed = 2.5
	engine.properties.label = "hero"
	engine.properties.alive = true
	engine.properties.tags = { "a", "b" }
	engine.properties.nested = { inner = 3 }
	engine.properties.removed = nil
end

function OnUpdate(entity, dt)
	engine.properties.ticked = (engine.properties.ticked or 0) + 1
end
)lua");

	Entity entity = fixture.AddScriptEntity(path, "Properties");
	LuaScriptComponent& component = entity.GetComponent<LuaScriptComponent>();
	JsonValue initial = JsonValue::MakeObject();
	initial.Set("initial", 7);
	initial.Set("removed", true);
	component.Properties = initial;

	fixture.Step();
	fixture.Step();

	const JsonValue& properties = entity.GetComponent<LuaScriptComponent>().Properties;
	REQUIRE(properties.IsObject());
	CHECK(properties["initial"].AsInt() == 8);
	CHECK(properties["speed"].AsFloat() == doctest::Approx(2.5f));
	CHECK(properties["label"].AsString() == "hero");
	CHECK(properties["alive"].AsBool());
	REQUIRE(properties["tags"].IsArray());
	CHECK(properties["tags"].GetSize() == 2);
	CHECK(properties["tags"][0].AsString() == "a");
	CHECK(properties["nested"]["inner"].AsInt() == 3);
	CHECK_FALSE(properties.Contains("removed"));
	CHECK(properties["ticked"].AsInt() == 2);

	// The component contract requires an object document: the serializer must be
	// able to write and read back what the script produced.
	const JsonValue reparsed = JsonValue::ParseOrEmpty(properties.Dump(), "scripting test");
	REQUIRE(reparsed.IsObject());
	CHECK(reparsed["label"].AsString() == "hero");
}

TEST_CASE("Scripting: scene, time, input, log and audio bindings")
{
	ScriptFixture fixture;
	const std::filesystem::path path = SingleScript("Bindings", R"lua(
function OnStart(entity)
	engine.properties.sceneName = engine.scene.name
	engine.scene.name = "Renamed By Script"
	engine.log.info("hello %s %d", "world", 42)
	engine.log.warn("warn %s", "message")
	engine.properties.played = engine.playSound("missing.wav", 1.0)
end

function OnUpdate(entity, dt)
	engine.properties.delta = engine.time.delta
	engine.properties.elapsed = engine.time.elapsed
	engine.properties.keyDown = engine.input.isKeyDown("W")
	local mouse = engine.input.mousePosition()
	engine.properties.mouseX = mouse.x
	engine.properties.mouseY = mouse.y
end
)lua");

	Entity entity = fixture.AddScriptEntity(path, "Bindings");
	Log::ClearRecentMessages();
	fixture.Step();
	fixture.Step();

	CHECK(fixture.Scene.GetName() == "Renamed By Script");
	CHECK(StringProperty(entity, "sceneName") == "Untitled Scene");
	CHECK_FALSE(BoolProperty(entity, "played"));
	CHECK_FALSE(BoolProperty(entity, "keyDown"));
	CHECK(NumberProperty(entity, "delta") == doctest::Approx(1.0f / 60.0f));
	CHECK(NumberProperty(entity, "elapsed") == doctest::Approx(2.0f / 60.0f));
	CHECK(NumberProperty(entity, "mouseX") == doctest::Approx(0.0f));
	CHECK(NumberProperty(entity, "mouseY") == doctest::Approx(0.0f));

	CHECK(MessagesContaining("hello world 42", LogLevel::Info).size() == 1);
	CHECK(MessagesContaining("warn message", LogLevel::Warn).size() == 1);
}

TEST_CASE("Scripting: hierarchy bindings parent and list children")
{
	ScriptFixture fixture;
	const std::filesystem::path path = SingleScript("Hierarchy", R"lua(
function OnStart(entity)
	local child = engine.createEntity("Child")
	child:parent(entity)
	engine.properties.parentUuid = child:parent():uuid()
	engine.properties.childCount = #entity:children()
	engine.properties.selfByUuid = engine.findEntity(entity:uuid()) ~= nil
	engine.properties.missingUuid = engine.findEntity("not-a-uuid") == nil
	engine.properties.total = #engine.entities()

	child:parent(nil)
	engine.properties.detached = child:parent() == nil
	child:destroy()
	engine.properties.childAlive = child:isValid()
end
)lua");

	Entity parent = fixture.AddScriptEntity(path, "Parent");
	fixture.Step();

	CHECK(StringProperty(parent, "parentUuid") == parent.GetUuid().ToString());
	CHECK(NumberProperty(parent, "childCount") == doctest::Approx(1.0f));
	CHECK(BoolProperty(parent, "selfByUuid"));
	CHECK(BoolProperty(parent, "missingUuid"));
	CHECK(NumberProperty(parent, "total") == doctest::Approx(2.0f));
	CHECK(BoolProperty(parent, "detached"));
	CHECK_FALSE(BoolProperty(parent, "childAlive"));
	CHECK(fixture.Scene.GetEntities().size() == 1);
}

TEST_CASE("Scripting: syntax errors are logged, contained and recovered from")
{
	ScriptFixture fixture;
	const std::filesystem::path path = SingleScript("SyntaxError", R"lua(
function OnStart(entity)
	engine.properties.broken = true
)lua");

	Entity entity = fixture.AddScriptEntity(path, "Broken");
	Log::ClearRecentMessages();

	fixture.Step();
	fixture.Step();
	fixture.Step();

	CHECK(entity.IsValid());
	CHECK(fixture.Scripting.GetInstanceCount() == 1);
	CHECK(fixture.Scripting.GetElapsedTime() > 0.0);
	// A compile failure is reported once; it must not repeat every frame.
	CHECK(MessagesContaining("failed to compile", LogLevel::Error).size() == 1);
	CHECK_FALSE(entity.GetComponent<LuaScriptComponent>().Properties.Contains("broken"));

	// Fixing the file on disk reloads and starts the script.
	RewriteScript(path, R"lua(
function OnStart(entity)
	engine.properties.fixed = true
end
)lua");
	Log::ClearRecentMessages();
	fixture.Step();
	CHECK(BoolProperty(entity, "fixed"));
	CHECK(fixture.Scripting.GetInstanceCount() == 1);
}

TEST_CASE("Scripting: runtime errors log a traceback and disable after five failures")
{
	ScriptFixture fixture;
	const std::filesystem::path path = SingleScript("RuntimeError", R"lua(
function OnUpdate(entity, dt)
	error("boom")
end
)lua");

	Entity entity = fixture.AddScriptEntity(path, "Boomer");
	Log::ClearRecentMessages();

	for (int frame = 0; frame < LuaEngine::c_MaxConsecutiveErrors; ++frame)
	{
		fixture.Step();
	}
	CHECK(CountErrorsContaining("boom") == static_cast<std::size_t>(LuaEngine::c_MaxConsecutiveErrors));
	CHECK(fixture.Scripting.GetErrorCount() == static_cast<u64>(LuaEngine::c_MaxConsecutiveErrors));

	// The log line names the entity and carries the Lua traceback.
	const std::vector<LogMessage> errors = MessagesContaining("boom", LogLevel::Error);
	REQUIRE_FALSE(errors.empty());
	CHECK(errors.front().Message.find("Boomer") != std::string::npos);
	CHECK(errors.front().Message.find("stack traceback") != std::string::npos);
	CHECK(errors.front().Message.find("OnUpdate") != std::string::npos);
	CHECK(MessagesContaining("disabled after", LogLevel::Error).size() == 1);

	// Disabled means disabled: further updates neither call nor log the script.
	for (int frame = 0; frame < 3; ++frame)
	{
		fixture.Step();
	}
	CHECK(CountErrorsContaining("boom") == static_cast<std::size_t>(LuaEngine::c_MaxConsecutiveErrors));
	CHECK(fixture.Scripting.GetErrorCount() == static_cast<u64>(LuaEngine::c_MaxConsecutiveErrors));
	CHECK(entity.IsValid());

	// Unknown component names are explicit errors rather than silent misses.
	const std::filesystem::path bogus = SingleScript("UnknownComponent", R"lua(
function OnStart(entity)
	local missing = entity:getComponent("bogus")
end
)lua");
	fixture.AddScriptEntity(bogus, "Bogus");
	Log::ClearRecentMessages();
	fixture.Step();
	CHECK(MessagesContaining("unknown component type", LogLevel::Error).size() == 1);
}

TEST_CASE("Scripting: a script file change reloads the instance")
{
	ScriptFixture fixture;
	const std::filesystem::path path = SingleScript("Reload", R"lua(
function OnStart(entity)
	engine.properties.version = 1
	engine.properties.persist = "kept"
end

function OnDestroy(entity)
	engine.properties.destroyed = (engine.properties.destroyed or 0) + 1
end
)lua");

	Entity entity = fixture.AddScriptEntity(path, "Reloaded");
	Log::ClearRecentMessages();
	fixture.Step();
	CHECK(NumberProperty(entity, "version") == doctest::Approx(1.0f));
	CHECK(StringProperty(entity, "persist") == "kept");

	RewriteScript(path, R"lua(
function OnStart(entity)
	engine.properties.version = 2
	engine.properties.sawPersist = engine.properties.persist
end

function OnDestroy(entity)
	engine.properties.destroyed = (engine.properties.destroyed or 0) + 1
end
)lua");
	Log::ClearRecentMessages();
	fixture.Step();

	CHECK(NumberProperty(entity, "version") == doctest::Approx(2.0f));
	CHECK(StringProperty(entity, "sawPersist") == "kept");
	CHECK(NumberProperty(entity, "destroyed") == doctest::Approx(1.0f));
	CHECK(MessagesContaining("reloaded", LogLevel::Info).size() == 1);

	// A broken edit must not take down the chunk that currently runs.
	RewriteScript(path, "function OnStart(entity\n");
	Log::ClearRecentMessages();
	fixture.Step();
	CHECK(MessagesContaining("failed to compile", LogLevel::Error).size() == 1);
	CHECK(NumberProperty(entity, "version") == doctest::Approx(2.0f));

	RewriteScript(path, R"lua(
function OnStart(entity)
	engine.properties.version = 4
end
)lua");
	fixture.Step();
	CHECK(NumberProperty(entity, "version") == doctest::Approx(4.0f));
}

TEST_CASE("Scripting: missing script paths are reported once")
{
	ScriptFixture fixture;
	Entity entity = fixture.Scene.CreateEntity("Unbound");
	entity.AddComponent<LuaScriptComponent>();
	Log::ClearRecentMessages();

	fixture.Step();
	fixture.Step();
	fixture.Step();

	CHECK(entity.IsValid());
	CHECK(MessagesContaining("has no script path", LogLevel::Error).size() == 1);
	CHECK(fixture.Scripting.GetInstanceCount() == 1);
}

TEST_CASE("Scripting: the script root resolves scripts by asset uuid")
{
	ScriptFixture fixture;
	const std::filesystem::path directory = PrepareDirectory("ScriptRoot");

	AssetHandle script;
	script.Id = Uuid::Generate();
	script.Type = AssetType::Script;
	const std::filesystem::path path = WriteScript(directory, script.Id.ToString() + ".lua", R"lua(
function OnStart(entity)
	engine.properties.fromRoot = true
end
)lua");
	REQUIRE(FileSystem::Exists(path));

	fixture.Scripting.SetScriptRoot(directory);
	Entity entity = fixture.Scene.CreateEntity("Rooted");
	LuaScriptComponent& component = entity.AddComponent<LuaScriptComponent>();
	component.Script = script;

	fixture.Step();
	CHECK(BoolProperty(entity, "fromRoot"));

	// Unregistering the root convention takes the next update back to "no path".
	fixture.Scripting.SetScriptRoot({});
	CHECK(fixture.Scripting.GetScriptRoot().empty());
}

TEST_CASE("Scripting: the raycast binding reports a physics hit")
{
	ScriptFixture fixture;
	PhysicsWorld physics;
	const bool physicsReady = physics.Initialize();
	if (physicsReady)
	{
		fixture.Scene.AddSystem(&physics);
	}

	Entity floor = fixture.Scene.CreateEntity("Floor");
	floor.GetComponent<TransformComponent>().LocalPosition = glm::vec3(0.0f, -0.5f, 0.0f);
	floor.AddComponent<RigidBodyComponent>();
	floor.AddComponent<ColliderComponent>().HalfExtents = glm::vec3(10.0f, 0.5f, 10.0f);

	const std::filesystem::path path = SingleScript("Raycast", R"lua(
function OnStart(entity)
	local hit = engine.physics.raycast({ x = 0, y = 5, z = 0 }, { x = 0, y = -1, z = 0 }, 20)
	if hit == nil then
		engine.properties.hit = false
		return
	end
	engine.properties.hit = true
	engine.properties.hitUuid = hit.entityUuid
	engine.properties.hitY = hit.point.y
	engine.properties.miss = engine.physics.raycast({ x = 0, y = 5, z = 0 }, { x = 0, y = 1, z = 0 }, 20) == nil
end
)lua");
	Entity scripted = fixture.AddScriptEntity(path, "Raycaster");

	if (physicsReady)
	{
		fixture.Step();
		CHECK(BoolProperty(scripted, "hit"));
		CHECK(StringProperty(scripted, "hitUuid") == floor.GetUuid().ToString());
		CHECK(NumberProperty(scripted, "hitY") == doctest::Approx(0.0f).epsilon(0.001));
		CHECK(BoolProperty(scripted, "miss"));
	}
	else
	{
		// Without a physics device the binding must report a miss instead of
		// failing; the scripting path itself stays fully functional.
		fixture.Step();
		CHECK_FALSE(BoolProperty(scripted, "hit"));
		CHECK(scripted.IsValid());
	}

	fixture.Scene.RemoveSystem(&physics);
	physics.Shutdown();
}