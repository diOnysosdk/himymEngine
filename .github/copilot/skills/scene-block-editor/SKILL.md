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
- Keep the cues.txt shader_cues format: 42 pipe-separated fields (25 base + 17 curve indices). Includes palette colors, speed, intensity, warp, exposure, fade, timing, layer controls, and curve assignments for all animatable parameters.
- Keep the cues.txt image_cues format: `asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|curve_x|curve_y|curve_scale|curve_opacity` (18 fields with 4 curve indices).
- Keep the cues.txt text_cues format: `text|font_name|x|y|size|color_r|color_g|color_b|effect_type|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order|curve_x|curve_y|curve_size|curve_color_r|curve_color_g|curve_color_b` (22 fields with 6 curve indices).
- Keep the cues.txt mesh_cues format: `asset_key|asset_path|mesh_type|pos_x|pos_y|pos_z|rot_x|rot_y|rot_z|scale_x|scale_y|scale_z|color_r|color_g|color_b|color_a|mesh_size|mesh_param|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|metallic|roughness` + 16 curve indices (44 fields total). mesh_type 4 = external glTF/GLB (asset_path holds workspace-relative path).
- Keep `ProjectData.assets_path` memset-cleared in `CreateEditor`, `NewProject`, and `LoadProject`.
- Keep editor preview frame rendering order: shader cue → image cue → text cue → mesh cue (last, with depth test).
- Do NOT define `ImageCue`, `TextCue`, `MusicCue`, `MeshCue` in `rev_editor.h` — they are imported from `rev_runtime.h` via `using` declarations.
- When adding fields to shared cue structs, update `rev_runtime.h` and `rev_runtime.cpp` first, then update `ExportProject` and parser in rev_runtime.
- **Auto-save pattern**: All modals (Shader, Image, Text, Mesh, Music) use an `AutoSave()` lambda that immediately copies `editing_cue` to `scene->cues[index]` and sets `modified` flag on every UI control change. No Apply/Cancel workflow — changes persist instantly.

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

## Curve Animation System
- **Curve library**: `rev::curve` namespace provides `Curve`, `Point`, `EaseMode` (Linear, EaseIn, EaseOut, EaseInOut, Smoothstep, Hold), `CreateCurve`, `AddPoint`, `Evaluate`, `SortPoints`.
- **Curve storage**: `ProjectData` holds up to 32 curves in `curves[32]`, tracked by `curve_count`. Each curve is a standalone animation.
- **Curve assignment**: Each animatable parameter has an `int curve_*` field (e.g., `curve_speed`, `curve_palette_low_r`). Value `-1` means no curve; `0-31` is the curve index.
- **ShaderCue curves** (17 total): `curve_speed`, `curve_intensity`, `curve_warp`, `curve_exposure`, `curve_fade`, `curve_palette_low_r/g/b`, `curve_palette_mid_r/g/b`, `curve_palette_high_r/g/b`, `curve_opacity`, `curve_exposure_ramp`, `curve_fade_ramp`.
- **ImageCue curves** (4 total): `curve_x`, `curve_y`, `curve_scale`, `curve_opacity`.
- **TextCue curves** (6 total): `curve_x`, `curve_y`, `curve_size`, `curve_color_r`, `curve_color_g`, `curve_color_b`.
- **MeshCue curves** (16 total): `curve_pos_x/y/z`, `curve_rot_x/y/z`, `curve_scale_x/y/z`, `curve_color_r/g/b/a`, `curve_metallic`, `curve_roughness`.
- **Curve initialization**: All curve fields MUST be initialized to `-1` (no curve) in struct constructors and JSON loaders. Zero-init causes all params to incorrectly reference curve 0.
- **Curve modal UI**: Each animatable parameter has a `+` button. Click creates a new curve with two points (start/end at current value), assigns curve index, opens curve editor modal.
- **Curve editor modal**: `EditorContext` has `curve_editor_modal_request_open`, `editing_curve_index`, `editing_curve_cue_type` (0=shader, 1=image, 2=text, 3=mesh), `editing_curve_label`. Modal shows canvas for point visualization/editing.
- **Point editing**: Click to select, drag to move, double-click to open point properties modal, right-click to delete (min 2 points required).
- **Point properties modal**: `EditorContext.point_properties_modal_open`, edits `Time` (DragFloat 0.0-1.0), `Value` (DragFloat unclamped -FLT_MAX to FLT_MAX for rotations/positions), `EaseMode` dropdown.
- **Endpoint locking**: First point locked at t=0.0, last point locked at t=1.0. Time field is disabled in point properties modal for endpoints. Dragging endpoints only moves vertically (value), not horizontally (time).
- **Curve duration**: Separate field in curve editor controls playback speed (how many seconds the 0-1 range represents).
- **Curve deletion**: Delete button in curve editor opens confirmation modal. On confirm, resets matching curve field to `-1` in the active cue, keeping UI in sync.
- **Curve validation**: All curve buttons validate `curve_field >= 0 && curve_field < curve_count` before opening editor (prevents "invalid curve index" errors).
- **Preview evaluation**: `RenderPreviewFrame` evaluates all assigned curves at `elapsed_time / curve.duration` and uses animated values for uniforms/transforms.
- **Runtime evaluation**: `minimal_intro.exe` evaluates curves the same way during render (e.g., shader palette colors, mesh rotation).
- **JSON round-trip**: `SaveProject` writes all curve fields per cue, `LoadProject` reads them back (must initialize to `-1` first, then parse).
- **Export format**: Curve indices and shader transform fields are appended to existing cue fields in cues.txt (shader_cues: 51 fields, image_cues: 18 fields, text_cues: 22 fields, mesh_cues: 44 fields).

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
