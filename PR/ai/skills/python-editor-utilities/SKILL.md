---
name: Python Editor Utilities
description: "Use for Python helper scripts such as OBJ/MTL conversion, shader import, and text baking utilities that support the editor pipeline."
---
# Python Editor Utilities

Use this skill for the smaller Python helpers around the editor pipeline.

## Scope
- `tools/obj_to_meshbin.py`
- `tools/shader_scene_importer.py`
- `tools/bake_text_to_png.py`
- related helper UI/modules when the change is specific to one utility and not the main editor

## Non-negotiables
- Keep conversions deterministic.
- Keep generated assets compatible with the runtime loaders.
- Preserve lightweight workflows for compo usage.
- Keep imports and external dependencies minimal.

## Common change patterns
- OBJ/MTL material parsing and `.meshbin` packing.
- Shader source import into split scene-shader format.
- Text-to-PNG baking for authored overlays.
- Helper UI for importer-specific workflows.

## Pair with
- `Python Editor Tooling Map` for orientation.
- `Scene Block Editor` when helper changes affect the main authoring flow.
- `Revision Build Validation` for py_compile or pipeline checks.