---
name: Scene Block Editor
description: "Use for tools/scene_block_editor.py and related authoring/export flow changes that touch scene settings, OBJ/MTL import, text/image cues, shader curves, or Do It All build/run behavior."
---
# Scene Block Editor

Use this skill for editor-side authoring work.

## Scope
- `tools/scene_block_editor.py`
- `tools/scene_block_editor_*.py`
- exported authored files such as `assets/scene_data.txt`, `assets/scene3d.txt`, `assets/image_cues.txt`, `assets/text_cues.txt`, `assets/shader_curves.txt`

## Non-negotiables
- Keep Active timing and Effect timing separate in the UI and export.
- Keep text kinds explicit: `title_main`, `credits_main`, `scroll_text`, `multiline_text`.
- Keep non-scroll text anchor semantics explicit: `x=0.5`, `y=0.5` means center.
- Keep 3D row export deterministic, objectized, and aligned with runtime consumption.
- Keep OBJ/MTL import and normal-map linking deterministic and lightweight.
- Keep `Do It All` as a direct save -> export -> configure -> build -> run workflow.
- Keep shader pipeline export dependency fields explicit: only the root scene pass may have a blank `dependencies` field; all other passes must name at least one dependency.
- Fail export/validation in the editor when pipeline dependency chains are malformed instead of deferring the failure to runtime startup.

## Read-before-edit targets
- `tools/scene_block_editor.py`
- `src/content/shader_pipeline_loader.cc` when changing `[shader_pipeline]` row format or dependency semantics
- the matching runtime loader or renderer for the exported data
- `docs/API-REFERENCE.md` when timing/export semantics are in play

## Pair with
- `Revision Runtime Core` when export semantics affect runtime behavior.
- `Revision Build Validation` when runtime files changed or build flow changed.