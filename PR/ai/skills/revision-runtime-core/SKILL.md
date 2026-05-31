---
name: Revision Runtime Core
description: "Use for C++ runtime work in examples/minimal_intro/ that changes frame flow, cue loading, image rendering, shader dispatch, or asset path resolution."
---
# Revision Runtime Core

Use this skill for runtime changes in `examples/minimal_intro/main.cpp`.

## Scope
- `examples/minimal_intro/main.cpp` — standalone intro, loads `assets/cues.txt`
- `revision_libs/rev_platform/` — Win32 windowing, OpenGL context
- `revision_libs/rev_shader/` — shader compilation and dispatch

## Non-negotiables
- Keep the main loop deterministic: pump messages, get time, update cues, render.
- Keep GDI+ initialized (`GdiplusStartup`) before any `LoadImageTexture` call.
- Keep `LoadImageCue` parser field count aligned with the export format: currently 9 fields — `asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order`.
- Asset paths in `cues.txt` are workspace-relative (`{project_name}_assets/{key}`). The runtime resolves them to `../../../{asset_path}` from `build/bin/Release/`.
- Convert forward slashes to backslashes before passing paths to GDI+.
- Keep the global `g_logfile` (when present for debug) flushed and closed before exit.
- Keep cues.txt loaded from `assets/cues.txt` (relative to exe working directory, i.e., workspace root when launched via editor start scripts).

## Read-before-edit targets
- `assets/cues.txt` — verify field layout before changing any `sscanf_s` patterns
- `revision_libs/rev_editor/src/editor_context.cpp` — ExportCues() for export format source of truth
- `PR/architecture/API-REFERENCE.md`

## Image loading notes (GDI+)
- Error 3 from GDI+ = FileNotFound OR GDI+ not initialized — check both.
- GDI+ error 5 = access denied / wrong format.
- Always check `bitmap->GetLastStatus() == Gdiplus::Ok` after `new Gdiplus::Bitmap(wpath)`.

## Pair with
- `Revision Codebase Map` for orientation.
- `Scene Block Editor` when runtime semantics must stay aligned with export.
- `Revision Build Validation` for build checks.