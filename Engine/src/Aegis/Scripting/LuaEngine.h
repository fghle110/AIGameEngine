#pragma once

// Embedded Lua 5.4 scripting. One LuaEngine owns one lua_State and one scripting
// instance per entity that carries a LuaScriptComponent.
//
// A LuaEngine is a SceneSystem: construct it, Initialize(scene), then add it
// with Scene::AddSystem. Update order 100 runs it after physics (-100) and
// before audio (200), so a script sees the physics result of the frame it runs
// in. The editor, the runtime and the tests compose it the same way; nothing in
// the engine creates it implicitly.
//
// Scripts are trusted code: the whole Lua standard library is available and
// there is no sandbox. See docs/SCRIPTING.md for the script API reference.

#include "Aegis/Assets/AssetHandle.h"
#include "Aegis/Core/Base.h"
#include "Aegis/Core/Time.h"
#include "Aegis/Scene/SceneSystem.h"

#include <filesystem>
#include <string_view>

namespace aegis
{
class AudioEngine;
class PhysicsWorld;
class Scene;

class LuaEngine final : public SceneSystem
{
public:
	// A script instance that keeps failing is disabled after this many
	// consecutive callback errors, so a broken OnUpdate cannot flood the log
	// every frame. A successful callback resets the counter; reloading the
	// script clears it as well.
	static constexpr i32 c_MaxConsecutiveErrors = 5;

	// Default seconds between script file timestamp polls. 0 polls every
	// update.
	static constexpr f32 c_DefaultReloadCheckInterval = 0.25f;

	LuaEngine();
	~LuaEngine() override;

	LuaEngine(const LuaEngine&) = delete;
	LuaEngine& operator=(const LuaEngine&) = delete;

	[[nodiscard]] std::string_view GetName() const override
	{
		return "Scripting";
	}
	[[nodiscard]] i32 GetUpdateOrder() const override
	{
		return 100;
	}

	// Creates the lua_State, opens the Lua standard libraries, installs the
	// engine bindings and binds the engine to scene. Calling it again for the
	// same scene is a no-op; for another scene the previous state is shut down
	// first. Returns false only when the Lua state cannot be created.
	[[nodiscard]] bool Initialize(Scene& scene);

	// Calls OnDestroy for every started script and closes the lua_State. Call
	// it before the scene the engine drives is destroyed. Registered scripts
	// and the script root survive, so a host can switch scenes without
	// registering the assets again.
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const;

	// Retires instances whose entity disappeared, starts new instances,
	// reloads changed script files and calls OnUpdate on every live instance.
	// Script errors never escape: they are logged with the entity and a
	// traceback, and the instance is disabled after c_MaxConsecutiveErrors.
	void OnUpdate(Scene& scene, Timestep timestep) override;

	// Script resolution. A registered path always wins. An unregistered handle
	// falls back to "<scriptRoot>/<asset uuid>.lua" when a script root is set,
	// which lets a project ship scripts named after their asset id without a
	// registration step. Registering an empty path removes the registration.
	void RegisterScript(AssetHandle script, const std::filesystem::path& path);
	void UnregisterScript(AssetHandle script);
	void ClearScripts();
	void SetScriptRoot(std::filesystem::path root);
	[[nodiscard]] const std::filesystem::path& GetScriptRoot() const;

	// Optional host subsystems used by the bindings. Without an audio engine
	// engine.playSound() returns false; without a physics world (set here or
	// registered with the scene) engine.physics.raycast() returns nil.
	void SetAudioEngine(AudioEngine* audioEngine);
	[[nodiscard]] AudioEngine* GetAudioEngine() const;
	void SetPhysicsWorld(PhysicsWorld* physicsWorld);
	[[nodiscard]] PhysicsWorld* GetPhysicsWorld() const;

	// Seconds between script file timestamp polls for hot reload. 0 checks
	// every update, which is what the tests and the editor use; positive
	// values trade reload latency for one stat call per instance and poll.
	void SetReloadCheckInterval(f32 seconds);
	[[nodiscard]] f32 GetReloadCheckInterval() const;

	// Statistics for the editor and the tests.
	[[nodiscard]] usize GetInstanceCount() const;
	[[nodiscard]] f64 GetElapsedTime() const;
	[[nodiscard]] u64 GetErrorCount() const;

private:
	struct Impl;
	Scope<Impl> m_Impl;
};
} // namespace aegis
