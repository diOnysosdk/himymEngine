---
name: Revision Build Validation
description: "Use for the build and compile checks that verify C++ runtime, editor, shader, and customization changes."
---
# Revision Build Validation

Use this skill to validate changes with the smallest useful command set.

## Default checks
- `cmake -S . -B build`
- `cmake --build build --config Release`

## Add when relevant
- `cmake -S . -B build -DREV_ENABLE_3D=ON ; cmake --build build --config Release` when optional 3D or mesh renderer code changed.
- `python -m py_compile tools/scene_block_editor.py` when editor code changed.
- Use the editor `Do It All` path when workflow behavior changes (save/export/configure/build/run), and inspect `build/scene_block_editor_workflow.log` for per-step failures.
- If `intro.exe` exits immediately after a successful build, inspect `build/runtime_startup.log` before broadening investigation.

## Validation rules
- Prefer the narrowest command that can falsify the current hypothesis.
- Do not widen the validation scope before the first focused check.
- Treat build results as part of the implementation, not a postscript.