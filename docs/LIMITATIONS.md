# Known limitations

Honest list of what Aegis does not implement yet. Nothing here is faked: when a feature
is listed, the engine either ignores the corresponding setting or does not expose it.

## Rendering

- **No image-based lighting.** `EnvironmentSettings.HdriPath`, `HdriIntensity` and
  `EnableIBL` are serialised but ignored. Ambient light comes from the procedural sky in
  `Engine/Shaders/SceneSky.glsl`; there is no equirectangular HDR import, no irradiance or
  prefiltered specular cube and no BRDF LUT, so metals reflect a cheap gradient instead of
  a real environment.
- **No screen-space ambient occlusion.** `EnvironmentSettings.EnableSSAO`, `SSAORadius`
  and `EnableFXAA` are serialised but ignored. The PBR pass has no depth prepass, so it
  cannot read the depth of the current frame; SSAO needs that prepass (or a deferred
  renderer) before it can be added honestly.
- **No bloom.** `EnvironmentSettings.EnableBloom`, `BloomThreshold` and `BloomIntensity`
  are serialised but ignored. The post pass currently does exposure and ACES only; a bloom
  chain (bright pass, downsample, separable blur, additive composite) would slot in between
  the HDR target and the tonemap.
- **No anti-aliasing.** No MSAA on the scene targets and no FXAA or TAA pass.
- **Single shadow map, no cascades.** The sun uses one 2048x2048 map fitted to a box around
  the camera (`SceneRenderer::ComputeDirectionalShadowMatrix`). Distant shadows lose
  resolution and shadow distance is the only tuning knob, unlike the cascaded shadow maps
  AGENTS.md describes as the target design. Point and spot lights cast no shadows.
- **No frustum culling.** `SceneRenderer::gatherDrawItems` submits every visible
  `MeshRendererComponent` without testing it against the camera frustum. Scenes with
  thousands of entities need it before the draw cost becomes acceptable.
- **No render settings UI.** There is no runtime panel or command that toggles individual
  features; the `EnvironmentSettings` of a scene are the only switchboard.
- **Per-entity textures are not persisted.** A material created at runtime (the demo scene
  does this) lives in the `AssetCache` only. Material documents (`.aemat`) and a texture
  import pipeline are not implemented, so a scene stores material handles that only resolve
  while the cache still holds them.
- **AssetCache uploads synchronously.** Each texture and mesh upload opens its own command
  list and waits for idle. That is fine for a few dozen assets but must become a batched
  upload queue backed by a staging ring for production content.

## Editor

- The viewport toolbar switches between the editor camera and the scene camera, but there
  is no per-camera aspect override, no safe-frame overlay and no play-in-viewport mode.

## Testing

- `Tests/Renderer/SceneRendererTests.cpp` skips its GPU cases when no Vulkan 1.3 device is
  available: it logs a warning and returns, so a machine without a GPU reports the cases as
  passed with a warning rather than as skipped. doctest has no `SKIP` status.
## Repository-wide status (lead integration pass)

- No direct-manipulation transform gizmo yet: entities are placed with the Inspector
  fields or the command API (`transform.set`); the viewport draws selection wireframes,
  a grid and entity icons but no draggable translate/rotate/scale handles.
- No undo/redo stack. `CommandInfo::Undoable` is false for every command; the registry
  and command handlers are structured so an undo layer can wrap them later.
- Export copies the runtime executable, compiled shaders and the project folder. There is
  no packed archive format (`.pak`) and no code obfuscation/signing.
- macOS and Linux build paths exist in CMake (GLFW, MoltenVK notes, POSIX sockets) but
  have not been validated on real hardware in this environment; only Windows was run.
- Skinned meshes and animation clips are not imported from glTF; static meshes only.
- Audio clips are registered at runtime (`AudioEngine::RegisterClip`) or resolved through
  the Lua script root convention; there is no editor asset pipeline for audio yet.
- The feature project references the built-in primitives as fallbacks when a mesh asset is
  not loaded: the runtime renders them with the default material and logs one warning per
  entity. Materials are authored through the editor command API in this milestone.
- Single window, single Vulkan device per process; ImGui multi-viewport is not enabled.
