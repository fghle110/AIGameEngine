---
name: aegis-engine-dev
description: Use when building, debugging or extending the Aegis 3D game engine (C++/CMake/Vulkan/NVRHI).
---

# Aegis engine development

## Build and verify

1. `./Scripts/Build.ps1 -Configuration Debug -Test` configures, builds and runs the doctest suite through ctest.
2. A private build tree while other modules are edited: `./Scripts/Build.ps1 -BuildDirectory build/<agent> -CMakeArgs "-DAE_EXCLUDE_MODULES=Audio,Physics"`.
3. Headless rendering check: `build/windows-debug/bin/AegisEditor.exe --frames 60 --screenshot _probe/shot.png`, then inspect the PNG.

## Layout

- Engine modules: `Engine/src/Aegis/{Core,ECS,Scene,Renderer,Physics,Audio,Scripting,Serialization,Commands,Project}`.
- Shaders: `Engine/Shaders/*.vert|.frag`; register new files in `Engine/CMakeLists.txt` with `ae_add_shaders(Aegis FILES ...)`.
- Editor `Editor/src/Editor`, runtime `Runtime/src`, CLI/export `Tools/src/Cli`.

## Rules that bite

- NVRHI Vulkan uses a DX-style viewport: never flip Y in projections. `GLM_FORCE_DEPTH_ZERO_TO_ONE` is global.
- Cross-frame resources need `desc.initialState` + `desc.keepInitialState = true`. Volatile buffers are constant buffers only.
- GLSL `set = N` matches NVRHI binding layouts only with `registerSpaceIsDescriptorSet = true`.
- Every editor action is a `CommandBus` command; the UI calls commands, never the reverse.
- Add tests in the same change; ctest must be green before committing.
