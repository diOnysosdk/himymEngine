# Workspace Routing

- Skills are the primary reusable capability layer for this workspace.
- Use them for repeatable tasks before falling back to prompts:
  - `Revision Codebase Map` for C++ subsystem orientation and ownership
  - `Revision Runtime Core` for `examples/minimal_intro/main.cpp`, `revision_libs/rev_runtime/` (shared cue structs, loaders, texture helpers, Mat4 math)
  - `Scene Block Editor` for `revision_libs/rev_editor/**`, `examples/editor_app/**`, music/image/mesh cue workflows, export (cues.txt), and pack-build-run flow
  - `Shader Authoring` for split GLSL and shader plumbing work
  - `Revision Build Validation` for build and compile checks
  - `Revision Director` for mixed tasks that need routing across domains

- Use the workspace specialists deliberately:
  - `@runtime-dev` for `revision_libs/rev_runtime/`, `examples/minimal_intro/`, cue loading (image/text/music/mesh), GDI+ texture helpers, Mat4 math, and packed-build behavior.
  - `@editor-dev` for `revision_libs/rev_editor/**`, `examples/editor_app/**`, modal flows, image/music/mesh cue Browse+copy behavior, export (cues.txt), pack-build-run workflow, and editor-to-runtime timing semantics.
  - `@mesh-graphics` for `revision_libs/rev_mesh/` procedural geometry, Phong shader plumbing, VAO/VBO/IBO management, and MeshCue render integration.
  - `@shader-author` for `revision_libs/rev_shader/`, GLSL shaders, shader-id dispatch, and shader plumbing.
  - `@build-system` for CMake configuration, target linking, and build flags.
  - `@director` for mixed tasks that span runtime, editor, shader, mesh, or customization work.

- For customization files under `.github/` keep the agent/instruction/skill set synchronized:
  - update descriptions when scope changes
  - add missing instruction files when a domain lacks one

- If a task touches both authoring/export and runtime consumption, keep semantics aligned across both layers in the same change set.
- When adding a new cue type, follow the established pattern: struct in `rev_runtime.h` → parser in `rev_runtime.cpp` → ExportProject in `editor_context.cpp` → LoadProject round-trip → render in RenderPreviewFrame → render in `minimal_intro/main.cpp`.
- When semantics change, update the relevant docs in the same pass.
- When a fix establishes a durable workflow rule or regression trap, sync a short note into `/memories/repo/`.

- Keep the workspace customization map simple: specialist agents handle domain work, `@director` routes multi-domain work, and file instructions define per-file constraints.
