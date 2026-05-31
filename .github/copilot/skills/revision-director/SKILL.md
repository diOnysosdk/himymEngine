---
name: Revision Director
description: "Use for mixed runtime, editor, shader, mesh, customization, or docs work that needs routing across multiple project skills."
---
# Revision Director

Use this skill when a task crosses subsystem boundaries.

## Routing
- Use `Revision Runtime Core` for `examples/minimal_intro/main.cpp` and `revision_libs/rev_runtime/` changes.
- Use `Scene Block Editor` for `revision_libs/rev_editor/` and exported authored data (cues.txt format, export semantics, mesh cue authoring).
- Use `Shader Authoring` for GLSL and shader runtime contract changes.
- Use `Revision Build Validation` after the code changes are in place.
- Use `Revision Codebase Map` first when the correct owner is not obvious.

## Adding a new cue type (canonical pattern)
1. Define struct in `rev_runtime.h` — use `Revision Runtime Core`
2. Implement parser in `rev_runtime.cpp` — use `Revision Runtime Core`
3. Add using declaration in `rev_editor.h` — use `Scene Block Editor`
4. Add array fields to `SceneBlock`, context fields to `EditorContext` — use `Scene Block Editor`
5. Add `AddXxx`/`DeleteXxx` + modal + UI in `editor_context.cpp` — use `Scene Block Editor`
6. Add `ExportProject` section + `LoadProject` JSON round-trip — use `Scene Block Editor`
7. Add render block in `RenderPreviewFrame` — use `Scene Block Editor`
8. Add render block in `minimal_intro/main.cpp` — use `Revision Runtime Core`
9. Rebuild both `editor_app` and `minimal_intro` — use `Revision Build Validation`

## rev_runtime ownership rule
When adding a new field to `ImageCue`, `TextCue`, `MusicCue`, or `MeshCue`:
1. Edit `rev_runtime.h` (struct definition) — use `Revision Runtime Core`
2. Edit `rev_runtime.cpp` (parser implementation) — use `Revision Runtime Core`
3. Edit `editor_context.cpp` `ExportProject` — use `Scene Block Editor`
4. Rebuild both `editor_app` and `minimal_intro` — use `Revision Build Validation`
Do NOT touch `main.cpp` or `rev_editor.h` for struct changes.

## Coordination rules
- If authoring/export and runtime consumption both change, keep the semantics aligned in the same change set.
- If shader and runtime contracts change, update both layers together.
- If workspace customization files change, keep the agent, instruction, and skill surfaces synchronized.
- If a fix establishes a durable workflow rule or regression trap, sync a note into `/memories/repo/`.

## Output expectation
- Return one integrated result, even when multiple skills or domains are involved.
