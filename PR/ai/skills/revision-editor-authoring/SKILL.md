---
name: Scene Block Editor
description: "Use for revision_libs/rev_editor/ and editor_app changes: scene blocks, image cues, music cues, shader cues, project assets, export (cues.txt), pack-build-run workflow, and preview rendering."
applyTo:
  - "revision_libs/rev_editor/**"
  - "examples/editor_app/**"
---
# Scene Block Editor

Use this skill for editor-side authoring work.

## Scope
- `revision_libs/rev_editor/src/editor_context.cpp` — core editor logic: `LoadProject`, `SaveProject`, `ExportProject`, `BuildAndRun`, `PackBuildAndRun`, `RenderPreviewFrame`
- `revision_libs/rev_editor/include/rev_editor.h` — `ProjectData`, `SceneBlock`, `ImageCue`, `ShaderCue`, `MusicCue`, `TextCue` structs
- `examples/editor_app/main.cpp` — editor entry point, GDI+ init, ImGui integration

## Non-negotiables
- Keep `assets_path` set on every `LoadProject` call: `{workspace}\{project_name}_assets`.
- Auto-create the assets folder via `CreateDirectoryA` when loading a project.
- Keep `ExportProject` image path format as `{rel_assets_prefix}/{asset_key}` (workspace-relative forward slashes).
- Keep `LoadImageTexture` called only after `GdiplusStartup` has been called in `main()`.
- GDI+ must use backslash separators in all paths.
- Keep the cues.txt image_cues format: `asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order` (9 fields).
- Keep `ProjectData.assets_path` memset-cleared in `CreateEditor`, `NewProject`, and `LoadProject`.
- Keep editor preview frame rendering: shader cue composited first, image cue overlaid on top.

## Music cue behavior
- `MusicCue` fields: `asset_key[64]`, `asset_path[512]`, `cue_start`, `cue_end`.
- Browse dialog copies the `.xm` file to `{assets_path}\{filename}` (same as images) and stores a workspace-relative forward-slash path in `asset_path`.
- `SaveProject` / `LoadProject` round-trip all four fields through JSON (`music_cues` array).
- `ExportProject` writes `asset_key|asset_path|cue_start|cue_end` to the `[music_cues]` section.
- `ImportFromCues` parses `[music_cues]` lines with the same pipe-split pattern.
- **If rev_pack.cpp changes, rebuild `editor_app` before running "Pack, Build and Run"** — rev_pack is statically linked into the editor binary.

## Pack, Build and Run workflow
`PackBuildAndRun()` in `editor_context.cpp`:
1. `GetProjectCuesPath` → export `cues.txt` via `ExportProject`
2. `rev::pack::PackAssets(cues_path, build/packed_assets.h, cache, "")` — generates header with embedded assets + cues
3. `cmake --build build --config Release --target minimal_intro_packed`
4. Launches `build\bin\Release\minimal_intro_packed.exe`

The PRE_BUILD touch on `main.cpp` ensures MSBuild always recompiles main.cpp against the fresh header.

## Project-specific assets workflow
1. User opens/creates `{name}.json`
2. `LoadProject` extracts project name → creates `{workspace}\{name}_assets\` folder
3. Browse dialogs (image, music) copy files into `{name}_assets/`
4. Cues reference assets by filename (`asset_key`) only in project JSON
5. `ExportProject` builds workspace-relative paths for the runtime

## Read-before-edit targets
- `revision_libs/rev_editor/include/rev_editor.h` for struct layout
- `revision_libs/rev_editor/src/editor_context.cpp` — `LoadProject`, `ExportProject`, `PackBuildAndRun`
- `examples/minimal_intro/main.cpp` — `LoadMusicCue`, `LoadImageCue` parsers must stay aligned with export format

## Pair with
- `Revision Runtime Core` when export semantics affect runtime behavior.
- `Revision Build Validation` when runtime files changed or build flow changed.