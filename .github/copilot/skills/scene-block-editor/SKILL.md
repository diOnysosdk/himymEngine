---
name: Scene Block Editor
description: "Use for revision_libs/rev_editor/ and editor_app changes: scene blocks, image cues, music cues, mesh cues, shader cues, project assets, export (cues.txt), pack-build-run workflow, and preview rendering."
applyTo:
  - "revision_libs/rev_editor/**"
  - "examples/editor_app/**"
---
# Scene Block Editor

Use this skill for editor-side authoring work.

## Scope
- `revision_libs/rev_editor/src/editor_context.cpp` — core editor logic: `LoadProject`, `SaveProject`, `ExportProject`, `BuildAndRun`, `PackBuildAndRun`, `RenderPreviewFrame`, `AddMeshCue`, `DeleteMeshCue`, `RenderMeshModal`
- `revision_libs/rev_editor/include/rev_editor.h` — `ProjectData`, `SceneBlock`, `ShaderCue` structs; `ImageCue`/`TextCue`/`MusicCue`/`MeshCue` via `using rev::runtime::*` declarations
- `revision_libs/rev_runtime/include/rev_runtime.h` — source of truth for `ImageCue`, `TextCue`, `MusicCue`, `MeshCue`, `ColorRGB`, `ImageTexture`
- `examples/editor_app/main.cpp` — editor entry point, GDI+ init, ImGui integration

## Non-negotiables
- Keep `assets_path` set on every `LoadProject` call: `{workspace}\{project_name}_assets`.
- Auto-create the assets folder via `CreateDirectoryA` when loading a project.
- Keep `ExportProject` image path format as `{rel_assets_prefix}/{asset_key}` (workspace-relative forward slashes).
- Keep `LoadImageTexture` called only after `GdiplusStartup` has been called in `main()`.
- GDI+ must use backslash separators in all paths.
- Keep the cues.txt image_cues format: `asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end` (14 fields).
- Keep the cues.txt text_cues format: `text|font_name|x|y|size|color_r|color_g|color_b|effect_type|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order` (16 fields).
- Keep the cues.txt mesh_cues format: `asset_key|mesh_type|pos_x|pos_y|pos_z|rot_x|rot_y|rot_z|scale_x|scale_y|scale_z|color_r|color_g|color_b|color_a|mesh_size|mesh_param|effect_type|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order` (25 fields).
- Keep `ProjectData.assets_path` memset-cleared in `CreateEditor`, `NewProject`, and `LoadProject`.
- Keep editor preview frame rendering order: shader cue → image cue → text cue → mesh cue (last, with depth test).
- Do NOT define `ImageCue`, `TextCue`, `MusicCue`, `MeshCue` in `rev_editor.h` — they are imported from `rev_runtime.h` via `using` declarations.
- When adding fields to shared cue structs, update `rev_runtime.h` and `rev_runtime.cpp` first, then update `ExportProject` and parser in rev_runtime.

## Music cue behavior
- `MusicCue` fields: `asset_key[64]`, `asset_path[512]`, `cue_start`, `cue_end`.
- Browse dialog copies the `.xm` file to `{assets_path}\{filename}` and stores a workspace-relative forward-slash path in `asset_path`.
- `SaveProject` / `LoadProject` round-trip all four fields through JSON (`music_cues` array).
- `ExportProject` writes `asset_key|asset_path|cue_start|cue_end` to the `[music_cues]` section.
- `ImportFromCues` parses `[music_cues]` lines with the same pipe-split pattern.
- **If rev_pack.cpp changes, rebuild `editor_app` before running "Pack, Build and Run"** — rev_pack is statically linked into the editor binary.

## Mesh cue behavior
- `MeshCue` fields (all in `rev_runtime.h`): `asset_key[64]`, `mesh_type` (0=cube 1=sphere 2=plane 3=torus), `pos[3]`, `rot[3]`, `scale[3]`, `color[4]` (RGBA), `mesh_size`, `mesh_param`, `effect_type`, `cue_start`, `cue_end`, `fade_in_start`, `fade_in_end`, `fade_out_start`, `fade_out_end`, `layer_order`.
- `SceneBlock` holds `mesh_cues*` (capacity-doubled heap array), `mesh_cue_count`, `mesh_cue_capacity`.
- `AddMeshCue(SceneBlock*, const MeshCue&)` / `DeleteMeshCue(SceneBlock*, int)` manage the array.
- `EditorContext` has `mesh_shader` (Phong shader compiled at `InitializePreview`), `mesh_modal_open`, `mesh_modal_request_open`, `editing_mesh`.
- `RenderMeshModal(EditorContext*)` is the ImGui modal for editing a MeshCue.
- `RenderPreviewFrame` renders mesh cues last (after text), with depth test enabled (`glEnable(0x0B71)` / `glDisable(0x0B71)`).
- Phong shader vertex attribs: `a_pos` (loc 0), `a_normal` (loc 1), `a_uv` (loc 2); uniforms: `u_model`, `u_view`, `u_projection`, `u_light_pos`, `u_view_pos`, `u_color`.
- `glUniformMatrix4fv` must be loaded via `wglGetProcAddress` (not in Windows `<gl/gl.h>`).
- `ExportProject` writes `[mesh_cues]` section after `[music_cues]` with 25-field pipe format.
- `SaveProject` / `LoadProject` round-trip all MeshCue fields through JSON (`mesh_cues` array).

## Pack, Build and Run workflow
`PackBuildAndRun()` in `editor_context.cpp`:
1. `GetProjectCuesPath` → export `cues.txt` via `ExportProject`
2. `rev::pack::PackAssets(cues_path, build/packed_assets.h, cache, "")` — generates header with embedded assets + cues
3. `cmake --build build --config Release --target minimal_intro_packed`
4. Launches `build\bin\Release\minimal_intro_packed.exe`

The PRE_BUILD touch on `main.cpp` ensures MSBuild always recompiles main.cpp against the fresh header.

## Project-specific assets workflow
1. User opens/creates `{name}.json`
2. `LoadProject` extracts project name → creates `{workspace}\{name}_assets\` folder
3. Browse dialogs (image, music) copy files into `{name}_assets/`
4. Cues reference assets by filename (`asset_key`) only in project JSON
5. `ExportProject` builds workspace-relative paths for the runtime

## Read-before-edit targets
- `revision_libs/rev_runtime/include/rev_runtime.h` — struct layout (source of truth for ImageCue, TextCue, MusicCue, MeshCue)
- `revision_libs/rev_editor/include/rev_editor.h` — editor-specific struct layout (ShaderCue, ProjectData, SceneBlock)
- `revision_libs/rev_editor/src/editor_context.cpp` — `LoadProject`, `ExportProject`, `PackBuildAndRun`
- `revision_libs/rev_runtime/src/rev_runtime.cpp` — parsers must stay aligned with export format

## Pair with
- `Revision Runtime Core` when export semantics affect runtime behavior.
- `Revision Build Validation` when runtime files changed or build flow changed.
