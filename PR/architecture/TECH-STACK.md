# Technology Stack

## Strategy Overview

This project uses one shared runtime architecture with profile-specific rendering and size policies.

- Demo profile: modern graphics path on Windows 11 compo machine
- Intro profile (64k/128k/256k): strict size-first runtime path
- Shared policy: deterministic execution, static ownership, no engine bloat

## Language and Platform

- Language: C++17 (MSVC)
- Platform: Windows (Win32 API)
- Build system: CMake 3.x
- Build tool: Visual Studio/MSBuild

## Core Subsystems

### Platform (Win32)

- Purpose: windowing, message pump, timing, input
- API: Win32 native calls
- Cursor policy: authored show/hide requests are applied safely; the OS cursor is only hidden while the intro window is active/foreground and is restored on focus loss or shutdown
- Rule: explicit ownership and direct flow, no platform abstraction layer

### Audio

- Format: XM only
- Decoder: static integration (no runtime DLL probing)
- Playback: Win32-native WinMM path
- Startup policy: cue-driven and silent by default until an authored music cue becomes active
- Source policy:
  - Primary: embedded authored asset by `audio_key`
  - Fallback: filesystem XM for dev iteration only behind diagnostics guards
- Timing: monotonic playback clock independent of scene-cycle wrap

### Renderer

- Shared rule: avoid fake cross-API abstraction that hides cost.

Current renderer path:
- API: OpenGL via Win32/WGL
- Optional mesh rendering with PBR materials and texture support
- glTF 2.0 (.glb) mesh import via cgltf (editor-side only)
- Procedural meshes: cube, sphere, plane, torus
- Texture support: Base color (diffuse) textures from Blender materials
- Material properties: base color factor, metallic, roughness
- Phong-based lighting with configurable parameters
- Goal: robust deterministic startup/shutdown with one explicit render path

Intro build emphasis:
- Minimal size-first path with strict import budget
- Prefer compact code and static data over generalized runtime systems
- Goal: preserve compression ratio and executable budget

### Sequence and Timeline

- Purpose: deterministic phase progression and content parameter updates
- Model: direct frame-time updates with explicit phase boundaries
- Cue timing model: scene-local sequence state plus scene-cycle absolute timing for cross-scene cue evaluation
- Rule: no hidden scheduler complexity

### Content and Assets

- Asset strategy: embedded payloads by default for deterministic release behavior
- Tooling: offline generation pipeline for embedded headers/data
- Rule: no runtime downloads, no network dependency

Intro/release sourcing policy:
- Authored runtime assets are embedded-only in intro/release behavior.
- Filesystem fallbacks are diagnostics-only behind compile-time guards.

Text object semantics:
- Runtime text objects are authored as `title_main`, `credits_main`, `scroll_text`, and `multiline_text`.
- Text cues keep Active timing separate from Effect timing.
- For non-scroll text objects, normalized `x`/`y` are anchor coordinates (`0.5`, `0.5` = screen center).

Mesh and texture workflow:
- Meshes imported from Blender via glTF 2.0 (.glb) format
- Embedded textures extracted to project assets folder at import time
- Editor calls `rev::gltf::LoadMesh(path, assets_path)` to parse geometry and extract textures
- Runtime loads mesh geometry from packed data, textures from extracted files
- Texture requirement: Image Texture node connected to Principled BSDF Base Color in Blender
- Supported: base color textures, metallic/roughness factors, procedural meshes (cube/sphere/plane/torus)

### Application Entry

- Purpose: subsystem init and main loop orchestration
- Loop contract: pump messages, sync time, update sequence, render, present

## Dependency Policy

Runtime dependency policy:
- Do not assume extra runtimes/redists installed on compo machine.
- Ship required local dependencies inside archive when unavoidable.
- Keep import surface minimal, especially for intro profiles.

Development dependency policy:
- CMake for build generation
- Python tooling allowed for offline asset generation
- Optional packing tools for release optimization

## Build Artifacts

Primary output:
- Monolithic release executable under build/Release/

Generated content:
- Embedded asset headers generated at build/prebuild time
- CMake-generated project files

## Competition Constraints Mapping

General:
- Windows 11 64-bit compo environment
- Latest NVIDIA WHQL driver path
- Display target: 1920x1080 at 60 Hz, 16:9
- ESC or Alt+F4 must terminate instantly
- Submission archive must be one .zip or .rar

Category limits:
- Demo: no max file size, max runtime 8 minutes
- 64k intro: 65536-byte executable limit, max runtime 8 minutes
- 128k intro: 131072-byte executable limit, max runtime 8 minutes
- 256k intro: 262144-byte executable limit, max runtime 8 minutes

## Optimization Strategy

| Priority | Technique | Profile Focus |
|----------|-----------|----------------|
| Size | Static embedding and compile-time flags | Intro strong, Demo moderate |
| Size | Minimize imports and runtime branches | Intro critical |
| Compression | Deterministic control flow and data layout | Intro critical |
| Reliability | Explicit startup path and safe defaults | Demo and Intro |
| Runtime clarity | Linear frame loop with direct subsystem calls | Demo and Intro |

## Code Style

- Functions and types: PascalCase
- Variables: lower_snake_case
- Members: trailing underscore
- Constants: kPascalCase
- Macros: ALL_CAPS only when necessary
- Files: snake_case

## Build Command

```powershell
cmake -S . -B build
cmake --build build --config Release
```

### Asset Embedding Performance

Binary asset hex encoding (images, meshbin) is done by a Python `execute_process` call during CMake configure rather than a CMake `foreach` loop. The CMake loop was O(filesize) in CMake script and caused multi-minute hangs for large files; Python does the same encoding in milliseconds.

### Incremental Configure (Embed Fingerprinting)

`tools/scene_block_editor.py` maintains `build/embed_fingerprint.json` — a SHA-1 fingerprint of all CMake embed inputs (authored text assets, shader GLSL sources, binary asset files, CMakeLists.txt, active project JSON) plus current build flags. If the fingerprint matches when Do It All or Build is invoked, the cmake configure step is skipped entirely. The fingerprint is written after every successful configure and invalidated by any asset, source, flag, or project JSON change.

Profile preset commands:

```powershell
cmake --preset demo ; cmake --build --preset demo
cmake --preset intro64k ; cmake --build --preset intro64k
cmake --preset intro128k ; cmake --build --preset intro128k
cmake --preset intro256k ; cmake --build --preset intro256k
```

## Build Profiles and Flags

Use explicit CMake cache flags to keep profile behavior deterministic and reviewable.

- `REV_PROFILE`:
  - `demo`
  - `intro64k`
  - `intro128k`
  - `intro256k`
- `REV_ENABLE_3D`:
  - `OFF` for lean builds
  - `ON` when the optional scene3d stage/mesh path is needed
- `REV_RENDER_GL`:
  - `ON` for current workspace builds
- `REV_ENABLE_DIAGNOSTICS`:
  - `OFF` in normal workspace builds so release compiles stay quiet
  - should only be enabled intentionally for local debugging/fallback work
- `REV_INTRO_SIZE_LIMIT`:
  - `0` for `demo`
  - `65536` for `intro64k`
  - `131072` for `intro128k`
  - `262144` for `intro256k`

These flags are part of the build contract and should not be replaced with runtime configuration files.

## Guardrails

- No plugin architecture, runtime module discovery, scripting, or reflection.
- No speculative framework abstractions that increase binary size.
- Keep profile differences explicit and compile-time controlled.

## Standalone Editor Completion Scope (No Bloat)

Use these rules while finishing `himym_editor`:

- Ship only required authoring workflow coverage: create/import/edit/save/export/build/launch.
- Prefer direct fixes in existing editor/export files over introducing new subsystems.
- Keep typed layer + asset inspector flow explicit; avoid generic tool frameworks.
- Add no new third-party runtime/editor dependencies.
- Keep diagnostics lightweight and optional.

Out of scope for completion:

- Plugin or extension architecture for editor features.
- Background service/process managers.
- Runtime configuration systems replacing compile-time profile flags.
- Cosmetic-only feature work that does not improve correctness or reliability.

## Standalone Editor Done Checklist

The editor completion pass is done only when all items below pass:

- Reliability: GUI and headless create/import/save/export flows complete without unexpected shutdown.
- Semantic parity: exported runtime files preserve current cue/timing/text/stage3d semantics.
- Determinism: repeated export from unchanged project keeps stable output ordering.
- Workflow: `Do It All` remains explicit and ordered as save -> export -> build -> launch.
- Build discipline: no new dependencies, no generalized abstractions, no plugin behavior.
- Validation: `cmake --build build --config Release --target himym_editor` succeeds.
- Smoke tests: headless import/export commands return zero exit code on fixture data.

