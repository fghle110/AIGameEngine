---
name: aegis-editor-automation
description: Use when an AI agent must drive the Aegis editor to author or test a game without human input.
---

# Driving the Aegis editor from an agent

## Start

`build/windows-debug/bin/AegisEditor.exe` serves the command API on port 7777; `--frames N --screenshot <png>` runs a self test and exits.

## Talk to it

Commands are JSON over HTTP: `POST http://127.0.0.1:7777/command` with `{"command":"scene.list","params":{}}`.
Prefer the CLI wrapper:

```powershell
AegisCli commands
AegisCli call scene.createEntity "{\"name\":\"Board\"}"
AegisCli call component.add "{\"uuid\":\"<uuid>\",\"type\":\"meshRenderer\"}"
AegisCli call editor.screenshot "{\"path\":\"_probe\/tetris.png\"}"
```

## Workflow for "build me a game"

1. `project.info` and `scene.list` first; never assume state.
2. `scene.createEntity` + `transform.set` for layout.
3. `component.add` / `component.set` for camera, lights, meshes, colliders, scripts.
4. `scene.save`, then `AegisCli scene <file>` to validate.
5. `editor.screenshot` and compare against intent; iterate.
6. `AegisCli export <project.aeproj> <outdir>` to ship.

## Gotchas

- Reference entities by UUID strings returned from createEntity / scene.list.
- Requests are marshalled onto the main thread; commands block but are safe.
- Invalid types or ranges return `{"ok":false,"error":...}`; read the error instead of guessing.
