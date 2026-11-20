#include "TestSupport.h"

#include <doctest/doctest.h>

#include "Aegis/Audio/AudioEngine.h"
#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Scene/Scene.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

using namespace aegis;

namespace
{
	constexpr u32 c_TestSampleRate = 44100;
	constexpr u32 c_TestChannelCount = 1;
	constexpr u32 c_TestFrameCount = 4410; // 0.1 seconds at 44.1 kHz
	constexpr u16 c_TestBitsPerSample = 16;
	constexpr u32 c_TestBytesPerFrame = 2; // mono 16-bit

	void appendU16(std::vector<u8>& bytes, u16 value)
	{
		bytes.push_back(static_cast<u8>(value & 0xFFu));
		bytes.push_back(static_cast<u8>((value >> 8) & 0xFFu));
	}

	void appendU32(std::vector<u8>& bytes, u32 value)
	{
		bytes.push_back(static_cast<u8>(value & 0xFFu));
		bytes.push_back(static_cast<u8>((value >> 8) & 0xFFu));
		bytes.push_back(static_cast<u8>((value >> 16) & 0xFFu));
		bytes.push_back(static_cast<u8>((value >> 24) & 0xFFu));
	}

	void appendTag(std::vector<u8>& bytes, std::string_view tag)
	{
		for (const char character : tag)
		{
			bytes.push_back(static_cast<u8>(character));
		}
	}

	// Writes a canonical little-endian 16-bit PCM WAV so no audio asset has to be
	// checked into the repository.
	std::vector<u8> buildToneWav()
	{
		const u32 dataSize = c_TestFrameCount * c_TestBytesPerFrame;

		std::vector<u8> bytes;
		bytes.reserve(44 + dataSize);

		appendTag(bytes, "RIFF");
		appendU32(bytes, 36 + dataSize);
		appendTag(bytes, "WAVE");
		appendTag(bytes, "fmt ");
		appendU32(bytes, 16); // PCM format chunk size
		appendU16(bytes, 1);  // WAVE_FORMAT_PCM
		appendU16(bytes, static_cast<u16>(c_TestChannelCount));
		appendU32(bytes, c_TestSampleRate);
		appendU32(bytes, c_TestSampleRate * c_TestBytesPerFrame);
		appendU16(bytes, static_cast<u16>(c_TestBytesPerFrame));
		appendU16(bytes, c_TestBitsPerSample);
		appendTag(bytes, "data");
		appendU32(bytes, dataSize);

		constexpr f64 c_TwoPi = 6.283185307179586;
		for (u32 frame = 0; frame < c_TestFrameCount; ++frame)
		{
			const f64 phase = c_TwoPi * 440.0 * static_cast<f64>(frame) / static_cast<f64>(c_TestSampleRate);
			const auto sample = static_cast<i16>(std::sin(phase) * 8000.0);
			appendU16(bytes, static_cast<u16>(sample));
		}

		return bytes;
	}

	std::filesystem::path writeToneWav(std::string_view fileName)
	{
		const std::filesystem::path path = std::filesystem::current_path() / "TestAssets" / "Audio" / fileName;
		const std::vector<u8> bytes = buildToneWav();
		REQUIRE(FileSystem::WriteBinaryFile(path, bytes.data(), bytes.size()));
		return path;
	}

	AssetHandle makeAudioClip()
	{
		AssetHandle clip;
		clip.Id = Uuid::Generate();
		clip.Type = AssetType::Audio;
		return clip;
	}

	void initializeNullEngine(AudioEngine& engine)
	{
		AudioEngineConfig config;
		config.UseNullBackend = true;
		REQUIRE(engine.Initialize(config));
		REQUIRE(engine.IsInitialized());
	}
}

TEST_CASE("Audio: null backend initializes and shuts down idempotently")
{
	AudioEngine engine;
	CHECK_FALSE(engine.IsInitialized());
	CHECK(engine.GetName() == "Audio");
	CHECK(engine.GetUpdateOrder() == 200);

	AudioEngineConfig config;
	config.UseNullBackend = true;
	REQUIRE(engine.Initialize(config));
	CHECK(engine.IsInitialized());
	CHECK(engine.GetBackendName() == "Null");

	// A second Initialize keeps the running engine instead of recreating it.
	CHECK(engine.Initialize(config));
	CHECK(engine.IsInitialized());
	CHECK(engine.GetBackendName() == "Null");

	engine.Shutdown();
	CHECK_FALSE(engine.IsInitialized());
	CHECK(engine.GetBackendName() == "None");

	// Shutdown must be safe to repeat.
	engine.Shutdown();
	CHECK_FALSE(engine.IsInitialized());

	// A backend can also be selected by name; the null backend always exists.
	AudioEngine named;
	AudioEngineConfig namedConfig;
	namedConfig.PreferredBackendName = "null";
	REQUIRE(named.Initialize(namedConfig));
	CHECK(named.GetBackendName() == "Null");
	named.Shutdown();
	CHECK_FALSE(named.IsInitialized());
}

TEST_CASE("Audio: WAV clip decodes with the expected duration")
{
	const std::filesystem::path path = writeToneWav("tone.wav");

	AudioClipData decoded;
	REQUIRE(AudioEngine::LoadClip(path, decoded));
	CHECK(decoded.IsValid());
	CHECK(decoded.SampleRate == c_TestSampleRate);
	CHECK(decoded.Channels == c_TestChannelCount);
	CHECK(decoded.DurationSeconds == doctest::Approx(0.1f).epsilon(0.02));
	CHECK_FALSE(decoded.SourcePath.empty());

	AudioEngine engine;
	initializeNullEngine(engine);

	const AssetHandle clip = makeAudioClip();
	REQUIRE(engine.RegisterClip(clip, path));
	CHECK(engine.IsClipRegistered(clip));
	CHECK(engine.GetClipDuration(clip) == doctest::Approx(decoded.DurationSeconds));
	REQUIRE(engine.GetClipData(clip) != nullptr);
	CHECK(engine.GetClipData(clip)->Channels == c_TestChannelCount);

	engine.UnregisterClip(clip);
	CHECK_FALSE(engine.IsClipRegistered(clip));
	CHECK(engine.GetClipDuration(clip) == 0.0f);
}

TEST_CASE("Audio: PlayOnStart source plays and Stop releases it")
{
	const std::filesystem::path path = writeToneWav("play-on-start.wav");

	AudioEngine engine;
	initializeNullEngine(engine);

	const AssetHandle clip = makeAudioClip();
	REQUIRE(engine.RegisterClip(clip, path));

	Scene scene("Audio Test");
	scene.AddSystem(&engine);

	Entity source = scene.CreateEntity("Speaker");
	AudioSourceComponent& component = source.AddComponent<AudioSourceComponent>();
	component.Clip = clip;
	component.PlayOnStart = true;
	component.Spatial = false;
	component.Loop = true;
	component.Volume = 0.5f;
	component.Pitch = 1.0f;

	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 1u);

	// The system does not restart a source that was stopped explicitly.
	engine.Stop(source);
	CHECK(engine.GetActiveSourceCount() == 0u);
	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 0u);
}

TEST_CASE("Audio: destroying the entity or removing the component stops playback")
{
	const std::filesystem::path path = writeToneWav("lifecycle.wav");

	AudioEngine engine;
	initializeNullEngine(engine);

	const AssetHandle clip = makeAudioClip();
	REQUIRE(engine.RegisterClip(clip, path));

	Scene scene("Audio Test");
	scene.AddSystem(&engine);

	Entity destroyed = scene.CreateEntity("Destroyed Speaker");
	AudioSourceComponent& destroyedSource = destroyed.AddComponent<AudioSourceComponent>();
	destroyedSource.Clip = clip;
	destroyedSource.Spatial = false;
	destroyedSource.Loop = true;

	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 1u);

	scene.DestroyEntity(destroyed);
	CHECK_FALSE(destroyed.IsValid());
	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 0u);

	Entity removed = scene.CreateEntity("Muted Speaker");
	AudioSourceComponent& removedSource = removed.AddComponent<AudioSourceComponent>();
	removedSource.Clip = clip;
	removedSource.Spatial = false;
	removedSource.Loop = true;

	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 1u);

	removed.RemoveComponent<AudioSourceComponent>();
	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 0u);

	// Clearing the scene must release every remaining voice.
	Entity cleared = scene.CreateEntity("Cleared Speaker");
	AudioSourceComponent& clearedSource = cleared.AddComponent<AudioSourceComponent>();
	clearedSource.Clip = clip;
	clearedSource.Spatial = false;
	clearedSource.Loop = true;

	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 1u);

	scene.Clear();
	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 0u);
}

TEST_CASE("Audio: volume and pitch clamping and master volume round trip")
{
	AudioEngine engine;
	CHECK(engine.GetMasterVolume() == doctest::Approx(1.0f));

	CHECK(AudioEngine::ClampVolume(0.5f) == doctest::Approx(0.5f));
	CHECK(AudioEngine::ClampVolume(-1.0f) == doctest::Approx(0.0f));
	CHECK(AudioEngine::ClampVolume(10.0f) == doctest::Approx(4.0f));
	CHECK(AudioEngine::ClampPitch(1.5f) == doctest::Approx(1.5f));
	CHECK(AudioEngine::ClampPitch(0.0f) == doctest::Approx(0.01f));
	CHECK(AudioEngine::ClampPitch(100.0f) == doctest::Approx(4.0f));

	initializeNullEngine(engine);

	engine.SetMasterVolume(0.25f);
	CHECK(engine.GetMasterVolume() == doctest::Approx(0.25f));
	engine.SetMasterVolume(2.0f);
	CHECK(engine.GetMasterVolume() == doctest::Approx(2.0f));
	engine.SetMasterVolume(-4.0f);
	CHECK(engine.GetMasterVolume() == doctest::Approx(0.0f));
	engine.SetMasterVolume(100.0f);
	CHECK(engine.GetMasterVolume() == doctest::Approx(4.0f));

	// Hostile source values are clamped instead of failing playback.
	const AssetHandle clip = makeAudioClip();
	REQUIRE(engine.RegisterClip(clip, writeToneWav("clamped.wav")));

	Scene scene("Audio Test");
	scene.AddSystem(&engine);

	Entity source = scene.CreateEntity("Loud Speaker");
	AudioSourceComponent& component = source.AddComponent<AudioSourceComponent>();
	component.Clip = clip;
	component.Spatial = false;
	component.Loop = true;
	component.Volume = 100.0f;
	component.Pitch = -2.0f;

	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 1u);
}

TEST_CASE("Audio: spatial sources resolve the primary listener and follow transforms")
{
	const std::filesystem::path path = writeToneWav("spatial.wav");

	AudioEngine engine;
	initializeNullEngine(engine);

	const AssetHandle clip = makeAudioClip();
	REQUIRE(engine.RegisterClip(clip, path));

	Scene scene("Audio Test");
	scene.AddSystem(&engine);

	Entity listener = scene.CreateEntity("Listener");
	listener.AddComponent<AudioListenerComponent>();
	listener.GetComponent<TransformComponent>().LocalPosition = glm::vec3(2.0f, 1.0f, -3.0f);

	// Scene enumeration sees the listener; the engine picks it up on update.
	usize listenerCount = 0;
	for (Entity entity : scene.GetEntities())
	{
		if (entity.HasComponent<AudioListenerComponent>())
		{
			++listenerCount;
		}
	}
	CHECK(listenerCount == 1u);

	Entity primary = scene.CreateEntity("Primary Listener");
	AudioListenerComponent& primaryComponent = primary.AddComponent<AudioListenerComponent>();
	primaryComponent.Primary = true;

	Entity emitter = scene.CreateEntity("Emitter");
	AudioSourceComponent& source = emitter.AddComponent<AudioSourceComponent>();
	source.Clip = clip;
	source.Spatial = true;
	source.Loop = true;
	source.MinDistance = 1.0f;
	source.MaxDistance = 25.0f;

	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 1u);
	CHECK(engine.GetListenerUuid() == primary.GetUuid());

	// Moving both entities refreshes the 3D parameters without restarting.
	emitter.GetComponent<TransformComponent>().LocalPosition = glm::vec3(8.0f, 0.0f, 0.0f);
	primary.GetComponent<TransformComponent>().LocalPosition = glm::vec3(-4.0f, 2.0f, 0.0f);
	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 1u);
	CHECK(engine.GetListenerUuid() == primary.GetUuid());

	// An explicit override wins; clearing it returns to the primary listener.
	engine.SetListener(listener);
	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetListenerUuid() == listener.GetUuid());

	engine.ClearListener();
	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetListenerUuid() == primary.GetUuid());

	// No listener at all falls back to the world origin instead of crashing.
	scene.DestroyEntity(listener);
	scene.DestroyEntity(primary);
	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetListenerUuid().IsNull());
	CHECK(engine.GetActiveSourceCount() == 1u);
}

TEST_CASE("Audio: detached Play starts one-shots that StopAll releases")
{
	const std::filesystem::path path = writeToneWav("one-shot.wav");

	AudioEngine engine;
	initializeNullEngine(engine);

	const AssetHandle clip = makeAudioClip();
	REQUIRE(engine.RegisterClip(clip, path));

	CHECK(engine.Play(clip, 0.5f, 1.0f, false));
	CHECK(engine.GetActiveSourceCount() == 1u);

	// Out-of-range parameters are clamped, loop keeps the second voice alive.
	CHECK(engine.Play(clip, 8.0f, 0.0f, true));
	CHECK(engine.GetActiveSourceCount() == 2u);

	engine.StopAll();
	CHECK(engine.GetActiveSourceCount() == 0u);

	engine.StopAll();
	CHECK(engine.GetActiveSourceCount() == 0u);
}

TEST_CASE("Audio: invalid clips are rejected with a warning")
{
	const std::filesystem::path missing =
		std::filesystem::current_path() / "TestAssets" / "Audio" / "missing.wav";
	FileSystem::RemoveFile(missing);

	AudioClipData decoded;
	CHECK_FALSE(AudioEngine::LoadClip(missing, decoded));
	CHECK_FALSE(decoded.IsValid());

	AudioEngine uninitialized;
	CHECK_FALSE(uninitialized.IsInitialized());
	CHECK_FALSE(uninitialized.Play(makeAudioClip()));

	AudioEngine engine;
	initializeNullEngine(engine);

	const AssetHandle unregistered = makeAudioClip();
	CHECK_FALSE(engine.IsClipRegistered(unregistered));
	CHECK(engine.GetClipDuration(unregistered) == 0.0f);
	CHECK(engine.GetClipData(unregistered) == nullptr);

	const u64 warningsBeforePlay = Log::GetMessageCount(LogLevel::Warn);
	CHECK_FALSE(engine.Play(unregistered, 1.0f, 1.0f, false));
	CHECK(engine.GetActiveSourceCount() == 0u);
	CHECK(Log::GetMessageCount(LogLevel::Warn) > warningsBeforePlay);

	Scene scene("Audio Test");
	scene.AddSystem(&engine);

	Entity source = scene.CreateEntity("Broken Speaker");
	AudioSourceComponent& component = source.AddComponent<AudioSourceComponent>();
	component.Clip = unregistered;
	component.PlayOnStart = true;

	const u64 warningsBeforeUpdate = Log::GetMessageCount(LogLevel::Warn);
	scene.OnUpdate(Timestep(1.0f / 60.0f));
	CHECK(engine.GetActiveSourceCount() == 0u);
	CHECK(Log::GetMessageCount(LogLevel::Warn) > warningsBeforeUpdate);

	// A malformed file is rejected just like a missing one.
	const std::filesystem::path garbage =
		std::filesystem::current_path() / "TestAssets" / "Audio" / "garbage.wav";
	const std::vector<u8> bytes{ 0x52, 0x49, 0x46, 0x46, 0x00 };
	REQUIRE(FileSystem::WriteBinaryFile(garbage, bytes.data(), bytes.size()));
	CHECK_FALSE(engine.RegisterClip(makeAudioClip(), garbage));
}