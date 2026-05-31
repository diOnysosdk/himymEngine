---
name: Revision Runtime Core
description: "Use for C++ runtime work in examples/minimal_intro/ that changes frame flow, cue loading, image/music rendering, shader dispatch, asset path resolution, or packed build behavior."
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
- Keep `LoadImageCue` parser field count aligned with the export format: currently 14 fields — `asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end`.
- Keep `LoadTextCue` parser field count aligned with the export format: currently 16 fields.
- Keep `LoadMeshCue` parser field count aligned with the export format: currently 25 fields — `asset_key|mesh_type|pos_x|pos_y|pos_z|rot_x|rot_y|rot_z|scale_x|scale_y|scale_z|color_r|color_g|color_b|color_a|mesh_size|mesh_param|effect_type|cue_start|cue_end|fade_in_start|fade_in_end|fade_out_start|fade_out_end|layer_order`.
- Do NOT redefine `ImageCue`, `TextCue`, `MusicCue`, `MeshCue`, `ImageTexture`, `TextTexture` in `main.cpp` — they come from `rev_runtime.h` via `using` declarations.
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

## DPI awareness + letterbox viewport
- `rev_platform` calls `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` inside `CreateIntroWindow` (covers both editor and runtime — do NOT add a second call elsewhere).
- For fullscreen, `platform_win32.cpp` uses `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)` as the physical window size and stores it in `window->win_width` / `window->win_height`.
- The render loop clears the full physical window first, then computes a centered letterbox viewport that fits the configured render resolution (1920×1080) into the actual window:
```cpp
int ww = window->win_width  > 0 ? window->win_width  : config.width;
int wh = window->win_height > 0 ? window->win_height : config.height;
glViewport(0, 0, ww, wh);
glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
// Letterbox: fit config resolution centered in physical window
{
    float ta = (float)config.width / (float)config.height;
    float wa = (float)ww / (float)wh;
    int vx=0,vy=0,vw=ww,vh=wh;
    if (wa > ta) { vw=(int)(vh*ta+0.5f); vx=(ww-vw)/2; }
    else         { vh=(int)(vw/ta+0.5f); vy=(wh-vh)/2; }
    glViewport(vx,vy,vw,vh);
}
```
- All NDC calculations still use `config.width` / `config.height` (unchanged).

## Unified sorted draw pass (minimal_intro/main.cpp)
- Image, text, and mesh cues are **not drawn in fixed order**. They are collected into a `DrawEntry[3]` array, bubble-sorted by `layer_order` ascending (lower = drawn first = further back), then rendered in one loop.
- Blend/depth state is switched lazily: sprites enable blend + disable depth; mesh enables depth + clears depth buffer once, disables blend.
- The pattern (pseudocode):
```cpp
DrawEntry entries[3]; int ne = 0;
if (img_active) entries[ne++] = {0, image_cue.layer_order};
if (txt_active) entries[ne++] = {1, text_cue.layer_order};
if (msh_active) entries[ne++] = {2, mesh_cue.layer_order};
// bubble sort by layer
for each sorted entry: switch on type, set GL state, draw
```
- Depth buffer is cleared with `glClear(0x00000100)` (GL_DEPTH_BUFFER_BIT) once on the **first** mesh item, not unconditionally.

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
- Packed path: look up music asset key in `kPackedAssets`; file path: read the `.xm` file from `music_cue.asset_path` (workspace-relative).

## Image loading notes (GDI+)
- Error 3 from GDI+ = FileNotFound OR GDI+ not initialized — check both.
- GDI+ error 5 = access denied / wrong format.
- Always check `bitmap->GetLastStatus() == Gdiplus::Ok` after `new Gdiplus::Bitmap(wpath)`.
- **IStream lifetime**: GDI+ decodes PNG lazily at `LockBits`, not at `Bitmap` ctor. Do NOT `stream->Release()` until after `UnlockBits` + `delete bitmap`.

## Read-before-edit targets
- A project's `cues.txt` — verify field layout before changing any sscanf patterns
- `revision_libs/rev_runtime/include/rev_runtime.h` — struct definitions and function signatures (source of truth)
- `revision_libs/rev_runtime/src/rev_runtime.cpp` — parser implementations (`LoadImageCue`, `LoadTextCue`, `LoadMusicCue`, `LoadMeshCue`, Mat4 functions)
- `revision_libs/rev_editor/src/editor_context.cpp` — `ExportProject()` for export format source of truth
- `revision_libs/rev_pack/include/rev_pack.h` — `PackedAsset` struct
- `revision_libs/rev_mesh/include/rev_mesh.h` — `Vertex`, `Mesh` structs and procedural geometry API

## Pair with
- `Revision Codebase Map` for orientation.
- `Scene Block Editor` when runtime semantics must stay aligned with export.
- `Revision Build Validation` for build checks.