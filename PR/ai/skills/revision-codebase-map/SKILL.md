---
name: revision-codebase-map
description: "Use when orienting on the C++ runtime layout, file ownership, and cross-module invariants before editing."
---
# Revision Codebase Map

Use this skill to map the C++ surface before making changes.

## Main C++ ownership
- `src/app`: entry point, startup gating, frame loop orchestration.
- `src/platform`: Win32 windowing, message pump, timing, cursor lifecycle.
- `src/audio`: XM decode, WinMM streaming, cue-driven music startup.
- `src/sequence`: phase graph, scene-cycle timing, exposure/fade, scene look config.
- `src/content`: authored-data loaders and embedded asset glue.
- `src/renderer`: WGL/OpenGL init, shader plumbing, optional 3D, overlays.

## Startup diagnostics ownership
- `src/app/main_win32.cc`: strict startup guard outcomes and top-level startup failure logging.
- `src/content/shader_pipeline_loader.cc`: `[shader_pipeline]` parsing/validation (dependency graph, optional-empty field handling, load rejection reasons).
- `tools/scene_block_editor.py`: pre-export validation that should reject malformed pipeline dependency chains before runtime launch.

## Stable invariants
- Keep the frame loop deterministic and explicit.
- Keep authored runtime assets embedded-first in release behavior.
- Keep authoring/export semantics aligned with runtime loaders.
- Keep heavyweight 3D state runtime-owned instead of parked on the stack.
- Treat build validation as part of the change, not a separate optional step.

## Pair with
- `Revision Runtime Core` for `src/...` runtime work.
- `Scene Block Editor` for `tools/scene_block_editor.py` and export semantics.
- `Shader Authoring` for GLSL and shader plumbing.
- `Revision Build Validation` for verification.