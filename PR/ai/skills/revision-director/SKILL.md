---
name: Revision Director
description: "Use for mixed runtime, editor, shader, customization, or docs work that needs routing across multiple project skills."
---
# Revision Director

Use this skill when a task crosses subsystem boundaries.

## Routing
- Use `Revision Runtime Core` for `examples/minimal_intro/main.cpp` and `revision_libs/rev_runtime/` changes.
- Use `Scene Block Editor` for `revision_libs/rev_editor/` and exported authored data (cues.txt format, export semantics).
- Use `Shader Authoring` for GLSL and shader runtime contract changes.
- Use `Revision Build Validation` after the code changes are in place.
- Use `Revision Codebase Map` first when the correct owner is not obvious.

## rev_runtime ownership rule
When adding a new field to `ImageCue`, `TextCue`, or `MusicCue`:
1. Edit `rev_runtime.h` (struct definition) — use `Revision Runtime Core`
2. Edit `rev_runtime.cpp` (parser implementation) — use `Revision Runtime Core`
3. Edit `editor_context.cpp` `ExportProject` — use `Scene Block Editor`
4. Rebuild both `editor_app` and `minimal_intro` — use `Revision Build Validation`
Do NOT touch `main.cpp` or `rev_editor.h` for struct changes.

## Coordination rules
- If authoring/export and runtime consumption both change, keep the semantics aligned in the same change set.
- If shader and runtime contracts change, update both layers together.
- If workspace customization files change, keep the agent, instruction, prompt, and skill surfaces synchronized.
- If runtime contracts change, keep parity docs synchronized in the same pass: architecture, API reference, code style notes, prompt/instruction guidance, and relevant skills.
- For imported glTF transparency/material behavior, verify both runtime and editor agree on texture alpha, slot color/texture usage, and opaque-before-transparent slot rendering.
- If launch fails after a successful build, route diagnostics first to editor workflow logs (`build/scene_block_editor_workflow.log`) and runtime startup logs (`build/runtime_startup.log`) before changing unrelated subsystems.

## Output expectation
- Return one integrated result, even when multiple skills or domains are involved.