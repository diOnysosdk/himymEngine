---
name: Revision Runtime Core
description: "Use for C++ runtime work in examples/minimal_intro/ that changes frame flow, cue loading, image/music/mesh rendering, shader dispatch, asset path resolution, or packed build behavior."
---
# Revision Runtime Core

Use this skill for runtime changes in `examples/minimal_intro/main.cpp`.

## Scope
- `examples/minimal_intro/main.cpp` — standalone intro, loads cues from file or embedded data; does NOT define cue structs or loader functions (those live in rev_runtime)
- `revision_libs/rev_runtime/` — shared static lib: cue structs, `ComputeEffectOpacity`, `LoadImageTexture`, `LoadImageTextureFromMemory`, `RenderTextToTexture`, `LoadImageCue`, `LoadTextCue`, `LoadMusicCue`, `LoadMeshCue`, and 8 Mat4 math functions
- `revision_libs/rev_mesh/` — procedural mesh lib: `CreateCube/Sphere/Plane/Torus`, `UploadToGPU`, `Render`, `DestroyMesh` (linked into both editor and minimal_intro)
- `revision_libs/rev_platform/` — Win32 windowing, OpenGL context
- `revision_libs/rev_shader/` — shader compilation and dispatch
- `revision_libs/rev_xm/` — XM music player (wraps libxm-windows)

## Non-negotiables
- Keep the main loop deterministic: pump messages, get time, update cues, render.
- Keep GDI+ initialized (`GdiplusStartup`) before any `LoadImageTexture` call.
- Keep `LoadShaderCue` parser field count aligned with the export format: currently 42 fields (25 base + 17 curve indices) — palette colors, speed, intensity, warp, exposure, fade, timing, layer controls, and curve assignments.
- Keep `LoadImageCue` parser field count aligned with the export format: currently 18 fields (14 base + 4 curve indices) — `asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|curve_x|curve_y|curve_scale|curve_opacity`.
- Keep `LoadTextCue` parser field count aligned with the export format: currently 22 fields (16 base + 6 curve indices).
- Keep `LoadMeshCue` parser field count aligned with the export format: currently 44 fields (28 base + 16 curve indices) — `asset_key|asset_path|mesh_type|pos_x|pos_y|pos_z|rot_x|rot_y|rot_z|scale_x|scale_y|scale_z|color_r|color_g|color_b|color_a|mesh_size|mesh_param|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|metallic|roughness` + 16 curve indices. mesh_type 4 = glTF/GLB external file (asset_path). Backward compatible: fewer fields default missing curves to -1.
- **Curve evaluation**: All cue types support curve animation. Pattern: `if (cue.curve_param >= 0 && cue.curve_param < curve_count) { float t = (time - cue.cue_start) / curves[cue.curve_param].duration; animated_value = rev::curve::Evaluate(curves[cue.curve_param], t); }`. Use animated values for shader uniforms, sprite transforms, mesh transforms, colors, etc.
- **Curve initialization**: All curve fields default to `-1` (no curve). Parser uses `sscanf_s` with parsed field count validation for backward compatibility.
- Do NOT redefine `ImageCue`, `TextCue`, `MusicCue`, `MeshCue`, `ShaderCue`, `ImageTexture`, `TextTexture` in `main.cpp` — they come from `rev_runtime.h` via `using` declarations.
- Do NOT redefine cue loaders or `Mat4*` functions in `main.cpp` — they are implemented in `rev_runtime.cpp`.
- `glUniformMatrix4fv` and other GL 2.0+ functions are NOT in Windows `<gl/gl.h>`; load via `wglGetProcAddress` before first use.
- For 3D mesh rendering: enable depth test (`glEnable(0x0B71)`) before mesh draw, disable after (`glDisable(0x0B71)`) to avoid breaking the 2D sprite pass.
- Asset paths in `cues.txt` are workspace-relative (`{project_name}_assets/{key}`). CWD is always workspace root (walk-up-3 from exe at startup).
- Convert forward slashes to backslashes before passing paths to GDI+.
- `TextCue.size` is `float` — do not cast to `int` at call sites.
- `TextCue.color` is `ColorRGB color` — access as `cue.color.r/g/b`.

## Mat4 math (rev_runtime.cpp, namespace rev::runtime)
- 8 canonical column-major matrix functions for OpenGL: `Mat4Identity`, `Mat4Perspective`, `Mat4LookAt`, `Mat4Translate`, `Mat4RotateEuler`, `Mat4Scale`, `Mat4Multiply`, `Mat4Model`
- `Mat4Model(pos, rot, scale)` builds a combined TRS matrix.
- All return `float[16]` column-major (OpenGL convention).
- Use `using rev::runtime::Mat4Perspective;` etc. in main.cpp — do NOT copy implementations.

## 3D mesh render pattern (minimal_intro/main.cpp)
```cpp
// After text/image rendering:
if (has_mesh && elapsed >= mesh_cue.cue_start && elapsed < mesh_cue.cue_end) {
    glEnable(0x0B71); // GL_DEPTH_TEST
    auto mesh_obj = rev::mesh::CreateCube(mesh_cue.mesh_size);  // or Sphere/Plane/Torus
    rev::mesh::UploadToGPU(mesh_obj);
    float model[16], view[16], proj[16], mvp[16];
    Mat4Model(mesh_cue.pos, mesh_cue.rot, mesh_cue.scale, model);
    Mat4LookAt({0,0,5}, {0,0,0}, {0,1,0}, view);
    Mat4Perspective(45.0f, aspect, 0.1f, 100.0f, proj);
    Mat4Multiply(proj, view, mvp); Mat4Multiply(mvp, model, mvp);
    // bind mesh_shader, set u_model/u_view/u_projection/u_color/u_light_pos/u_view_pos
    rev::mesh::Render(mesh_obj, -1);
    rev::mesh::DestroyMesh(mesh_obj);
    glDisable(0x0B71);
}
```

## CWD walk-up invariant
`main()` walks up 3 dirs from the exe location (`build/bin/Release/ → workspace root`) via `GetModuleFileNameA` + `SetCurrentDirectoryA`. All relative paths are then workspace-relative. Do not change or remove this.

## Packed build (`HIMYM_PACKED_ASSETS`)
- When defined, all assets come from `kPackedAssets[]` / `kPackedCuesContent[]` in `packed_assets.h` (generated by rev_pack into `build/`).
- `HIMYM_HAS_PACKED_CUES` is defined when cues.txt content is embedded. Cues are written to a temp file for parsing.
- Look up assets via `rev::pack::GetPackedAsset(key, kPackedAssets, kPackedAssetCount)`.
- `packed_assets.h` is included only when `HIMYM_PACKED_ASSETS` is defined.
- The `minimal_intro_packed` CMake target has a PRE_BUILD rule that touches `main.cpp` to force recompile after every editor pack cycle.

## XM music playback (WinMM audio thread)
- `rev::xm::CreatePlayer(data, size)` loads the module but produces no sound alone.
- Audio requires a dedicated `waveOut` thread:
  1. `waveOutOpen(WAVE_MAPPER, PCM 16-bit 48kHz stereo)`
  2. Pre-fill 4 WAVEHDR buffers × 2048 frames before entering the loop.
  3. Poll `WHDR_DONE` each iteration: call `rev::xm::Update(player, float_buf, frames)`, clamp to [-1,1], convert to int16, `waveOutWrite`.
  4. On stop: set `stop=true`, `WaitForSingleObject`, `waveOutReset`, `waveOutUnprepareHeader`, `waveOutClose`.
- `#pragma comment(lib, "winmm.lib")` + `#include <mmsystem.h>` required.

## Image loading notes (GDI+)
- Error 3 from GDI+ = FileNotFound OR GDI+ not initialized — check both.
- Always check `bitmap->GetLastStatus() == Gdiplus::Ok` after `new Gdiplus::Bitmap(wpath)`.
- **IStream lifetime**: GDI+ decodes PNG lazily at `LockBits`, not at `Bitmap` ctor. Do NOT `stream->Release()` until after `UnlockBits` + `delete bitmap`.

## Read-before-edit targets
- A project's `cues.txt` — verify field layout before changing any sscanf patterns
- `revision_libs/rev_runtime/include/rev_runtime.h` — struct definitions and function signatures (source of truth)
- `revision_libs/rev_runtime/src/rev_runtime.cpp` — parser implementations and Mat4 functions
- `revision_libs/rev_editor/src/editor_context.cpp` — `ExportProject()` for export format source of truth
- `revision_libs/rev_mesh/include/rev_mesh.h` — `Vertex`, `Mesh` structs and procedural geometry API

## Pair with
- `Revision Codebase Map` for orientation.
- `Scene Block Editor` when runtime semantics must stay aligned with export.
- `Revision Build Validation` for build checks.
