---
name: Revision Runtime Core
description: "Use for C++ runtime work under src/app, src/platform, src/audio, src/sequence, src/content, and src/renderer that changes frame flow, cue semantics, cursor behavior, 3D timing, asset loading, or size-sensitive ownership."
---
# Revision Runtime Core

Use this skill for runtime changes in the C++ codebase.

## Scope
- `src/app/main_win32.cc`
- `src/platform/*`
- `src/audio/*`
- `src/sequence/*`
- `src/content/*`
- `src/renderer/*`

## Non-negotiables
- Keep the main loop deterministic: pump messages, get time, update sequence, resolve cues, render.
- Keep music cue-driven and silent by default until a valid authored cue becomes active.
- Keep cursor visibility local to the app window and safe across focus loss, alt-tab, and shutdown.
- Keep global `scene3d` timing continuous unless a task explicitly changes that contract.
- Keep optional 3D state runtime-owned when it is large enough to affect stack usage or PE size.
- Keep release behavior embedded-first for authored runtime assets.
- Keep strict startup guards explicit and diagnosable; when startup returns `1`, preserve the most specific failure reason in `build/runtime_startup.log`.
- For authored pipeline parsing (`[shader_pipeline]`), treat empty delimited fields as valid empty values where the format allows them; avoid scanset parsing patterns that collapse optional fields.

## Read-before-edit targets
- `docs/API-REFERENCE.md`
- `docs/ARCHITECTURE.md`
- `src/app/main_win32.cc`
- `src/content/shader_pipeline_loader.cc`
- the nearest owning loader or renderer file for the behavior being changed

## Pair with
- `Revision Codebase Map` for orientation.
- `Scene Block Editor` when runtime semantics must stay aligned with export.
- `Revision Build Validation` for build checks.