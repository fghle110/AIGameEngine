# AGENTS.md — Aegis engine development guide

Aegis is a production-grade cross-platform 3D game engine written in C++20 with
CMake. Windows, macOS and Linux (Ubuntu 24+) are the supported hosts; Vulkan is
the only render API (MoltenVK on macOS). This document is the contract every
contributor — human or AI agent — follows.

If anything in this file conflicts with a direct instruction from the project
owner, the owner's instruction wins. Otherwise these rules are binding.

---

## 1. Golden rules

1. **Production quality, no hacks.** No dead code, no "temporary" paths, no
   silent failure. If something cannot be implemented correctly yet, it is
   documented as a known limitation in `docs/LIMITATIONS.md`, not faked.
2. **Tests are part of the feature.** Every new behaviour ships with unit
   tests; every new engine subsystem ships with an integration test that runs
   the real code path. `ctest` must pass before any commit.
3. **The engine is a static library.** The editor and the runtime are thin
   executables that compose engine systems. Editor code never leaks into the
   engine library; the runtime never depends on ImGui or on editor code.
4. **One source of truth for control.** Every editor capability is implemented
   as a command in the editor command bus. The UI and the AI HTTP API call the
   same command. If you add a UI action with side effects, add the command too.
5. **Determinism and inspectability.** Scenes, prefabs and materials are
   human-readable JSON with stable key order. Every persistent object has a
   `Uuid`. Formats are versioned and must stay backward compatible.
6. **Measure, do not guess.** Renderer and simulation changes come with a
   before/after measurement (frame time, draw calls, test runtime), recorded in
   the commit message.
7. **Never break the build at HEAD.** No comment-only "fixes", no disabled
   tests, no `TODO` in place of required behaviour.

## 2. Repository layout

```
Engine/                 static engine library (Aegis::Engine)
  src/Aegis.h           umbrella header for game code
  src/Aegis/...         engine modules (public headers live next to sources)
  Shaders/              GLSL shaders, compiled to SPIR-V at build time
Editor/                 editor executable + editor UI sources (AegisEditor)
Runtime/                exported game runtime executable (AegisRuntime)
Tools/                  command line tools (asset import, scene inspection, AI CLI)
Tests/                  unit + integration tests (doctest, CTest)
Assets/                 sample project used by tests and as the feature test scene
docs/                   architecture, building, scripting, AI-control references
skills/                 agent-facing task playbooks (see section 10)
cmake/                  dependency, shader and warning modules
third_party/impl/       single-header implementation translation units
```

Public engine headers live under `Engine/src/Aegis/<Module>/`. Include them
as `#include "Aegis/<Module>/<Header>.h"`. Headers include their own
dependencies; a header must compile standalone.

## 3. Build and test

Windows:

```powershell
./Scripts/Build.ps1 -Configuration Debug -Test          # configure + build + test
./Scripts/Build.ps1 -Configuration Release -Test
./Scripts/Build.ps1 -Configuration Debug -Target AegisTests
```

Linux / macOS:

```bash
cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-debug
ctest --test-dir build/linux-debug --output-on-failure
```

Third-party dependencies are pinned in `cmake/Dependencies.cmake` and fetched
into the build directory on first configure. Never edit files under a fetched
dependency; if a patch is genuinely required, apply it through
`ExternalProject`/`PATCH_COMMAND` and document why.

Test binaries must be runnable without a GPU: renderer-facing tests use the
null renderer, and GPU tests are skipped (not silently passed) when Vulkan is
unavailable.

## 4. Code style (Hazel developer guide)

| Element | Convention | Example |
| --- | --- | --- |
| Namespaces | lower_snake_case | `aegis`, `aegis::renderer` |
| Classes / structs / enums | PascalCase | `SceneRenderer`, `RenderPipelineDesc` |
| Public methods / functions | PascalCase | `Init()`, `GetEntityCount()` |
| Private/protected methods | camelCase | `createSwapchain()` |
| Local variables / parameters | camelCase | `entityCount` |
| Private members | `m_` + camelCase | `m_Window` |
| Static / file-scope state | `s_` + camelCase | `s_Instance` |
| Constants (compile-time) | `c_` + PascalCase | `c_MaxFramesInFlight` |
| Macros | `AE_` prefix, UPPER_SNAKE | `AE_ASSERT`, `AE_EVENT_CLASS_TYPE` |
| Files | PascalCase, one primary type per file | `SceneRenderer.cpp` |
| Enumerators | PascalCase | `LogLevel::Warning` |
| Booleans | `Is`/`Has`/`Should` prefix | `IsVisible()` |

Formatting: tabs (width 4), Allman braces, 120 columns, pointers bind to the
type (`Type*`), no single-line control statements. `.clang-format` in the
repository root is authoritative — run it on every touched file.

Additional rules:

- `auto` only when the type is obvious from the right-hand side.
- `nullptr`, `constexpr`, `[[nodiscard]]` where they add safety.
- Never `using namespace` in headers.
- Prefer `Scope<T>` (unique_ptr) and `Ref<T>` (shared_ptr) aliases.
- Comments explain *why*; code explains *what*.
- Every assert message states the violated invariant and the actual value.

## 5. Architecture rules

- **Dependencies point inward.** `Core` depends on nothing but the standard
  library. `Platform` may use GLFW. `Renderer` may use NVRHI/volk/Vulkan.
  `Scene` may use `Core`. Systems never include editor headers.
- **The renderer is behind an interface.** Code outside `Renderer/` talks to
  `RendererBackend`; it does not include NVRHI or Vulkan headers. The null
  backend implements the same interface so logic is testable headlessly.
- **Scene data is plain data.** Components are POD-ish structs registered with
  the ECS. Behaviour lives in systems and in Lua scripts, not in components.
- **No global singletons** except the deliberate ones (`Log`, `Application::Get`,
  `AssetManager` owned by the project). Anything else is passed by reference.
- **Ownership is expressed in types.** Raw pointers never own. `Scope` owns,
  `Ref` shares, handles (`Entity`, asset IDs) reference.
- **Errors are explicit.** Functions returning values that can fail use
  `std::optional`, `bool` + out-parameter, or a `Result<T>`-style return —
  never an exception across a module boundary, never a silent default.
- **Threading.** Main thread owns scene/render state. Asset loading may use a
  worker pool, but results are integrated on the main thread.

## 6. Rendering conventions

- All shaders are GLSL (`Engine/Shaders/*.glsl`, stage encoded in the
  filename: `.vert`, `.frag`, `.comp`), compiled to SPIR-V by glslang at
  build time. Shader compilation failures break the build.
- Descriptor sets: 0 = per-frame (view/projection, lights, environment),
  1 = per-material, 2 = per-object (transform, custom constants). Keep the
  layout documented in `Engine/Shaders/Common.glsl`.
- Colour pipeline: render in linear space to an HDR target (RGBA16F), apply
  exposure, bloom and ACES tonemapping in the post process, then output in
  display space. Never tone-map twice.
- Shadow maps use cascaded shadow maps (CSM) with PCF; keep cascade splits in
  the frame constants so the editor can visualize them.
- IBL: an equirectangular HDR is prefiltered at load time into irradiance and
  prefiltered specular cubes plus a BRDF LUT. Cache the result next to the
  source file under `Saved/Cache/IBL/`.
- Every render feature must be switchable at runtime through the render
  settings (used by the editor and by automated tests).

## 7. Serialization conventions

- JSON written by the engine uses `JsonValue::Dump(4)`.
- Every document has `"version": <int>` at the root. Bump the version when
  changing semantics and keep a loader path for the previous version.
- Entity references are UUIDs as strings; asset references are project-relative
  paths with forward slashes plus the asset UUID.
- Unknown JSON members are preserved when rewriting a file if feasible;
  otherwise the omission is called out in the PR/commit description.
- Tests: every serializer has a round-trip test and a "loads a document written
  by the previous version" test.

## 8. Testing policy

- Unit test: pure logic, no GPU, no filesystem outside the build directory.
- Integration test: real subsystems (ECS + scene + serialization, scripting,
  physics, assets) with the null renderer.
- GPU test: requires a Vulkan device; skipped when unavailable. Never mark a
  GPU test as passed when it was skipped.
- The feature scene `Assets/Projects/FeatureTest` must reference every
  component type, every renderer feature and every scripting API entry point.
  An integration test loads it, simulates frames, and asserts the expected
  entity/component state.
- Doctest naming: `TEST_CASE("Module: behaviour under test")`. Assertions
  should cover both the success and the failure path.
- Bug fixes start with a failing test, then the fix.

## 9. Commits and reviews

Commit messages: imperative subject ≤ 72 characters, body explaining the
*why*, the measured impact and the tests run. Example:

```
Add cascaded shadow map fitting

Fits the four cascades to the camera frustum with a depth-range stable
projection so shadow shimmering drops noticeably while walking.

Tests: AegisTests (ShadowCascadeTests), feature scene GPU smoke test
```

Before every commit:

1. `./Scripts/Build.ps1 -Configuration Debug -Test` passes (or the Linux
   equivalent).
2. `./Scripts/Build.ps1 -Configuration Dist` links (shipping configuration).
3. Self-review the diff for style, dead code, missing tests and leaks.
4. Update `docs/` when behaviour or formats change.

## 10. Skills for agents

Task playbooks live in `skills/<name>/SKILL.md`:

- `aegis-engine-dev` — building, debugging and extending the engine itself.
- `aegis-editor-automation` — driving the editor/AI command API to build games
  without human intervention.
- `aegis-scene-authoring` — authoring scenes, prefabs and materials correctly.
- `aegis-testing` — writing and running unit/integration/GPU tests.

When a task matches one of these skills, read it first and follow it exactly.

## 11. Working with the AI editor API

The editor exposes a JSON command API (default `http://127.0.0.1:7777`) with
command `{"command": "<name>", "params": { ... }}` and a CLI wrapper in
`Tools/`. All state-changing commands are undoable and are logged to the
editor console. Automation scripts must:

1. Query `project.info` and `scene.list` instead of assuming state.
2. Create entities through `entity.create` and reference them by UUID.
3. Save the scene with `scene.save` before validating (`scene.validate`).
4. Take a screenshot (`editor.screenshot`) for visual verification.

The full command catalogue is generated from the command registry — see
`docs/AI_CONTROL.md` (regenerated by `AegisCli commands`).

## 12. Definition of done

A change is done when it builds in Debug and Dist, all tests pass, new
behaviour is covered by tests, user-facing docs/skills are updated, and the
feature is visible in the feature test project where applicable.
