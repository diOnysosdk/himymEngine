# Workspace Routing

- Skills are the primary reusable capability layer for this workspace.
- Use them for repeatable tasks before falling back to prompts:
  - `Revision Codebase Map` for C++ subsystem orientation and ownership
  - `Revision Runtime Core` for `src/app`, `src/platform`, `src/audio`, `src/sequence`, `src/content`, and `src/renderer`
  - `Scene Block Editor` for `tools/scene_block_editor.py` and export flow work
  - `Python Editor Tooling Map` for Python-side authoring helpers and script ownership
  - `Python Editor Utilities` for importer/baker helpers like `obj_to_meshbin.py`, `shader_scene_importer.py`, and `bake_text_to_png.py`
  - `Shader Authoring` for split GLSL and shader plumbing work
  - `Revision Build Validation` for build and compile checks
  - `Revision Director` for mixed tasks that need routing across domains
- Prompt files under `.github/prompts/` are legacy compatibility wrappers only; keep them thin and prefer skills for new work.

- Use the workspace specialists deliberately:
  - `Intro Runtime` for Win32/WGL runtime code, sequence/timeline flow, cue runtime semantics, cursor runtime behavior, optional 3D stage timing/material-slot/normal-map behavior, size-sensitive renderer ownership regressions, and loaders.
  - `Scene Block Editor` for `tools/scene_block_editor.py`, modal flows, OBJ/MTL import + normal-map sidecar linking, authoring UX, export formats, and editor-to-runtime timing semantics.
  - `Shader Author` for `assets/shader/scenes/*.glsl` plus `assets/shader/shader_common.glsl`/`shader_footer.glsl`, shader-id dispatch, dyn controls, grading, and shader plumbing.
  - `Revision Director` for mixed tasks that span runtime, editor, shader, or customization work and need routing/integration.

- For customization files under `.github/` or `AGENTS.md`, keep the agent/instruction/prompt set synchronized:
  - update descriptions when scope changes
  - add missing instruction files when a domain lacks one
  - add prompts only when they improve repeatable task entry

- If a task touches both authoring/export and runtime consumption, keep semantics aligned across both layers in the same change set when practical.
- When semantics change, update the relevant docs (`docs/ARCHITECTURE.md`, `docs/API-REFERENCE.md`, and related authored-data docs) in the same pass when practical.
- When a fix establishes a durable workflow rule or regression trap, sync a short verified note into `/memories/repo/`.

- Keep the workspace customization map simple: specialist agents handle domain work, the director agent routes multi-domain work, and file instructions define per-file constraints.