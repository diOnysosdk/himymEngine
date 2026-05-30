---
name: Python Editor Tooling Map
description: "Use when orienting on the Python editor toolchain, helper modules, and export/import script ownership before editing."
---
# Python Editor Tooling Map

Use this skill to map the Python-side authoring surface before making changes.

## Main Python tooling
- `tools/scene_block_editor.py`: main authoring UI, save/load/export flow, shader cue/curve wiring, scene and text/image authoring.
- `tools/scene_block_editor_*.py`: helper modules for policy, parsing, defaults, assets, state, and text logic.
- `tools/obj_to_meshbin.py`: OBJ/MTL to `.meshbin` converter for compact runtime mesh assets.
- `tools/shader_scene_importer.py`: GLSL import helper for split scene shader authoring.
- `tools/bake_text_to_png.py`: text asset baker for pre-rendered PNG overlays.

## Stable Python-side invariants
- Keep editor export deterministic and readable.
- Keep runtime-facing files aligned with the C++ loaders.
- Keep UI and export semantics explicit for timing, anchors, and authored defaults.
- Keep importer helpers dependency-light and compo-friendly.

## Pair with
- `Scene Block Editor` for main authoring workflow changes.
- `Python Editor Utilities` for importer, baker, and shader import helpers.
- `Revision Build Validation` for py_compile and pipeline checks.