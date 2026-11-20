#pragma once

// Miniaudio-backed audio subsystem. The public interface hides every miniaudio
// type behind a pimpl so game, editor and runtime code never include a
// third-party audio header. AudioEngine is a SceneSystem: register it with the
// scene and it keeps entity sources and the 3D listener in sync every update.
//
// The engine is main-thread only, like every other SceneSystem. miniaudio's
// audio thread reads the per-voice parameters through its own atomics.

#include "Aegis/Assets/AssetHandle.h"
#include "Aegis/Core/Base.h"
#include "Aegis/Core/Uuid.h"
#include "Aegis/Scene/SceneSystem.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace aegis
{
	class Entity;
	class Scene;
	class Timestep;

	// Metadata resolved from an audio file. The decoder is closed again before
	// this structure is returned, so keeping it costs no file handle. Playback
	// later reopens the file and fails with a warning if it has been removed.
	struct AudioClipData
	{
		// UTF-8 absolute path the clip was decoded from.
		std::string SourcePath;
		f32 DurationSeconds = 0.0f;
		u32 SampleRate = 0;
		u32 Channels = 0;

		[[nodiscard]] bool IsValid() const
		{
			return !SourcePath.empty() && DurationSeconds > 0.0f && SampleRate > 0 && Channels > 0;
		}
	};

	struct AudioEngineConfig
	{
		// When false, AudioSourceComponent::Spatial is ignored and every source is
		// mixed as a plain 2D voice.
		bool EnableSpatialization = true;

		// Forces the cross-platform null backend. Tests and headless runs use it;
		// it still mixes in real time and exercises the full engine path.
		bool UseNullBackend = false;

		// Clamped to [c_MinVolume, c_MaxVolume].
		f32 MasterVolume = 1.0f;

		// Optional miniaudio backend name ("wasapi", "directsound", "null", ...).
		// An unknown or unavailable name logs a warning and falls back to the
		// default backend chain.
		std::string PreferredBackendName;
	};

	// Owns the miniaudio engine, the clip metadata cache and every playing voice.
	// The application or editor owns one instance and registers it with
	// Scene::AddSystem so it runs after physics and scripting.
	class AudioEngine final : public SceneSystem
	{
	public:
		AudioEngine();
		~AudioEngine() override;

		AudioEngine(const AudioEngine&) = delete;
		AudioEngine& operator=(const AudioEngine&) = delete;

		std::string_view GetName() const override { return "Audio"; }
		i32 GetUpdateOrder() const override { return 200; }
		void OnUpdate(Scene& scene, Timestep timestep) override;

		// Opens the audio device and applies the master volume. If no playback
		// device is available (headless CI) or the preferred backend cannot be
		// opened, the miniaudio null backend is used instead: a warning is logged
		// and IsInitialized() still returns true. Calling Initialize twice keeps
		// the running engine and returns true.
		[[nodiscard]] bool Initialize(const AudioEngineConfig& config = {});
		void Shutdown();
		[[nodiscard]] bool IsInitialized() const;

		// Friendly miniaudio backend name ("WASAPI", "Null", ...). "None" before
		// initialization and after Shutdown.
		[[nodiscard]] std::string GetBackendName() const;

		// Reads the file header through miniaudio's decoders (WAV, FLAC, OGG
		// Vorbis, MP3) without opening a device. Fills the UTF-8 path, duration,
		// sample rate and channel count. Returns false and logs a warning when the
		// file is missing, unsupported or empty.
		[[nodiscard]] static bool LoadClip(const std::filesystem::path& path, AudioClipData& out);

		// Registers (or replaces) the clip behind an asset handle. Only the UUID of
		// the handle is used as the key; the asset type is not inspected. Returns
		// false and logs a warning when the file cannot be loaded.
		[[nodiscard]] bool RegisterClip(AssetHandle clip, const std::filesystem::path& path);
		void UnregisterClip(AssetHandle clip);
		void ClearClips();
		[[nodiscard]] bool IsClipRegistered(AssetHandle clip) const;

		// Metadata for a registered clip, or nullptr. The pointer is owned by the
		// engine and invalidated by the next clip registration change or Shutdown.
		[[nodiscard]] const AudioClipData* GetClipData(AssetHandle clip) const;

		// Duration in seconds, or 0 when the clip is not registered.
		[[nodiscard]] f32 GetClipDuration(AssetHandle clip) const;

		// Plays a registered clip as a detached 2D one-shot that is not bound to
		// an entity. Volume is clamped to [0, 4] and pitch to [0.01, 4]. Returns
		// false and logs a warning when the engine is not initialized or the clip
		// is not registered.
		[[nodiscard]] bool Play(AssetHandle clip, f32 volume = 1.0f, f32 pitch = 1.0f, bool loop = false);

		// Stops the entity's AudioSourceComponent voice. PlayOnStart is not
		// re-evaluated, so the source stays silent until it is recreated.
		void Stop(Entity entity);
		void Stop(const Uuid& entityUuid);
		void StopAll();

		// Number of tracked voices: entity sources plus detached Play() one-shots.
		// Non-looping voices are pruned on the update after they finish.
		[[nodiscard]] usize GetActiveSourceCount() const;

		void SetMasterVolume(f32 volume);
		[[nodiscard]] f32 GetMasterVolume() const;

		// Explicit 3D listener override. ClearListener() returns to automatic mode,
		// which prefers an active AudioListenerComponent with Primary set and
		// otherwise uses the first one found. Position and orientation are
		// refreshed from the listener's TransformComponent every update.
		void SetListener(Entity entity);
		void SetListener(const Uuid& entityUuid);
		void ClearListener();
		[[nodiscard]] Uuid GetListenerUuid() const;

		// Gain is clamped to [0, 4] (+12 dB) for sources and the master bus; pitch
		// is clamped to [0.01, 4] so the resampler never divides by zero. Values
		// that are not finite fall back to 1.
		[[nodiscard]] static f32 ClampVolume(f32 volume);
		[[nodiscard]] static f32 ClampPitch(f32 pitch);

		static constexpr f32 c_MinVolume = 0.0f;
		static constexpr f32 c_MaxVolume = 4.0f;
		static constexpr f32 c_MinPitch = 0.01f;
		static constexpr f32 c_MaxPitch = 4.0f;

	private:
		struct Impl;
		Scope<Impl> m_Impl;
	};
}