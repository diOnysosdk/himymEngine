---
name: Shader Authoring
description: "Use for split GLSL scene work, shader scene-id dispatch, dyn controls, grading/compositing, shader curve hookups, and shader runtime contract changes."
---
# Shader Authoring

Use this skill for shader and shader-plumbing work.

## Scope
- `assets/shader/scenes/*.glsl`
- `assets/shader/shader_common.glsl`
- `assets/shader/shader_footer.glsl`
- runtime shader plumbing in `src/renderer/*` when uniform or scene-id contracts change

## Non-negotiables
- Keep scene-id dispatch stable unless the runtime/editor contract changes with it.
- Keep `dyn0` neutral at `0.5` with no hidden baseline wobble.
- Preserve smooth `a_st` / `b_st` phase behavior instead of multiplying changing dyn-speed factors by large absolute time.
- Keep curve targets and editor labels aligned with runtime names.
- Keep unknown or unsupported scene ids deterministic in fallback behavior.

## Read-before-edit targets
- `docs/API-REFERENCE.md`
- `docs/ARCHITECTURE.md`
- `src/renderer/shader.h`
- `src/renderer/shader.cc`
- `src/content/shader_curve_loader.*` when curve targets change

## Pair with
- `Scene Block Editor` when shader curves or exported targets change.
- `Revision Runtime Core` when uniform bindings or scene dispatch change.
- `Revision Build Validation` after any shader/runtime edit.