# Building Aegis

## Requirements

- Windows: Visual Studio 2022+ (MSVC v143+) with the C++ workload, CMake 3.24+, Ninja (bundled with VS), Windows SDK 10.
- Linux: GCC 13+ or Clang 17+, CMake 3.24+, Ninja, X11/Wayland development packages for GLFW, Vulkan loader + drivers.
- macOS: Xcode 15+, CMake, Ninja; Vulkan via MoltenVK (install the SDK and make `MoltenVK` discoverable).

## Windows

```powershell
./Scripts/Build.ps1                     # Debug configure + build
./Scripts/Build.ps1 -Test              # + ctest
./Scripts/Build.ps1 -CMakeArgs "-DAE_EXCLUDE_MODULES=Audio"   # partial tree for parallel work
./Scripts/Build.ps1 -BuildDirectory build/other -Configuration Release
./Scripts/Build.ps1 -Clean -Configuration Debug
```

## Linux / macOS

```bash
cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-debug
ctest --test-dir build/linux-debug --output-on-failure
```

## Dependencies

All third-party code is pinned in `cmake/Dependencies.cmake` and fetched on first configure: GLFW 3.4, glm 1.0.1, NVRHI (pinned commit) + Vulkan-Headers 1.4.352, volk 1.3.295, glslang 15.4.0 (shader compiler), Jolt 5.7.0, Dear ImGui 1.91.9b-docking, Lua 5.4.8, doctest 2.4.12, stb, cgltf, miniaudio 0.11.26. Source archives are hash-verified; git dependencies are pinned to tags or commits.

Shaders (GLSL) are compiled to SPIR-V at build time and copied next to every executable that needs them.

## Packaging

`AegisCli export <project.aeproj> <output-directory>` copies `AegisRuntime.exe`, the compiled shaders and the project folder, and writes a README with the run command.
