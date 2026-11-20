# Audio

The audio subsystem wraps [miniaudio](https://github.com/mackron/miniaudio) behind
`aegis::AudioEngine`, a `SceneSystem` that owns the playback device, the clip
metadata cache and every playing voice. No miniaudio type appears in a public
engine header: the implementation is a pimpl in
`Engine/src/Aegis/Audio/AudioEngine.cpp`.

## Composing the system

```cpp
aegis::AudioEngine audio;
aegis::AudioEngineConfig config;
config.MasterVolume = 0.8f;
if (!audio.Initialize(config)) { /* no backend at all: report and stay silent */ }
scene.AddSystem(&audio); // update order 200, after physics (-100) and scripting (100)
```

Only one instance is needed per application. The editor and the runtime both
drive it through the same `Scene::OnUpdate` path.

## Backend selection and headless fallback

1. `UseNullBackend = true` forces miniaudio's cross-platform null backend. It
   mixes in real time and is what tests use, so no audio hardware is required.
2. `PreferredBackendName` (for example `"wasapi"`, `"directsound"`, `"null"`,
   matched case-insensitively) is tried first. An unknown or unavailable name
   logs a warning and continues.
3. The default miniaudio backend chain is tried next.
4. If no playback device can be opened, the engine falls back to the null
   backend, logs a warning and `IsInitialized()` still returns `true`.

`GetBackendName()` returns the backend actually in use (`"WASAPI"`,
`"Null"`, ...) or `"None"` before `Initialize` / after `Shutdown`.
`Initialize` is idempotent while the engine is running and `Shutdown` may be
called any number of times.

## Clips

`AudioEngine::RegisterClip(AssetHandle, std::filesystem::path)` decodes the file
header through miniaudio's built-in decoders (WAV, FLAC, OGG Vorbis, MP3),
stores the UTF-8 absolute path, duration, sample rate and channel count, and
keys the entry by the asset UUID. `AudioSourceComponent::Clip` is resolved
against this cache; the asset type of the handle is not inspected.

```cpp
AudioClipData decoded;
AudioEngine::LoadClip("Assets/Audio/Footstep.wav", decoded); // header only, no device
audio.RegisterClip(clip, "Assets/Audio/Footstep.wav");
audio.GetClipDuration(clip);  // seconds, 0 when not registered
```

Clips shorter than 4 seconds are decoded fully when playback starts; longer
clips stream from disk so music does not have to fit in memory. The cache is
cleared by `Shutdown`, so clips have to be registered again after
re-initialization.

## Entity sources

Every entity with an `AudioSourceComponent` is evaluated once per update:

* The first update that sees the entity starts the clip when `PlayOnStart` is
  set and the clip is registered. `Loop`, `Volume`, `Pitch`, `Spatial`,
  `MinDistance` and `MaxDistance` are applied and then refreshed every frame,
  so editor tweaks are audible immediately.
* A missing or unregistered clip logs a warning once and does not start; it is
  never retried silently.
* `Stop(Entity)` stops the voice immediately and does not restart it.
  Destroying the entity, removing the component, clearing the scene or
  deactivating the entity releases the voice on the next update. Deactivating
  and reactivating an entity re-evaluates `PlayOnStart`.
* `StopAll()` releases every entity voice and every detached `Play()` one-shot.
* `GetActiveSourceCount()` reports the number of tracked voices (entity sources
  plus detached one-shots that have not finished).

`AudioEngine::Play(clip, volume, pitch, loop)` is a detached 2D one-shot for
UI sounds and scripting. It is not attached to an entity and is released when
it ends or by `StopAll()`.

## Gain and pitch

* Source and master gain are clamped to `[0, 4]` (+12 dB); pitch is clamped to
  `[0.01, 4]` so the resampler can never divide by zero. Non-finite values fall
  back to 1. `AudioEngine::ClampVolume` / `ClampPitch` expose the exact rules.
* `SetMasterVolume` takes effect immediately, including for already playing
  sources; `GetMasterVolume` returns the stored (clamped) value.

## 3D listener and spatialization

The engine looks for an active `AudioListenerComponent` every update, preferring
one with `Primary = true` and otherwise using the first one found. Its
`TransformComponent::WorldPosition` becomes the listener position, its world
rotation supplies the forward (-Z) and up (+Y) axes. `SetListener(Entity)`
or `SetListener(Uuid)` pins an explicit listener; `ClearListener()` returns to
automatic selection. Without a listener the engine uses the world origin and
default orientation.

Spatialized sources use miniaudio's absolute positioning with linear
attenuation, rolloff 1 and the component's `MinDistance` / `MaxDistance`
(clamped so max > min). `AudioEngineConfig::EnableSpatialization = false`
forces every source to a plain 2D voice regardless of the component.

## Known limitations

* HRTF, occlusion, reverb and effect buses are not exposed yet; miniaudio's
  engine mixer, attenuation and doppler are available internally.
* Doppler is not enabled explicitly, so moving sources are attenuated but not
  pitch-shifted by velocity.
* Clips are decoded/streamed independently per voice: the same clip played
  twice opens two data sources. A shared decoded buffer cache is future work.
* Playback is main-thread driven through `Scene::OnUpdate`. If updates stop,
  already started voices keep playing, but parameter refreshes and listener
  tracking stop until updates resume.

## Tests

`Tests/Audio/AudioTests.cpp` generates a 0.1 s 44.1 kHz mono 16-bit PCM WAV in
the build tree and covers null-backend initialization and backend-name
selection, clip decoding and duration, `PlayOnStart` / `Stop` / entity
destruction / component removal, clamping, master volume, spatial listener
selection, detached one-shots and rejection of invalid clips. Nothing requires
audio hardware.