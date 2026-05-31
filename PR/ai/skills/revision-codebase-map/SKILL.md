---
name: revision-codebase-map
description: "Use when orienting on the himym project layout, file ownership, and cross-module invariants before editing."
---
# Revision Codebase Map

Use this skill to map the project surface before making changes.

## Directory layout
```
revision_libs/
  rev_editor/        C++ editor library (scene blocks, image cues, shader cues, export)
    include/         rev_editor.h — ProjectData, SceneBlock, ImageCue, ShaderCue structs
    src/             editor_context.cpp — core editor logic, preview, image loading
  rev_platform/      Win32 windowing, OpenGL context, GLFW/Win32 backend
  rev_shader/        Shader compilation, uniform binding
  rev_curve/         Curve/animation system
  rev_sequence/      Timeline sequencing
examples/
  editor_app/        main.cpp — editor entry point; GDI+ init, ImGui loop
  minimal_intro/     main.cpp — standalone compiled intro; reads assets/cues.txt
assets/
  cues.txt           Exported runtime data (shader_cues, image_cues, text_cues, etc.)
{project_name}_assets/
                     Per-project image/asset folder (created on LoadProject)
PR/
  ai/                Agent definitions, skills, instructions
  architecture/      ARCHITECTURE.md, API-REFERENCE.md
```

## Key struct relationships
- `ProjectData` owns `scenes[]` (SceneBlock), each scene has `image_cues[]` (ImageCue) and `shader_cues[]` (ShaderCue)
- `ProjectData.assets_path` = `{workspace}\{project_name}_assets` — set on LoadProject, folder auto-created
- `ProjectData.project_path` and `.workspace_path` — set from the .himym/.json file path

## cues.txt format
```
[image_cues]
asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order
```
- `asset_path` = `{project_name}_assets/{asset_key}` (relative from workspace root)
- Runtime (minimal_intro) resolves to `../../../{asset_path}` from `build/bin/Release/`

## Image loading invariants
- GDI+ must be initialized (`GdiplusStartup`) before any `LoadImageTexture` call
- GDI+ requires backslash path separators on Windows
- editor_app and minimal_intro both init GDI+ in their respective `main()`

## Stable invariants
- Keep the frame loop deterministic and explicit.
- Keep authoring/export semantics aligned with runtime loaders.
- Keep `LoadImageCue` parser field count matching the export format (currently 9 fields with `layer_order`).
- Treat build validation as part of the change, not a separate optional step.

## Pair with
- `Revision Runtime Core` for `examples/minimal_intro/` runtime changes.
- `Scene Block Editor` for `revision_libs/rev_editor/` and export semantics.
- `Shader Authoring` for GLSL and shader plumbing.
- `Revision Build Validation` for verification.