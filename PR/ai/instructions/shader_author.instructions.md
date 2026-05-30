---
applyTo: assets/shader/**/*.glsl
description: "Use when editing split shader scene branches (`assets/shader/scenes/*.glsl`), shared shader glue (`shader_common.glsl`/`shader_footer.glsl`), GLSL grading/compositing, shader uniform plumbing, shader curve targets, or scene-id-to-shader behavior for the intro runtime."
---
# Shader Author Instructions

## Focus
- Keep shader changes deterministic, compact, and driven by existing authored controls.
- Preserve scene-id dispatch and fallback behavior unless the task explicitly changes both authoring and runtime.

## Control Contract
- `dyn0..dyn3` are artist-facing authored controls.
- Neutral/default curve value `0.5` must remain visually neutral.
- Do not hide baseline motion or intensity behind reset values.
- Treat `dyn0` as a speed control for integrated shader phase time; when touching shared glue,
  keep GLSL and runtime in sync with the `a_st` / `b_st` contract.
- Avoid phase formulas that multiply a changing dyn-speed factor against large absolute time
  (for example `time * speed * dyn_speed`) because they can create visible jitter pops.

## Runtime Contract
- Shader edits must stay aligned with runtime uniform bindings.
- If a uniform contract changes, update runtime plumbing in the same change set.
- If a shader id or curve target changes, update affected editor/export guidance too.

## Visual Discipline
- Prefer stable, projector-safe contrast and composition.
- Keep fallback branches graceful for unknown scene ids.
- Avoid shader bloat that adds cost without meaningful authored control.

## Validation
- For shader/runtime changes:
  - cmake -S . -B build
  - cmake --build build --config Release
- If editor/export behavior changed too:
  - python -m py_compile tools/scene_block_editor.py