---
name: Revision Director
description: "Use for mixed runtime, editor, shader, customization, or docs work that needs routing across multiple project skills."
---
# Revision Director

Use this skill when a task crosses subsystem boundaries.

## Routing
- Use `Revision Runtime Core` for `src/...` runtime changes.
- Use `Scene Block Editor` for `tools/scene_block_editor.py` and exported authored data.
- Use `Shader Authoring` for GLSL and shader runtime contract changes.
- Use `Revision Build Validation` after the code changes are in place.
- Use `Revision Codebase Map` first when the correct owner is not obvious.

## Coordination rules
- If authoring/export and runtime consumption both change, keep the semantics aligned in the same change set.
- If shader and runtime contracts change, update both layers together.
- If workspace customization files change, keep the agent, instruction, prompt, and skill surfaces synchronized.
- If launch fails after a successful build, route diagnostics first to editor workflow logs (`build/scene_block_editor_workflow.log`) and runtime startup logs (`build/runtime_startup.log`) before changing unrelated subsystems.

## Output expectation
- Return one integrated result, even when multiple skills or domains are involved.