---
name: Scene Block Editor
description: "Use for revision_libs/rev_editor/ and editor_app changes: scene blocks, image cues, music cues, shader cues, project assets, export (cues.txt), pack-build-run workflow, and preview rendering."
applyTo:
  - "revision_libs/rev_editor/**"
  - "examples/editor_app/**"
---
# Scene Block Editor

Use this skill for editor-side authoring work.

## Scope
- `revision_libs/rev_editor/src/editor_context.cpp` — core editor logic: `LoadProject`, `SaveProject`, `ExportProject`, `BuildAndRun`, `PackBuildAndRun`, `RenderPreviewFrame`
- `revision_libs/rev_editor/include/rev_editor.h` — `ProjectData`, `SceneBlock`, `ShaderCue` structs; `ImageCue`/`TextCue`/`MusicCue` via `using rev::runtime::*` declarations
- `revision_libs/rev_runtime/include/rev_runtime.h` — source of truth for `ImageCue`, `TextCue`, `MusicCue`, `ColorRGB`, `ImageTexture`
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
- Keep editor preview frame rendering: shader cue composited first; then image/text/mesh cues rendered via a **unified sorted draw pass** (sorted by `layer_order` ascending — lower = further back). Do NOT restore the old three-block fixed order.
- Do NOT define `ImageCue`, `TextCue`, `MusicCue`, `MeshCue` in `rev_editor.h` — they are imported from `rev_runtime.h` via `using` declarations.
- When adding fields to shared cue structs, update `rev_runtime.h` and `rev_runtime.cpp` first, then update `ExportProject` and parser in rev_runtime.

## Music cue behavior
- `MusicCue` fields: `asset_key[64]`, `asset_path[512]`, `cue_start`, `cue_end`.
- Browse dialog copies the `.xm` file to `{assets_path}\{filename}` (same as images) and stores a workspace-relative forward-slash path in `asset_path`.
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
- `RenderPreviewFrame` renders image/text/mesh cues in a single loop sorted by `layer_order`. Blend and depth state are toggled lazily between sprite and mesh items. Depth test (`glEnable(0x0B71)`) is enabled only when a mesh item is about to draw; blend (`glEnable(GL_BLEND)`) is enabled only for sprite items.
- Phong shader vertex attribs: `a_pos` (loc 0), `a_normal` (loc 1), `a_uv` (loc 2); uniforms: `u_model`, `u_view`, `u_projection`, `u_light_pos`, `u_view_pos`, `u_color`.
- `glUniformMatrix4fv` must be loaded via `wglGetProcAddress` (not in Windows `<gl/gl.h>`).
- `ExportProject` writes `[mesh_cues]` section after `[music_cues]` with 25-field pipe format.
- `SaveProject` / `LoadProject` round-trip all MeshCue fields through JSON (`mesh_cues` array).

## Mesh texture loading (glTF/glb)
- When user browses for a `.glb` mesh file in the mesh modal, call `rev::gltf::LoadMesh(filepath, editor->project->assets_path)` to extract material properties, textures, and animations.
- **Always pass `assets_path` as the second parameter** to extract embedded textures from the glTF file. Without it, textures remain embedded and won't render.
- Extracted textures are written to `{project_name}_assets/` and the material struct contains relative paths in `base_color_texture`, `normal_texture`, `metallic_roughness_texture` fields.
- **Animations**: Check `ir->animation_count > 0` after loading. If present, transfer to mesh: `mesh->animation_data = ir->animations; mesh->animation_count = ir->animation_count; mesh->current_animation = 0; ir->animations = nullptr;`
- In `RenderPreviewFrame`, when rendering mesh type 4 (glTF):
  1. Load mesh via `rev::gltf::LoadMesh(cue->asset_path, editor->project->assets_path)`.
  2. If `ir->material.base_color_texture[0]` is not empty, load texture: `rev::runtime::LoadImageTexture(ir->material.base_color_texture, &tex)` and assign `mesh->base_color_texture = tex.texture_id`.
  3. **Animation evaluation**: Calculate `float anim_time = editor->current_time - cue->cue_start;` (scene-relative time). Handle looping if `mesh->animation_loop` is true. Call `rev::gltf::EvaluateAnimation(&anims[mesh->current_animation], anim_time, translation, rotation, scale);` then `rev::gltf::ApplyAnimationTransform(anim_pos, anim_rot, anim_scale, translation, rotation, scale);` to blend animation with cue transform.
  4. Before rendering, check if `mesh->base_color_texture != 0`. If so, bind texture: `glBindTexture(0x0DE1, mesh->base_color_texture)`, set shader uniforms `u_base_color_texture=0`, `u_has_texture=1`. Otherwise set `u_has_texture=0`.
- Mesh cache entries retain loaded textures and animations across frames — texture binding happens even for cached meshes.
- **Cache invalidation**: MeshCacheEntry includes `last_write_time` (Win32 FILETIME as uint64). Use `GetFileModificationTime(path)` to check if file changed, invalidate cache if mismatch.
- Mesh shader must support texture sampling: vertex shader passes `v_uv`, fragment shader samples `u_base_color_texture` when `u_has_texture` is 1.
- Procedural meshes (cube/sphere/plane/torus) have no textures or animations — always set `u_has_texture=0` for them.

## Timeline-driven animation playback
- **Key principle**: Animations evaluate based on `editor->current_time` (scene time), NOT an independent animation timer per mesh.
- When `editor->playing` is true, `UpdatePlayback(editor, delta_time)` advances `editor->current_time`.
- When user scrubs the timeline slider in the Preview panel, `editor->playing` pauses and `editor->current_time` is set directly.
- Animation evaluation in `RenderPreviewFrame`: `float anim_time = editor->current_time - cue->cue_start;` then evaluate at that time.
- **Stop button**: Sets `editor->playing = false` and `editor->current_time = 0.0f`. Animations automatically reset because they evaluate at scene time 0.
- **Mesh modal controls** (in `RenderMeshModal`): Show animation selection dropdown (if multiple animations), but NO independent time scrubber — animations are controlled by scene timeline.
- Do NOT call `UpdateMeshAnimation(mesh, dt)` in preview rendering — that would advance an independent timer. Instead, evaluate animation at scene-relative time directly via `EvaluateAnimation(..., anim_time, ...)`.
- Multiple animations: `mesh->current_animation` selects which animation to play (0-based index). User can change this in the mesh modal.

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
- `revision_libs/rev_runtime/include/rev_runtime.h` — struct layout (source of truth for ImageCue, TextCue, MusicCue)
- `revision_libs/rev_editor/include/rev_editor.h` — editor-specific struct layout (ShaderCue, ProjectData)
- `revision_libs/rev_editor/src/editor_context.cpp` — `LoadProject`, `ExportProject`, `PackBuildAndRun`
- `revision_libs/rev_runtime/src/rev_runtime.cpp` — `LoadImageCue`, `LoadTextCue` parsers must stay aligned with export format

## Pair with
- `Revision Runtime Core` when export semantics affect runtime behavior.
- `Revision Build Validation` when runtime files changed or build flow changed.