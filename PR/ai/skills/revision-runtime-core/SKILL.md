---
name: Revision Runtime Core
description: "Use for C++ runtime work in examples/minimal_intro/ that changes frame flow, cue loading, image/music/mesh rendering, shader dispatch, asset path resolution, glTF import/render behavior, or packed build behavior."
---
# Revision Runtime Core

Use this skill for runtime changes in `examples/minimal_intro/main.cpp`.

## Scope
- `examples/minimal_intro/main.cpp` — standalone intro, loads cues from file or embedded data; does NOT define cue structs or loader functions (those live in rev_runtime)
- `revision_libs/rev_runtime/` — shared static lib: cue structs, `ComputeEffectOpacity`, `LoadImageTexture`, `LoadImageTextureFromMemory`, `RenderTextToTexture`, `LoadImageCue`, `LoadTextCue`, `LoadMusicCue`, `LoadMeshCue`, and 8 Mat4 math functions
- `revision_libs/rev_mesh/` — procedural mesh lib: `CreateCube/Sphere/Plane/Torus`, `UploadToGPU`, `Render`, `DestroyMesh`. Mesh now also carries per-slot material mapping + imported-light metadata for glTF assets.
- `revision_libs/rev_gltf/` — glTF/glb importer: `LoadMesh(path, texture_output_dir)` and `LoadMeshFromMemory(buf, size, texture_output_dir)`.
- `revision_libs/rev_platform/` — Win32 windowing, OpenGL context
- `revision_libs/rev_shader/` — shader compilation and dispatch
- `revision_libs/rev_xm/` — XM music player (wraps libxm-windows)


## Non-negotiables
- Keep the main loop deterministic: pump messages, get time, update cues, render.
- Keep GDI+ initialized (`GdiplusStartup`) before any `LoadImageTexture` call.
- Keep `LoadShaderCue` parser field count aligned with the export format: currently 42 fields (25 base + 17 curve indices).
- Keep `LoadImageCue` parser field count aligned with the export format: currently 18 fields (14 base + 4 curve indices).
- Keep `LoadTextCue` parser field count aligned with the export format: currently 22 fields (16 base + 6 curve indices).
- Keep `LoadMeshCue` parser field count aligned with the export format: currently 44 fields (28 base + 16 curve indices).
- Do NOT redefine `ImageCue`, `TextCue`, `MusicCue`, `MeshCue`, `ImageTexture`, `TextTexture` in `main.cpp` — they come from `rev_runtime.h` via `using` declarations.
- Do NOT redefine cue loaders or `Mat4*` functions in `main.cpp` — they are implemented in `rev_runtime.cpp`.
- `glUniformMatrix4fv` and other GL 2.0+ functions are NOT in Windows `<gl/gl.h>`; load via `wglGetProcAddress` before first use.
- For 3D mesh rendering: enable depth test (`glEnable(0x0B71)`) before mesh draw, disable after (`glDisable(0x0B71)`) to avoid breaking the 2D sprite pass.
- Imported glTF behavior must preserve authored fidelity:
  - Merge all scene mesh nodes (not first-mesh-only)
  - Keep per-slot material mapping (`MaterialSlot.material_index`, `MaterialSlot.base_color_texture`)
  - Keep imported light fallback (`Mesh.has_imported_light` / `imported_light_pos`; default `{3,5,4}`)
- Mesh alpha/transparency contract:
  - Mesh fragment shader alpha = cue alpha * slot alpha * sampled texture alpha (when textured)
  - Do not classify meshes as transparent solely because they have textures
  - For mixed meshes, draw opaque material slots first (depth write on, blend off), then transparent slots (depth write off, blend on)
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
- Image, text, and mesh cues are **not drawn in fixed order**. They are collected into one draw-entry list, sorted by `layer_order` ascending (lower = drawn first = further back), then rendered in one loop.
- Blend/depth state is switched lazily: sprites enable blend + disable depth; mesh enables depth + clears depth buffer once, disables blend.
- The pattern (pseudocode):
```cpp
DrawEntry entries[kMaxImageCues + kMaxTextCues + kMaxMeshCues]; int ne = 0;
if (img_active) entries[ne++] = {0, image_cue.layer_order};
if (txt_active) entries[ne++] = {1, text_cue.layer_order, text_idx};
if (msh_active) entries[ne++] = {2, mesh_cue.layer_order, mesh_idx};
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

## Mesh texture loading (glTF/glb)
- `rev::gltf::LoadMesh(path, texture_output_dir)` returns `ImportResult*` containing mesh geometry, material data (base color, metallic, roughness, texture paths), and animations.
- **Always pass `texture_output_dir`** to extract embedded textures from glTF/glb files. Without it, textures remain embedded and cannot be loaded.
- For non-packed builds: pass the mesh's directory as texture output dir (extract textures alongside mesh file).
- For packed builds: `rev::gltf::LoadMeshFromMemory(buf, size, texture_output_dir)` extracts embedded textures to the specified directory (typically `"."` for workspace root).
- After loading, check `ir->material.base_color_texture[0]` for texture path. If present, call `rev::runtime::LoadImageTexture(path, &tex)` and assign `mesh->base_color_texture = tex.texture_id`.
- **Animations**: Check `ir->animation_count > 0`. If present, transfer animations to mesh: `mesh->animation_data = ir->animations; mesh->animation_count = ir->animation_count; mesh->current_animation = 0; ir->animations = nullptr;` (transfers ownership).
- Mesh shaders must:
  1. Pass UVs from vertex shader: `out vec2 v_uv;`
  2. Sample texture in fragment shader: `uniform sampler2D u_base_color_texture; uniform int u_has_texture;`
  3. Multiply base color by texture: `if (u_has_texture != 0) base *= texture(u_base_color_texture, v_uv).rgb;`
- Before rendering, bind texture: `glBindTexture(0x0DE1, mesh->base_color_texture)` and set uniforms `u_base_color_texture=0, u_has_texture=1`.
- Blender export requirements: Image Texture node must connect to Principled BSDF Base Color, export with **Images** checkbox enabled. For animations, enable **Animations** checkbox in glTF export dialog.

## Mesh animation playback (glTF skeletal animations)
- **Timeline-driven**: Animations evaluate based on `(current_time - cue_start)`, NOT an independent animation timer.
- In the main loop, calculate `float dt = current_time - prev_time;` for frame delta time.
- **Before rendering each mesh cue**:
  1. Check if animation is present: `if (mesh->current_animation >= 0 && mesh->animation_data)`
  2. Calculate animation time relative to cue: `float anim_time = current_time - mesh_cue.cue_start;`
  3. Handle looping: `if (mesh->animation_loop) anim_time = fmodf(anim_time, duration);` else clamp to [0, duration]
  4. Evaluate animation: `rev::gltf::EvaluateAnimation(&anims[mesh->current_animation], anim_time, translation, rotation, scale);`
  5. Apply to mesh transform: `rev::gltf::ApplyAnimationTransform(anim_pos, anim_rot, anim_scale, translation, rotation, scale);`
  6. Build model matrix: `rev::runtime::Mat4Model(model, anim_pos, anim_rot, anim_scale);`
- **Key insight**: Do NOT call `UpdateMeshAnimation(mesh, dt)` in runtime — that advances an independent timer. Instead, evaluate animation at scene-relative time directly.
- Animation functions:
  - `EvaluateAnimation(anim, time, out_translation, out_rotation, out_scale)` - Interpolates keyframes at specific time
  - `ApplyAnimationTransform(pos, rot, scale, trans, rot_quat, anim_scale)` - Adds animation to cue transform
  - `QuaternionToEuler(quat, euler_degrees)` - Converts rotation quaternion to Euler angles
- Multiple animations: `mesh->current_animation` selects which animation to play (0-based index into `mesh->animation_data` array)

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