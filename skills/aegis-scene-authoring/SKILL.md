---
name: aegis-scene-authoring
description: Use when creating or editing Aegis scenes, prefabs and materials (JSON formats).
---

# Authoring Aegis scenes

## Format

Scenes are JSON (`*.aescene`, version 1) produced by `SceneSerializer`. Root: `{version, scene:{uuid,name,environment}, entities:[...]}`.
Entity: `uuid`, `name`, `active`, `transform` (local position, rotation xyzw, scale, parent, children) plus optional `camera`, `light`, `meshRenderer`, `rigidBody`, `collider`, `luaScript`, `audioSource`, `audioListener`, `flyCamera`.
Assets are `{"id":"<uuid>","type":"mesh|material|script|audio"}`.

## Rules

- Only local transforms are stored; world transforms are derived after loading.
- UUIDs must be unique; broken parent links are dropped with a warning.
- Unknown members are ignored on load and dropped on rewrite.
- Environment (exposure, shadows, SSAO, bloom, HDRI) lives under `scene.environment`.

## Verify

`AegisCli scene <file.aescene>`; the test `Feature project: manifest and scene load with every component` checks the full fixture in `Assets/Projects/FeatureTest`.
