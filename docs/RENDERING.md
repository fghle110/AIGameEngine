# Rendering

The Aegis renderer is Vulkan 1.3 through [NVRHI](https://github.com/NVIDIA-RTX/NVRHI).
`Renderer` owns the device and swapchain, `SceneRenderer` renders the scene into an
offscreen HDR target, and `ImGuiRenderer` composites the editor UI over it. Nothing
outside `Aegis/Renderer` and `Aegis/Assets` includes NVRHI headers.

## Frame structure

```
Renderer::BeginFrame          acquire swapchain image, open the frame command list, clear
  SceneRenderer::Render       0. IBL environment upload when the HDRI changed
                              1. shadow map pass (depth only, 2048x2048)
                              1b. SSAO (optional): camera depth prepass, half-res AO
                                  noise pass and a 9x9 blur, consumed by the PBR ambient term
                              2. forward PBR pass into RGBA16F + D32, starting with the sky
                              2b. bloom (optional): soft-knee bright pass at half resolution,
                                  horizontal blur, vertical blur, then an additive composite
                              3. post pass: exposure -> ACES -> SRGB8 display texture
  ImGuiRenderer::Render       UI, including the viewport image that samples the display texture
Renderer::EndFrame            submit, present, garbage collect
```

`SceneRenderer::Render` records every pass into the command list it is given, so the
editor can draw its UI in the same submission and the exported runtime can run with no
UI at all.

## Image-based lighting

When the scene sets `EnvironmentSettings.HdriPath` (or leaves it empty and `EnableIBL` is on)
the renderer builds an `aegis::EnvironmentMap` once and uploads it as three textures:

- an irradiance cube (already divided by PI, so diffuse = albedo * irradiance),
- a GGX-prefiltered specular cube with one mip per roughness step,
- a split-sum BRDF LUT.

All three are prefiltered on the CPU (`Engine/src/Aegis/Assets/EnvironmentMap.cpp`) from an
equirectangular image: any Poly Haven `.hdr` or the procedural sky from
`Engine/Shaders/SceneSky.glsl`. The PBR shader samples them when `uEnvironment.x` is set and
falls back to the analytic sky otherwise. Because the data is plain C++, the prefilter logic
is covered by GPU-free unit tests (`Tests/Renderer/EnvironmentMapTests.cpp`).

The cube textures are RGBA16F and are uploaded with packed half floats: NVRHI's
`writeTexture` copies raw bytes, it does not convert formats.

## Screen-space ambient occlusion

`EnvironmentSettings.EnableSSAO` switches on a depth-prepass AO chain:

1. `SceneDepthOnly.vert` renders the camera depth into the scene depth texture
   (`SceneRenderer::recordDepthPrepass`).
2. `SceneSsao.frag` reconstructs world positions from that depth, derives normals
   with screen-space derivatives and evaluates a 16-sample hemisphere kernel at
   half resolution.
3. `SceneSsaoBlur.frag` applies a 9x9 gaussian at the AO resolution.
4. The PBR shader multiplies the ambient (indirect) term by the blurred value; direct
   sun and point lights are deliberately not occluded. The forward pass then uses
   `LessOrEqual` depth so it passes the prepass depth it re-renders into.

The AO strength lives in `uEnvironment.w`, so the same switch also disables the
multiply when the passes are skipped.

## Bloom

`EnvironmentSettings.EnableBloom` (with `BloomThreshold` and `BloomIntensity`) runs a compact
chain between the forward and tonemap passes:

1. `BloomPrefilter.frag` samples the HDR target 2x2, halves the resolution and applies a
   soft-knee threshold so only real highlights contribute.
2. `BloomBlur.frag` runs a separable 9-tap gaussian twice, ping-ponging between two
   half-resolution RGBA16F targets.
3. `SceneTonemap.frag` adds the result scaled by the intensity before exposure, so the
   bloom is metered in scene-referred units and no tone-mapping happens twice.

## Render targets

| Target | Format | Purpose |
| --- | --- | --- |
| m_HdrColor | RGBA16_FLOAT | linear HDR scene colour, shader resource for the post pass |
| m_DepthTexture | D32 | depth for the PBR pass |
| m_FinalColor | SRGBA8_UNORM | tonemapped display image, sampled by ImGui |
| m_ShadowMap | D32 | directional sun shadow map |

Every target is created with `initialState` plus `keepInitialState = true` so NVRHI can
track states across frames. Reusing a resource without a declared initial state is what
produces validation errors and black frames, so new targets must follow the same pattern.

## Colour pipeline

The scene is lit in linear space and tonemapped exactly once, in the post pass:

1. textures are linearised on the CPU while loading (`Image::SrgbToLinear`) and the
   material factors are linear as well, so the PBR shader never applies a transfer
   function;
2. the forward pass writes linear radiance into RGBA16F;
3. `SceneTonemap.frag` multiplies by the scene exposure and applies the ACES filmic curve
   (Narkowicz fit);
4. the destination is an SRGBA8_UNORM texture, so the hardware performs the linear to sRGB
   encode once. ImGui samples that texture (sRGB decode) and writes to the sRGB swapchain
   (encode), which cancels out;
5. never tone-map twice: any new post effect must run before the tonemap, on the HDR
   target.

## Shading

`ScenePbr.frag` implements Cook-Torrance with the metallic-roughness workflow:

- **Specular**: GGX normal distribution, Smith geometry term, Schlick Fresnel.
- **Diffuse**: Lambertian albedo / pi, weighted by (1 - F) * (1 - metallic).
- **Normal mapping**: the vertex tangent frame is re-orthogonalised after interpolation; a
  material without a normal map binds a 1x1 flat normal (0.5, 0.5, 1.0) rather than white,
  because white decodes to a 45 degree tilt.
- **Lights**: one directional sun plus up to eight point/spot lights, with smooth range
  falloff and a smoothstepped spot cone.
- **Ambient**: a procedural sky (`Engine/Shaders/SceneSky.glsl`) provides the diffuse
  irradiance and a roughness-aware specular reflection. The same function draws the sky
  background, so surfaces reflect what is behind them.
- **Shadows**: the sun samples the shadow map with a 3x3 percentage-closer filter, a
  normal-offset bias and a slope-scaled raster bias.

## Descriptor sets and push constants

`registerSpaceIsDescriptorSet = true` makes the GLSL set numbers match the NVRHI
`registerSpace`. NVRHI maps a binding slot to a Vulkan binding as
`per-type offset + slot`, and the default offsets reserve 128 for samplers and 256 for
constant buffers. Every layout in this renderer pins `bindingOffsets` to zero, so the GLSL
bindings are the plain slot numbers:

| Set | Bindings |
| --- | --- |
| 0 (per frame) | 0 constant buffer, 1 shadow map, 2 sampler, push constants (112 bytes) |
| 1 (per material) | 0-4 albedo/normal/metallicRoughness/occlusion/emissive, 5-9 their samplers |
| 0 (depth passes) | 0 constant buffer, push constants |
| 0 (post and sky) | 0 HDR colour, 1 sampler, push constants (post only) |

The per-object block is pushed per draw:

```glsl
mat4 uModel;                             // 64 bytes
vec4 uBaseColorFactor;                   // 16
vec4 uMetallicRoughnessNormalOcclusion;  // 16  (metallic, roughness, normal scale, occlusion)
vec4 uEmissive;                          // 16  (rgb emissive, w strength)
```

112 bytes fits the 128-byte minimum every Vulkan device guarantees.

## NVRHI conventions

- NVRHI's Vulkan backend presents a Direct3D-style viewport (negative height), so the
  standard right-handed glm projections are used unchanged - never flip Y.
- `GLM_FORCE_DEPTH_ZERO_TO_ONE` is a global compile definition, so `glm::perspective`
  already produces the [0, 1] depth range Vulkan needs.
- Mesh builders and glTF wind front faces counter-clockwise seen from outside, which is the
  opposite of NVRHI's Direct3D default, so the scene pipelines set
  `rasterState.frontCounterClockwise = true`.
- Pipelines are created against `nvrhi::FramebufferInfo`, not against a framebuffer, so
  resizing the render targets does not recreate any pipeline.
- `writeBuffer` copies into NVRHI's upload buffer when the command list closes; it is used
  at most once per frame per constant buffer, never for per-object data. Per-object data
  travels through push constants instead.

## Assets

| Type | File | Notes |
| --- | --- | --- |
| Mesh | Assets/Mesh.h | pos/normal/tangent/uv vertices, submeshes, bounds, procedural Cube/Plane/Sphere/Cylinder builders, tangent and normal generation |
| Image | Assets/Texture.h | CPU texture: PNG/JPG via stb_image, .hdr as float, sRGB linearisation, box-filtered mip chain |
| Texture | Assets/Texture.h | GPU texture: RGBA16F upload with all mips plus the sampler preset |
| Material | Assets/Material.h | PBR factors plus the five map slots and the descriptor set that binds them |
| ModelImporter | Assets/ModelImporter.h | glTF/GLB through cgltf: baked node transforms, one submesh and one material per primitive, embedded and external images, generated tangents |

`AssetCache` (Renderer/AssetCache.h) is the single place where an `AssetHandle` becomes an
NVRHI resource. `SceneRenderer::Initialize` registers the four built-in primitives, and a
`MeshRendererComponent` whose handle is unknown falls back to the built-in cube (with one
warning per entity name) so content never silently disappears.

## Editor viewport

The editor renders the scene into the offscreen targets sized to the viewport, registers
the display texture with `ImGuiRenderer::RegisterTexture` (id 2, re-registered whenever the
target is recreated) and draws it with `ImGui::Image`. The grid, gizmos and selection
overlay are ImGui draw lists on top of that image. The viewport toolbar switches between
the free editor camera and the scene's primary camera; the gizmos and picking always use
the active camera.

`--demo` builds the primitive demo scene (checkerboard floor, two cubes, a sphere, a
cylinder, a sun and a point light) so
`AegisEditor --demo --frames 30 --screenshot out.png` proves PBR, shadows and tonemapping
without any external asset.

## Tests

| Suite | Coverage |
| --- | --- |
| Tests/Assets/MeshTests.cpp | primitive topology, bounds, unit normals/tangents, outward winding, tangent and normal generation |
| Tests/Assets/MaterialTests.cpp | engine and glTF factor defaults, texture slot rules, failure path |
| Tests/Assets/TextureTests.cpp | PNG and HDR round trips through stb, sRGB linearisation, box-filtered mips (including odd sizes), sampler presets |
| Tests/Assets/ModelImporterTests.cpp | minimal embedded glTF (data-URI buffer and image), external buffer, generated tangents, baked transforms, malformed input |
| Tests/Renderer/SceneRendererTests.cpp | headless Vulkan device: lit cube readback, textured plane readback, resize; the GPU cases skip with a warning when Vulkan 1.3 is unavailable |

## Measuring a change

Renderer changes are measured, not guessed. On the demo scene with a 1600x900 viewport,
five drawable entities and one shadow-casting sun, a frame costs 5 forward draws plus 4
shadow draws and renders in 12.9 ms on an Intel Iris Xe (Debug build, 78 fps). The Stats
panel and `SceneRenderer::GetStats()` report the same numbers.
