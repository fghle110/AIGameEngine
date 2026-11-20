---
name: aegis-testing
description: Use when writing or running Aegis unit, integration and GPU tests.
---

# Testing Aegis

## Run

`powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\Build.ps1 -Configuration Debug -Test`
Filter with doctest: `build/windows-debug/bin/AegisTests.exe -tc="Scene:*"`.

## Conventions

- doctest, one file per area under `Tests/<Area>/`, include `"TestSupport.h"` before doctest.
- `TEST_CASE("Module: behaviour")`; cover success and failure paths.
- Unit tests never need a GPU or audio hardware; GPU tests return early with a WARN when Vulkan is unavailable, never pass silently.
- File tests write under `TestAssets/` (ctest sets the working directory to the binary folder).
- Absolute repo paths come from `AE_TEST_SOURCE_DIR`.

## Expectations

- Serializers round-trip; commands have failure-path tests; subsystems have integration tests that run the real update loop.
- The feature project is the end-to-end fixture: regenerate with `AegisCli demo Assets/Projects/FeatureTest`.
