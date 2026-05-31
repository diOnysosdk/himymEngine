---
name: Scene Block Editor
description: "Use for revision_libs/rev_editor/ and editor_app changes: scene blocks, image cues, shader cues, project assets, export (cues.txt), and preview rendering."
---
# Scene Block Editor

Use this skill for editor-side authoring work.

## Scope
- `revision_libs/rev_editor/src/editor_context.cpp` — core editor logic, LoadProject, ExportCues, RenderPreviewFrame
- `revision_libs/rev_editor/include/rev_editor.h` — ProjectData, SceneBlock, ImageCue, ShaderCue structs
- `examples/editor_app/main.cpp` — editor entry point, GDI+ init, ImGui integration
- `assets/cues.txt` — exported runtime data

## Non-negotiables
- Keep `assets_path` set on every `LoadProject` call: `{workspace}\{project_name}_assets`.
- Auto-create the assets folder via `CreateDirectoryA` when loading a project.
- Keep `ExportCues` image path format as `{project_name}_assets/{asset_key}` (workspace-relative).
- Keep `LoadImageTexture` called only after `GdiplusStartup` has been called in `main()`.
- GDI+ must use backslash separators in all paths.
- Keep the cues.txt image_cues format: `asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order` (9 fields).
- Keep `ProjectData.assets_path` memset-cleared in `CreateEditor`, `NewProject`, and `LoadProject`.
- Keep editor preview frame rendering: shader cue composited first, image cue overlaid on top.

## Project-specific assets workflow
1. User opens/creates `{name}.himym` or `{name}.json`
2. `LoadProject` extracts project name → creates `{workspace}\{name}_assets\` folder
3. User copies images into `{name}_assets/` folder
4. Image cues reference images by filename (`asset_key`) only
5. Editor builds full path as `{assets_path}\{asset_key}` for preview loading
6. Export writes `{name}_assets/{asset_key}` as `asset_path` in cues.txt

## Read-before-edit targets
- `revision_libs/rev_editor/include/rev_editor.h` for struct layout
- `revision_libs/rev_editor/src/editor_context.cpp` — LoadProject, ExportCues, RenderPreviewFrame
- `examples/minimal_intro/main.cpp` — `LoadImageCue` parser, must stay aligned with export format
- `PR/architecture/API-REFERENCE.md` when struct/API contracts change

## Pair with
- `Revision Runtime Core` when export semantics affect runtime behavior.
- `Revision Build Validation` when runtime files changed or build flow changed.