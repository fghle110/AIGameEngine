# Known limitations

Honest list of what Aegis does not implement yet. Nothing here is faked: when a feature
is listed, the engine either ignores the corresponding setting or does not expose it.

## Rendering

- **Image-based lighting exists but is prefiltered on the CPU.** `EnvironmentSettings.HdriPath`
  loads any equirectangular `.hdr` (Poly Haven HDRIs work unchanged); when empty, the
  procedural sky is prefiltered instead. The prefilter (irradiance cube, GGX specular mip
  chain, split-sum BRDF LUT) runs once per environment in `Assets/EnvironmentMap.cpp` at
  48²/24²/32², which takes about a second in a debug build on the reference laptop. There
  is no GPU prefilter and no disk cache yet, and a scene with several different HDRIs pays
  that cost per environment switch.
- **SSAO is a single half-resolution estimate without temporality.** `EnableSSAO` and
  `SSAORadius` drive a 16-sample hemisphere kernel plus a 9x9 blur, and it multiplies only
  the ambient term. It has no bilateral depth-aware blur, no temporal accumulation and no
  denoise, so thin geometry can show halo noise; `EnableFXAA` is still ignored.
- **Bloom is a single-level, half-resolution chain.** `EnableBloom`, `BloomThreshold` and
  `BloomIntensity` drive a soft-knee bright pass, one horizontal and one vertical 9-tap
  gaussian and an additive composite before exposure. There is no multi-mip pyramid, no
  lens dirt/anamorphic streak and no temporal jitter, so very bright highlights bloom with a
  fixed radius.
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

- The transform gizmo only translates: it has three world-space axis handles plus a
  camera-plane centre handle, drags go through `transform.set`, and it has no rotate or
  scale handles, no local-axis mode and no snapping. Picking and drag math live in
  `Aegis/Math/EditorGizmo` and are unit tested; the drawing/dragging itself is editor UI.
- No undo/redo stack. `CommandInfo::Undoable` is false for every command; the registry
  and command handlers are structured so an undo layer can wrap them later.
- Export copies the runtime executable, compiled shaders and the project folder. There is
  no packed archive format (`.pak`) and no code obfuscation/signing.
- macOS and Linux build paths exist in CMake (GLFW, MoltenVK notes, POSIX sockets) but
  have not been validated on real hardware in this environment; only Windows was run.
- Skinned meshes and animation clips are not imported from glTF; static meshes only.
- Audio clips are registered at runtime (`AudioEngine::RegisterClip`) or resolved through
  the Lua script root convention; there is no editor asset pipeline for audio yet.
- The feature project now references the built-in primitives through their stable UUIDs, so
  the exported runtime renders the real cube/plane/sphere/cylinder. It still uses the default
  material everywhere: material assets are authored in the editor / through the command API,
  and the scene format can reference them, but no material files ship with the sample project.
- Single window, single Vulkan device per process; ImGui multi-viewport is not enabled.
