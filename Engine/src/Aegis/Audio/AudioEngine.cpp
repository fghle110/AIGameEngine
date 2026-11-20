#include "Aegis/Audio/AudioEngine.h"

#include "Aegis/Core/FileSystem.h"
#include "Aegis/Core/Log.h"
#include "Aegis/Scene/Components.h"
#include "Aegis/Scene/Entity.h"
#include "Aegis/Scene/Scene.h"

#include <miniaudio.h>

#include <glm/glm.hpp>

#include <cctype>
#include <cmath>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace aegis
{
	namespace
	{
		// Short clips are decoded up front (typical sound effects); longer ones are
		// streamed from disk so music does not have to fit in memory.
		constexpr f32 c_StreamThresholdSeconds = 4.0f;

		// miniaudio's linear attenuation needs max distance above min distance to
		// avoid a division by zero.
		constexpr f32 c_MinMaxDistanceGap = 0.01f;

		bool equalsIgnoreCase(std::string_view left, std::string_view right)
		{
			if (left.size() != right.size())
			{
				return false;
			}
			for (usize i = 0; i < left.size(); ++i)
			{
				const auto lower = [](char character)
				{
					return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
				};
				if (lower(left[i]) != lower(right[i]))
				{
					return false;
				}
			}
			return true;
		}

		// miniaudio's own name lookup is case sensitive ("Null", "WASAPI", ...);
		// a config string written by a user should not have to match that exactly.
		bool findBackendByName(std::string_view name, ma_backend& backend)
		{
			if (name.empty())
			{
				return false;
			}
			for (i32 index = 0; index <= static_cast<i32>(ma_backend_null); ++index)
			{
				const auto candidate = static_cast<ma_backend>(index);
				if (equalsIgnoreCase(name, ma_get_backend_name(candidate)))
				{
					backend = candidate;
					return true;
				}
			}
			return false;
		}

		std::string absolutePathUtf8(const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::path absolute = std::filesystem::absolute(path, error);
			if (error)
			{
				absolute = path;
			}
			return FileSystem::ToUtf8(absolute);
		}

		// A single playing ma_sound. ma_sound instances are nodes in the engine
		// graph, so their address must stay stable for the whole lifetime of the
		// sound; voices are therefore heap-allocated and the maps only move the
		// owning pointer.
		struct ActiveVoice
		{
			ma_sound Sound{};
			bool IsSpatial = false;
		};
	}

	struct AudioEngine::Impl
	{
		ma_context Context{};
		ma_engine Engine{};
		bool HasContext = false;
		bool HasEngine = false;

		AudioEngineConfig Config;
		f32 MasterVolume = 1.0f;
		std::string BackendName = "None";

		std::unordered_map<Uuid, AudioClipData, UuidHash> Clips;

		std::unordered_map<Uuid, Scope<ActiveVoice>, UuidHash> EntityVoices;
		std::unordered_map<u64, Scope<ActiveVoice>> DetachedVoices;

		// Entities whose PlayOnStart decision has already been made. Keeping the
		// set prevents stopped or finished sources from restarting every frame.
		std::unordered_set<Uuid, UuidHash> EvaluatedSources;

		Uuid ListenerOverride = Uuid::Null();
		Uuid ResolvedListener = Uuid::Null();
		u64 NextDetachedId = 1;

		~Impl()
		{
			stopAllVoices();
			destroyEngine();
		}

		bool initialize(const AudioEngineConfig& config)
		{
			if (HasEngine)
			{
				return true;
			}

			Config = config;
			MasterVolume = AudioEngine::ClampVolume(config.MasterVolume);

			bool initialized = false;

			if (config.UseNullBackend)
			{
				const ma_backend nullBackend[] = { ma_backend_null };
				initialized = startWithBackend(nullBackend, 1);
			}
			else
			{
				if (!config.PreferredBackendName.empty())
				{
					ma_backend preferred = ma_backend_null;
					if (!findBackendByName(config.PreferredBackendName, preferred))
					{
						AE_CORE_WARN("Audio: unknown backend name '{}'; using the default backend chain",
							config.PreferredBackendName);
					}
					else
					{
						const ma_backend preferredBackend[] = { preferred };
						initialized = startWithBackend(preferredBackend, 1);
						if (!initialized)
						{
							AE_CORE_WARN("Audio: preferred backend '{}' could not be opened; using the default backend chain",
								config.PreferredBackendName);
						}
					}
				}

				if (!initialized)
				{
					initialized = startWithBackend(nullptr, 0);
					if (initialized && Context.backend == ma_backend_null)
					{
						AE_CORE_WARN(
							"Audio: no playback device is available; the default backend chain selected the null backend");
					}
				}

				if (!initialized)
				{
					AE_CORE_WARN("Audio: no playback device is available; falling back to the miniaudio null backend");
					const ma_backend nullBackend[] = { ma_backend_null };
					initialized = startWithBackend(nullBackend, 1);
				}
			}

			if (!initialized)
			{
				AE_CORE_ERROR("Audio: failed to initialize any audio backend");
				return false;
			}

			ma_engine_set_volume(&Engine, MasterVolume);
			ma_engine_listener_set_position(&Engine, 0, 0.0f, 0.0f, 0.0f);
			ma_engine_listener_set_direction(&Engine, 0, 0.0f, 0.0f, -1.0f);
			ma_engine_listener_set_world_up(&Engine, 0, 0.0f, 1.0f, 0.0f);

			AE_CORE_INFO("Audio: initialized with the {} backend ({} Hz, {} channels, master volume {:.2f})",
				BackendName, ma_engine_get_sample_rate(&Engine), ma_engine_get_channels(&Engine), MasterVolume);
			return true;
		}

		bool startWithBackend(const ma_backend* backends, ma_uint32 backendCount)
		{
			destroyEngine();

			const ma_result contextResult = ma_context_init(backends, backendCount, nullptr, &Context);
			if (contextResult != MA_SUCCESS)
			{
				AE_CORE_DEBUG("Audio: context initialization failed ({}: {})", static_cast<i32>(contextResult),
					ma_result_description(contextResult));
				return false;
			}
			HasContext = true;

			ma_engine_config engineConfig = ma_engine_config_init();
			engineConfig.pContext = &Context;
			engineConfig.listenerCount = 1;
			engineConfig.noAutoStart = MA_FALSE;

			const ma_result engineResult = ma_engine_init(&engineConfig, &Engine);
			if (engineResult != MA_SUCCESS)
			{
				AE_CORE_DEBUG("Audio: engine initialization failed ({}: {})", static_cast<i32>(engineResult),
					ma_result_description(engineResult));
				destroyEngine();
				return false;
			}

			HasEngine = true;
			BackendName = ma_get_backend_name(Context.backend);
			return true;
		}

		void destroyEngine()
		{
			if (HasEngine)
			{
				ma_engine_uninit(&Engine);
				HasEngine = false;
			}
			if (HasContext)
			{
				ma_context_uninit(&Context);
				HasContext = false;
			}
			BackendName = "None";
		}

		void applyMasterVolume()
		{
			if (HasEngine)
			{
				ma_engine_set_volume(&Engine, MasterVolume);
			}
		}

		[[nodiscard]] const AudioClipData* findClip(AssetHandle clip) const
		{
			if (clip.Id.IsNull())
			{
				return nullptr;
			}
			const auto it = Clips.find(clip.Id);
			return it == Clips.end() ? nullptr : &it->second;
		}

		void releaseVoice(Scope<ActiveVoice>& voice)
		{
			if (!voice)
			{
				return;
			}
			ma_sound_stop(&voice->Sound);
			ma_sound_uninit(&voice->Sound);
			voice.reset();
		}

		void stopAllVoices()
		{
			for (auto& entry : EntityVoices)
			{
				releaseVoice(entry.second);
			}
			EntityVoices.clear();

			for (auto& entry : DetachedVoices)
			{
				releaseVoice(entry.second);
			}
			DetachedVoices.clear();
		}

		[[nodiscard]] static bool isFinished(const ActiveVoice& voice)
		{
			return ma_sound_at_end(&voice.Sound) != 0;
		}

		void pruneFinishedVoices()
		{
			for (auto it = EntityVoices.begin(); it != EntityVoices.end();)
			{
				if (isFinished(*it->second))
				{
					releaseVoice(it->second);
					it = EntityVoices.erase(it);
				}
				else
				{
					++it;
				}
			}

			for (auto it = DetachedVoices.begin(); it != DetachedVoices.end();)
			{
				if (isFinished(*it->second))
				{
					releaseVoice(it->second);
					it = DetachedVoices.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		u64 allocateDetachedId()
		{
			u64 id = NextDetachedId++;
			while (DetachedVoices.find(id) != DetachedVoices.end())
			{
				id = NextDetachedId++;
			}
			return id;
		}

		bool startVoice(const AudioClipData& data, const Uuid& owner, f32 volume, f32 pitch, bool loop, bool spatial,
			u64 detachedId)
		{
			if (!HasEngine)
			{
				AE_CORE_WARN("Audio: cannot start a sound because the audio engine is not initialized");
				return false;
			}
			if (!owner.IsNull() && EntityVoices.find(owner) != EntityVoices.end())
			{
				AE_CORE_WARN("Audio: entity {} already owns a voice", owner.ToString());
				return false;
			}

			auto voice = CreateScope<ActiveVoice>();
			const ma_uint32 flags =
				data.DurationSeconds > c_StreamThresholdSeconds ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
			const ma_result initResult =
				ma_sound_init_from_file(&Engine, data.SourcePath.c_str(), flags, nullptr, nullptr, &voice->Sound);
			if (initResult != MA_SUCCESS)
			{
				AE_CORE_WARN("Audio: failed to open clip '{}' ({}: {})", data.SourcePath, static_cast<i32>(initResult),
					ma_result_description(initResult));
				return false;
			}

			voice->IsSpatial = spatial && Config.EnableSpatialization;

			ma_sound_set_positioning(&voice->Sound, ma_positioning_absolute);
			ma_sound_set_attenuation_model(&voice->Sound, ma_attenuation_model_linear);
			ma_sound_set_rolloff(&voice->Sound, 1.0f);
			ma_sound_set_spatialization_enabled(&voice->Sound, voice->IsSpatial ? MA_TRUE : MA_FALSE);
			ma_sound_set_volume(&voice->Sound, AudioEngine::ClampVolume(volume));
			ma_sound_set_pitch(&voice->Sound, AudioEngine::ClampPitch(pitch));
			ma_sound_set_looping(&voice->Sound, loop ? MA_TRUE : MA_FALSE);

			const ma_result startResult = ma_sound_start(&voice->Sound);
			if (startResult != MA_SUCCESS)
			{
				AE_CORE_WARN("Audio: failed to start clip '{}' ({}: {})", data.SourcePath, static_cast<i32>(startResult),
					ma_result_description(startResult));
				ma_sound_uninit(&voice->Sound);
				return false;
			}

			if (owner.IsNull())
			{
				DetachedVoices.emplace(detachedId, std::move(voice));
			}
			else
			{
				EntityVoices.emplace(owner, std::move(voice));
			}
			return true;
		}

		void applySourceParameters(ActiveVoice& voice, const AudioSourceComponent& source, const glm::vec3& position)
		{
			ma_sound_set_volume(&voice.Sound, AudioEngine::ClampVolume(source.Volume));
			ma_sound_set_pitch(&voice.Sound, AudioEngine::ClampPitch(source.Pitch));
			ma_sound_set_looping(&voice.Sound, source.Loop ? MA_TRUE : MA_FALSE);

			const bool spatial = Config.EnableSpatialization && source.Spatial;
			if (spatial != voice.IsSpatial)
			{
				voice.IsSpatial = spatial;
				ma_sound_set_spatialization_enabled(&voice.Sound, spatial ? MA_TRUE : MA_FALSE);
			}

			if (spatial)
			{
				const f32 minDistance = Max(source.MinDistance, 0.0f);
				const f32 maxDistance = Max(source.MaxDistance, minDistance + c_MinMaxDistanceGap);
				ma_sound_set_attenuation_model(&voice.Sound, ma_attenuation_model_linear);
				ma_sound_set_rolloff(&voice.Sound, 1.0f);
				ma_sound_set_min_distance(&voice.Sound, minDistance);
				ma_sound_set_max_distance(&voice.Sound, maxDistance);
				ma_sound_set_position(&voice.Sound, position.x, position.y, position.z);
			}
		}

		[[nodiscard]] Entity resolveListener(Scene& scene) const
		{
			if (!ListenerOverride.IsNull())
			{
				Entity entity = scene.FindEntity(ListenerOverride);
				if (entity.IsValid() && entity.IsActive() && entity.HasComponent<AudioListenerComponent>())
				{
					return entity;
				}
			}

			Entity first;
			for (Entity entity : scene.GetEntities())
			{
				if (!entity.IsActive())
				{
					continue;
				}
				const AudioListenerComponent* listener = entity.TryGetComponent<AudioListenerComponent>();
				if (listener == nullptr)
				{
					continue;
				}
				if (listener->Primary)
				{
					return entity;
				}
				if (!first.IsValid())
				{
					first = entity;
				}
			}
			return first;
		}

		void updateListener(Entity listener)
		{
			if (!HasEngine)
			{
				return;
			}

			if (!listener.IsValid())
			{
				ResolvedListener = Uuid::Null();
				ma_engine_listener_set_position(&Engine, 0, 0.0f, 0.0f, 0.0f);
				ma_engine_listener_set_direction(&Engine, 0, 0.0f, 0.0f, -1.0f);
				ma_engine_listener_set_world_up(&Engine, 0, 0.0f, 1.0f, 0.0f);
				return;
			}

			glm::vec3 position(0.0f);
			glm::vec3 forward(0.0f, 0.0f, -1.0f);
			glm::vec3 up(0.0f, 1.0f, 0.0f);
			if (const TransformComponent* transform = listener.TryGetComponent<TransformComponent>())
			{
				position = transform->WorldPosition;
				forward = transform->WorldRotation * glm::vec3(0.0f, 0.0f, -1.0f);
				up = transform->WorldRotation * glm::vec3(0.0f, 1.0f, 0.0f);
			}

			ma_engine_listener_set_position(&Engine, 0, position.x, position.y, position.z);
			ma_engine_listener_set_direction(&Engine, 0, forward.x, forward.y, forward.z);
			ma_engine_listener_set_world_up(&Engine, 0, up.x, up.y, up.z);
			ResolvedListener = listener.GetUuid();
		}
	};

	AudioEngine::AudioEngine()
		: m_Impl(CreateScope<Impl>())
	{
	}

	AudioEngine::~AudioEngine()
	{
		Shutdown();
	}

	bool AudioEngine::Initialize(const AudioEngineConfig& config)
	{
		return m_Impl->initialize(config);
	}

	void AudioEngine::Shutdown()
	{
		if (!m_Impl)
		{
			return;
		}

		m_Impl->stopAllVoices();
		m_Impl->EvaluatedSources.clear();
		m_Impl->Clips.clear();
		m_Impl->ListenerOverride = Uuid::Null();
		m_Impl->ResolvedListener = Uuid::Null();
		m_Impl->destroyEngine();
	}

	bool AudioEngine::IsInitialized() const
	{
		return m_Impl->HasEngine;
	}

	std::string AudioEngine::GetBackendName() const
	{
		return m_Impl->BackendName;
	}

	bool AudioEngine::LoadClip(const std::filesystem::path& path, AudioClipData& out)
	{
		out = {};

		if (path.empty())
		{
			AE_CORE_WARN("Audio: cannot load a clip from an empty path");
			return false;
		}
		if (!FileSystem::IsFile(path))
		{
			AE_CORE_WARN("Audio: clip file '{}' does not exist", FileSystem::ToUtf8(path));
			return false;
		}

		const std::string utf8Path = absolutePathUtf8(path);
		ma_decoder decoder{};
		ma_result result = ma_decoder_init_file(utf8Path.c_str(), nullptr, &decoder);
		if (result != MA_SUCCESS)
		{
			AE_CORE_WARN("Audio: '{}' is not a supported audio file ({}: {})", utf8Path, static_cast<i32>(result),
				ma_result_description(result));
			return false;
		}

		ma_uint32 channels = 0;
		ma_uint32 sampleRate = 0;
		ma_uint64 frameCount = 0;
		result = ma_decoder_get_data_format(&decoder, nullptr, &channels, &sampleRate, nullptr, 0);
		if (result == MA_SUCCESS)
		{
			result = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
		}
		ma_decoder_uninit(&decoder);

		if (result != MA_SUCCESS || sampleRate == 0 || channels == 0 || frameCount == 0)
		{
			AE_CORE_WARN("Audio: clip '{}' contains no decodable audio frames", utf8Path);
			return false;
		}

		out.SourcePath = utf8Path;
		out.SampleRate = sampleRate;
		out.Channels = channels;
		out.DurationSeconds = static_cast<f32>(static_cast<f64>(frameCount) / static_cast<f64>(sampleRate));
		return true;
	}

	bool AudioEngine::RegisterClip(AssetHandle clip, const std::filesystem::path& path)
	{
		if (clip.Id.IsNull())
		{
			AE_CORE_WARN("Audio: cannot register a clip with a null asset handle");
			return false;
		}

		AudioClipData data;
		if (!LoadClip(path, data))
		{
			return false;
		}

		m_Impl->Clips[clip.Id] = std::move(data);
		AE_CORE_DEBUG("Audio: registered clip {} ('{}', {:.3f} s)", clip.Id.ToString(),
			m_Impl->Clips[clip.Id].SourcePath, m_Impl->Clips[clip.Id].DurationSeconds);
		return true;
	}

	void AudioEngine::UnregisterClip(AssetHandle clip)
	{
		if (clip.Id.IsNull())
		{
			return;
		}
		m_Impl->Clips.erase(clip.Id);
	}

	void AudioEngine::ClearClips()
	{
		m_Impl->Clips.clear();
	}

	bool AudioEngine::IsClipRegistered(AssetHandle clip) const
	{
		return m_Impl->findClip(clip) != nullptr;
	}

	const AudioClipData* AudioEngine::GetClipData(AssetHandle clip) const
	{
		return m_Impl->findClip(clip);
	}

	f32 AudioEngine::GetClipDuration(AssetHandle clip) const
	{
		const AudioClipData* data = m_Impl->findClip(clip);
		return data != nullptr ? data->DurationSeconds : 0.0f;
	}

	bool AudioEngine::Play(AssetHandle clip, f32 volume, f32 pitch, bool loop)
	{
		if (!m_Impl->HasEngine)
		{
			AE_CORE_WARN("Audio: Play() ignored because the audio engine is not initialized");
			return false;
		}

		const AudioClipData* data = m_Impl->findClip(clip);
		if (data == nullptr)
		{
			AE_CORE_WARN("Audio: clip {} is not registered; Play() ignored", clip.Id.ToString());
			return false;
		}

		return m_Impl->startVoice(*data, Uuid::Null(), volume, pitch, loop, false, m_Impl->allocateDetachedId());
	}

	void AudioEngine::Stop(Entity entity)
	{
		if (!entity.IsValid())
		{
			return;
		}
		Stop(entity.GetUuid());
	}

	void AudioEngine::Stop(const Uuid& entityUuid)
	{
		if (entityUuid.IsNull())
		{
			return;
		}

		const auto it = m_Impl->EntityVoices.find(entityUuid);
		if (it == m_Impl->EntityVoices.end())
		{
			return;
		}

		m_Impl->releaseVoice(it->second);
		m_Impl->EntityVoices.erase(it);
	}

	void AudioEngine::StopAll()
	{
		m_Impl->stopAllVoices();
	}

	usize AudioEngine::GetActiveSourceCount() const
	{
		return m_Impl->EntityVoices.size() + m_Impl->DetachedVoices.size();
	}

	void AudioEngine::SetMasterVolume(f32 volume)
	{
		m_Impl->MasterVolume = ClampVolume(volume);
		m_Impl->applyMasterVolume();
	}

	f32 AudioEngine::GetMasterVolume() const
	{
		return m_Impl->MasterVolume;
	}

	void AudioEngine::SetListener(Entity entity)
	{
		SetListener(entity.IsValid() ? entity.GetUuid() : Uuid::Null());
	}

	void AudioEngine::SetListener(const Uuid& entityUuid)
	{
		m_Impl->ListenerOverride = entityUuid;
	}

	void AudioEngine::ClearListener()
	{
		m_Impl->ListenerOverride = Uuid::Null();
	}

	Uuid AudioEngine::GetListenerUuid() const
	{
		return m_Impl->ResolvedListener;
	}

	void AudioEngine::OnUpdate(Scene& scene, Timestep)
	{
		if (!m_Impl->HasEngine)
		{
			return;
		}

		m_Impl->updateListener(m_Impl->resolveListener(scene));

		std::unordered_set<Uuid, UuidHash> liveSources;
		liveSources.reserve(scene.GetRegistry().GetComponentCount<AudioSourceComponent>());

		scene.GetRegistry().Each<AudioSourceComponent>(
			[&](ecs::EntityHandle handle, AudioSourceComponent& source)
			{
				Entity entity(handle, &scene);
				if (!entity.IsActive())
				{
					return;
				}

				const Uuid uuid = entity.GetUuid();
				liveSources.insert(uuid);

				glm::vec3 position(0.0f);
				if (const TransformComponent* transform = entity.TryGetComponent<TransformComponent>())
				{
					position = transform->WorldPosition;
				}

				const auto voiceIt = m_Impl->EntityVoices.find(uuid);
				if (voiceIt != m_Impl->EntityVoices.end())
				{
					m_Impl->applySourceParameters(*voiceIt->second, source, position);
					return;
				}

				// PlayOnStart is decided once, the first time the entity is seen. An
				// explicitly stopped source stays stopped and a missing clip warns
				// once instead of retrying every frame.
				if (!m_Impl->EvaluatedSources.insert(uuid).second || !source.PlayOnStart)
				{
					return;
				}

				const AudioClipData* clip = m_Impl->findClip(source.Clip);
				if (clip == nullptr)
				{
					AE_CORE_WARN("Audio: entity '{}' ({}) has no registered clip; playback was not started",
						entity.GetName(), uuid.ToString());
					return;
				}

				if (m_Impl->startVoice(*clip, uuid, source.Volume, source.Pitch, source.Loop, source.Spatial, 0))
				{
					const auto started = m_Impl->EntityVoices.find(uuid);
					if (started != m_Impl->EntityVoices.end())
					{
						m_Impl->applySourceParameters(*started->second, source, position);
					}
				}
			});

		// Destroyed entities, removed components and deactivated entities all end
		// up outside liveSources. Dropping the evaluation entry as well means a
		// reactivated entity starts PlayOnStart again, like a fresh one.
		for (auto it = m_Impl->EntityVoices.begin(); it != m_Impl->EntityVoices.end();)
		{
			if (liveSources.find(it->first) == liveSources.end())
			{
				m_Impl->releaseVoice(it->second);
				it = m_Impl->EntityVoices.erase(it);
			}
			else
			{
				++it;
			}
		}

		for (auto it = m_Impl->EvaluatedSources.begin(); it != m_Impl->EvaluatedSources.end();)
		{
			if (liveSources.find(*it) == liveSources.end())
			{
				it = m_Impl->EvaluatedSources.erase(it);
			}
			else
			{
				++it;
			}
		}

		m_Impl->pruneFinishedVoices();
	}

	f32 AudioEngine::ClampVolume(f32 volume)
	{
		if (!std::isfinite(volume))
		{
			return 1.0f;
		}
		return Clamp(volume, c_MinVolume, c_MaxVolume);
	}

	f32 AudioEngine::ClampPitch(f32 pitch)
	{
		if (!std::isfinite(pitch))
		{
			return 1.0f;
		}
		return Clamp(pitch, c_MinPitch, c_MaxPitch);
	}
}