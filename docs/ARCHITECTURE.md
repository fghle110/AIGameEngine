# Architecture

## Layers

1. `Core` — types, logging, JSON, filesystem, UUID, window/input/events, application and layers, crash reporting, command bus.
2. `Scene` — sparse-set ECS, entities and components, hierarchy, cameras, scene systems (physics, scripting, audio).
3. `Renderer` — Vulkan context (volk), NVRHI device, swapchain, ImGui renderer, scene renderer.
4. `Assets / Serialization / Project` — meshes, textures, materials, glTF import, CPU IBL prefilter, versioned JSON, project manifests.
5. `Physics / Audio / Scripting` — Jolt, miniaudio and Lua behind `SceneSystem`.
6. `Math` — pure editor/geometry helpers (translation gizmo projection, picking and drag constraints).
7. `Editor` — ImGui shell, panels, commands; `Runtime` — player; `Tools` — CLI, demo project generator and export.

## Frame

- `Application::Run` polls the window, then calls each layer `OnUpdate` and `OnImGuiRender`.
- The editor layer updates the scene (physics -> scripting -> transforms -> audio), draws the UI, then records one command list: clear, optional depth prepass + SSAO, scene render (offscreen HDR target), optional bloom, post-process, ImGui composite, present.
- Presentation waits on NVRHI's queue timeline semaphore, so no frame is presented before its GPU work completes.

## Control

`CommandBus` is the single source of truth for editor actions. Panels call commands; `HttpServer` marshals HTTP requests onto the main thread and executes the same commands. This guarantees the AI API cannot drift from the UI.

## Rules

See `AGENTS.md`: dependencies point inward, the renderer is behind an interface, scene data is plain data, ownership is expressed in types, errors are explicit, tests ship with features.
