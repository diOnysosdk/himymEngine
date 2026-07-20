# Architecture Overview — revision2026

Single-production Windows intro/demo runtime for Revision 2026.
No runtime plugins. One authored flow. Optional compile-time 3D path.

---

## HiMYM Runtime Addendum (2026-06)

The active HiMYM runtime/editor pipeline also includes a glTF path used by mesh cues (`mesh_type=4`) with these behavioral contracts:

- Import fidelity:
  - Merge all mesh nodes from the active scene into one runtime mesh (not first-node-only behavior).
  - Preserve per-primitive material slot mapping (`material_index`) and per-slot base color texture assignment.
  - Preserve imported light position when available; fallback to `{3,5,4}` when absent.
- Material rendering:
  - Mixed material slots are valid: some slots textured, others color-only.
  - Slot color and slot alpha are applied per material slot.
- Transparency/fade contract:
  - Final mesh alpha composes cue fade alpha, slot/material alpha, and sampled texture alpha.
  - Mesh transparency is not inferred from texture presence alone.
  - Mixed opaque/transparent meshes render in two phases: opaque slots first (depth write on, blend off), then transparent slots (depth write off, blend on).
- Per-cue camera/render state:
  - `MeshCue.fov_deg` controls the projection used for that cue in both editor preview and `minimal_intro`; default remains 45 degrees when missing.
  - `MeshCue.cull_mode` is authored per cue (`off`, `back`, `front`) and must be applied in both preview and runtime so imported meshes preview the same way they export.
  - Imported glTF camera playback (`MeshCue.use_imported_camera`) uses the camera node world transform each frame:
    - position from animated node translation
    - forward from camera node `-Z`
    - up from camera node `+Y`
    This avoids fixed-world-up drift and keeps pitch/yaw/roll fidelity aligned with authored glTF animation.
  - Animated imported-node deltas are evaluated from one active animation track (`current_animation`, fallback index 0), not by sequentially applying every animation in the file.

This contract exists to keep runtime and editor preview visually aligned for imported multi-object, multi-material assets.

## Indexed Pixel Animation Contract

Pixel animation is split into two layers:

- `rev::pixel::PixelAnimation` is a CPU-owned indexed asset with a fixed 16-color RGBA palette and raw frame bytes.
- `rev::runtime::PixelCue` is the timeline reference. It owns timing, transform, playback, palette offset/cycling, curves, blend mode, and shader/effect references, but never owns pixel data.

The `.pix` file format uses the `HPIX` magic and version `1`, followed by dimensions, frame count, palette metadata, palette colors, and one palette index per pixel. Editor projects persist PixelCues in JSON and export them under `[pixel_cues]` in `cues.txt`.

Runtime decoding is CPU-side. Each active frame is expanded to RGBA and uploaded with `GL_NEAREST` filtering so authored pixel edges remain hard. Packed builds use the same decoder through `LoadAnimationFromMemory`; the packer collects the first two pipe-delimited fields from `[pixel_cues]` as asset key and path.

The editor PixelCue browser accepts existing `.pix` files and common GDI+ image formats (`.png`, `.jpg`, `.jpeg`, `.bmp`, `.gif`, `.tif`, `.tiff`). Source images are quantized to the fixed palette and saved as a same-name `.pix` asset in the project assets folder; animated sources retain their exposed frame count.

Particle effects build on this asset contract rather than copying image data into every instance. The `rev_particles` library owns a fixed particle pool and deterministic spawn/update simulation; an asset-backed `PixelEmitterCue` references one image texture and renders many particle instances through the existing sprite pass. The legacy `PixelCue` remains a single animated indexed sprite for backward compatibility.

The first emitter core supports point/cone direction sampling, burst and rate emission, start delay, lifetime/speed/scale/rotation/angular-speed/animation-speed ranges, acceleration, drag, and local/world simulation-space metadata. Its visual source is explicitly either a referenced `.pix` asset or a built-in primitive (`square`, `circle`, `triangle`, or `diamond`), so simple effects do not require an authored image. Rendering, lifetime curves, attachment transforms, and editor controls should be added at the cue layer rather than to `rev_particles`.

`PixelEmitterCue` authoring is persisted independently from legacy `PixelCue` data in the JSON `pixel_emitter_cues` array and the `[pixel_emitter_cues]` cues export section. This keeps older projects readable while preserving the source mode, primitive choice, emitter ranges, and deterministic seed for new projects.

---

## Directory Layout

```
src/
  app/           Entry point. Win32 WinMain, main loop, subsystem wiring.
  platform/      Win32 window, message pump, high-res timing.
  audio/         XM decode, WinMM waveOut, embedded track bytes.
  sequence/      Phase graph, timeline config, all scene/scroll knobs.
  content/       Authored data loaders (scene/image/music/shader curves) and embedded asset glue.
  renderer/      WGL init, OpenGL 3.3 context, fullscreen shader pass, optional 3D pass, image overlay pass.
third_party/     jar_xm (header-only XM decoder)
include/         Shared project-wide headers
cmake/           build helper cmake files
assets/          Runtime assets (XM track if not embedded, etc.)
  scene3d.txt    Optional 3D stage config + scene-specific multi-object overrides such as `scene_a.object_0.*` / `scene_b.object_1.*` (used only when `REV_ENABLE_3D=ON`)
  meshes/        Offline-converted `.meshbin` files from Blender OBJ exports
docs/            This documentation folder
tools/           Authoring/editor utilities, including `obj_to_meshbin.py`
.github/agents/  VS Code Copilot agent mode definitions
```

---

## Frame Loop Contract

Defined in `src/app/main_win32.cc`:

```
WaitForWindowReady          ← polls until non-minimized, non-zero client area (5 consecutive frames)
InitializeRenderer          ← creates WGL context + compiles shaders (before music so GL exists)
InitializeSequence          ← zeroes phase graph state

loop until PlatformState.should_exit:
  PumpMessages              ← Win32 message pump (ESC → RequestExit)
  GetTimeSeconds            ← steady_clock wall-clock (used as shader animation time)
  StartupWarmupGate         ← optional black warmup window before timeline/music start
  InitializeMusic (cue-driven, once) ← opens waveOut + XM decode only when an authored music cue becomes active
  UpdateMusicPlayback       ← refills consumed WinMM WAVEHDR buffers after cue-driven audio init
  UpdateSequence            ← pure timeline phase graph, no audio reactive override
  SetCursorVisibility       ← per-scene cursor hide/show from IsCursorVisibleForScene()
  ResolveImageCue           ← evaluate active/effect windows in scene-cycle time and map cue to overlay slot/opacity/transform
  ApplyShaderCurves         ← common + per-shader modulation, including optional 3D stage controls (FOV, camera distance, yaw/pitch/roll, offsets, scale, material, tint)
  RenderFrame               ← GL fullscreen quad shader + optional 3D preview/mesh pass + image overlay pass + startup atlas text
  frame_index++
```

---

## Subsystem Responsibilities

### `platform/`
- Creates a borderless fullscreen window (`WS_POPUP | WS_VISIBLE`) on the target monitor.
- Pumps `WM_KEYDOWN` (ESC), `WM_CLOSE`, `WM_QUIT` → sets `should_exit`.
- `GetTimeSeconds()` is the sole timing source for the main loop; uses `std::chrono::steady_clock`.
- **Cursor control**: `SetCursorVisibility(state, bool)` now keeps cursor hiding window-local.
  The runtime uses an invisible client-area cursor only while the intro window is active/foreground,
  and switches back to the normal arrow on focus loss or shutdown without mutating the desktop-wide OS cursor.
  `WndProc` uses `WM_SETCURSOR`, `WM_SETFOCUS`, `WM_KILLFOCUS`, `WM_ACTIVATE`, and mouse events to
  keep the pointer hidden over the app while avoiding post-exit or alt-tab cursor glitches.

### `audio/`
- XM is decoded statically via `jar_xm` (header-only, no DLL).
- WinMM `waveOut` with 4-buffer WAVEHDR ring. `UpdateAudioDeviceMME` recycles completed buffers.
- Music time is accumulated from submitted samples: `t += 2048 / samplerate` per refill.
  This is perfectly deterministic and matches decoder position.
- Music startup is authored-cue driven: the runtime stays silent until a valid `music` cue becomes active,
  then resolves that cue's `audio_key` to the embedded XM asset. No cue means no music in release builds.

### `sequence/`
- **Phase graph**: `kStartupFadeIn → kStartupHold → kStartupTransition → kMain`
- **Main loop**: Scenes A → B → C cycle deterministically by absolute visual time.
- **Scene state**: `SequenceState` provides current/next scene, scene-local time, blend, and exposure/fade.
- **Knobs**: All timing and visual config lives in `scene_management.cc` as `constexpr` structs
  populated from `#define` macros. Editing macros is the sole supported tuning method.

Runtime timing model:
- `warmup_seconds` (timeline knob) keeps visuals black and scene time frozen before startup begins.
- Music is initialized once when warmup elapses, and never reinitialized on scene-cycle wrap.
- `global_time_seconds` is the authoritative continuous runtime clock.
- `main_scene_time_seconds` remains the current-scene local time for scene logic and transitions.
- Cue runtime evaluation in `main_win32.cc` derives a scene-cycle timing view from the global clock.
- Image cue `Active Start/End` and `Effect Start/End` are evaluated in cue-private absolute time derived from the source scene start, so carried cues can continue across a scene boundary without resetting effect phase.

### `content/`
- Owns authored-data loaders consumed by runtime startup and per-frame updates.
- `scene_data_loader`: timeline row + authored scene visuals/looks.
- `image_cue_loader`: scene-authored image overlays with active/effect timing and optional one-hop carry-to-next-scene.
- `animated_sprite` cue flow: scene-authored frame-sequence overlays exported as `[animated_sprite_cues]` with `sprite_name|frame_keys_csv|frame_paths_csv|...` timing/playback fields.
  Runtime resolves frame images per active cue using playback mode (`loop`/`once`/`pingpong`), `fps`, and optional curve-driven frame index.
  `rev_pack` includes referenced frame assets from both explicit relative paths and project-assets fallback resolution.
- `text_cue_loader`: scene-authored text objects with active/effect timing and text effect parameters.
- `music_cue_loader`: music cue metadata for runtime.
- `shader_pipeline_loader`: authored pass-graph metadata exported by the Python editor and validated at load time before the runtime accepts the shader pipeline bundle. The editor now validates shader pipeline dependency chains before export. The loader topologically orders passes and rejects missing, disabled, or cyclic dependencies. The runtime builds a typed active-pass plan per scene/time, uses authored `material` passes to preblend scene visuals, uses the overlay bucket plus scene-pass resolution ahead of legacy `shader_cues`, and executes authored `postfx` / `output` passes as final fullscreen composition layers.
- `font_config_loader`: named GDI font-atlas library exported from editor (`assets/font_config.txt`).
- `text_config_loader`: startup title/credits, scroller text, per-text font selection, and scroll motion/shading knobs exported from editor (`assets/text_config.txt`). The canonical editor defaults now provide a neutral steady-scroll baseline (`clean_classic`-style: zero wave/twist/depth carryover) so kind/preset switches and export do not leak stale motion values.
- `shader_curve_loader`: common and per-shader curve tracks for shader params plus optional `scene3d_*` stage controls. Shared/common 3D curves now use inspector-space units directly instead of normalized offsets.

### `renderer/`
- `InitializeRenderer`: creates WGL legacy context, upgrades to 3.3 compatibility via
  `wglCreateContextAttribsARB`. Compiles GLSL shaders, builds the authored GDI font-atlas library,
  and precaches glyph layouts for startup title/credits and scroll text using their selected font keys.
- When compiled with `REV_ENABLE_3D=ON`, the renderer also reads `assets/scene3d.txt` and can draw a
  lightweight compatibility-profile 3D stage before authored overlays. Blender content should be exported
  offline to `.obj`, converted with `tools/obj_to_meshbin.py`, and then embedded into the release build via
  the generated embedded project-asset tables; there is no heavy runtime importer.
- Keep heavyweight renderer ownership explicit: the multi-object 3D path increases `RendererState` size enough
  that `main_win32.cc` should keep it off the stack with an explicit runtime owner (the current verified path is heap allocation).
  Avoid reverting this to a large static instance: that masks the stack issue but can bloat the final PE/exe by several megabytes in Release builds.
- The editor now treats the stage mesh as a dedicated `3D` asset row per scene/effect block and exports its
  authored timing/visibility fields into `scene3d.txt` using `scene_<id>.object_<n>.<field>` keys
  (`scene_a.object_0.start_s`, `scene_a.object_1.mesh_path`, `scene_b.object_0.rotation_speed_deg`, etc.).
  Each authored 3D row can target its own mesh/texture and multiple rows sharing the same Scene ID now render
  together as distinct 3D objects in that scene (up to 8 objects per scene, keeping the runtime explicit and static).
  Global 3D timing still runs continuously across scene changes by default; only explicit per-scene timing fields
  restart the local 3D fade/start window for that scene. When any scene uses dedicated 3D rows, scenes without
  those rows stay off instead of unexpectedly inheriting the global mesh stage, while camera/tint/scale/material
  shading still change deterministically with the authored cycle. The same authored row can also render up to 8
  repeated copies of one mesh using `instance_count` together with `instance_step_x/y/z`, `instance_yaw_step_deg`,
  and `instance_scale_step`, giving lightweight multi-object layouts without adding a heavier scene graph or runtime
  instance system.
- Material shaping is lightweight and authored directly in `scene3d.txt` via `material_ambient`,
  `material_diffuse`, `material_specular`, `material_shininess`, and `material_rim` (globally or per-scene),
  giving the mesh stage more visual polish without a heavy runtime material system.
- Directional stage lighting is also authored in `scene3d.txt` via `light_yaw_deg`, `light_pitch_deg`,
  `light_intensity`, and `light_r/g/b`. Blender light objects are not imported in the `.obj -> .meshbin`
  path; use the editor/runtime light controls instead to keep the intro path deterministic and lightweight.
- Base 3D orientation is now authored with `rotation_yaw_deg`, `rotation_pitch_deg`, and `rotation_roll_deg`,
  while the shared curve editor can animate `scene3d_fov_degrees`, `scene3d_camera_distance`, `scene3d_yaw_deg`,
  `scene3d_pitch_deg`, `scene3d_roll_deg`, `scene3d_light_yaw_deg`, `scene3d_light_pitch_deg`,
  `scene3d_light_intensity`, and `scene3d_light_r/g/b` over the same scene/cycle time base used by the optional 3D stage.
  These 3D curve values use inspector-space units directly rather than normalized `0..1` offsets, and the editor keeps
  stable authoring ranges for unit-space tracks (for example yaw/pitch/roll around ±360° from the current base value)
  so large rotations are practical to draw. After editing curves, re-export / Do It All so the embedded runtime assets refresh.
- Optional diffuse texture use is authored via `texture_key` / `texture_path`; the editor can auto-detect a Blender
  `map_Kd` texture plus common normal-map sidecars (`bump`, `map_Bump`, `norm`, `map_kn`) from the OBJ's `.mtl`
  during import, and the runtime binds the embedded diffuse image plus imported normal-map data when present.
- `.meshbin` version 4 now preserves up to 8 Blender material slots with per-range diffuse texture paths, per-range normal-map
  paths (`bump` / `map_Bump` / `norm`), tangent-space basis data, and lightweight `Ka/Kd/Ks/Ke/Ns/d` shading values from the
  OBJ/MTL. If no stage-wide texture override is authored, the runtime binds those material textures per index range and uses the
  imported base color / specular / emissive / opacity data plus the sampled normal map to perturb the existing lightweight stage
  lighting, without introducing a heavy runtime material system.
- `RenderFrame`:
  1. Caches viewport rectangle computation (only recalculates on window resize).
  2. Builds draw order for shader/image/text overlays with early rejection of zero-opacity items.
  3. Sorts active overlays using shared `InsertionSortByLayer()` helper (code deduplication).
  4. Sets shader uniforms from scene visual config, blend amount, and dyn channels.
  5. Draws fullscreen quad — fragment shader runs scene dispatch + cross-blend + grading.
  6. Draws unified overlay draw order (shader/image/text/3D interleaved by layer):
     - Shader overlays: fullscreen pass with overlay visual config + dyn + blend mode.
     - Image overlays: batched GL state (begin batch on first image, draw with cached blend mode, end batch on non-image).
     - Text overlays: glyph layout rendering with effect evaluation. For non-scroll text (`title_main`, `credits_main`, `multiline_text`), normalized `x`/`y` are anchor coordinates (`0.5`, `0.5` centers on screen).
     - 3D objects: optional mesh stage rendering when `REV_ENABLE_3D=ON`.
  7. Draws final shader passes (post-compositing effects).
  8. Draws startup text from the atlas while `SequenceState.text_reveal > 0`.
  9. `SwapBuffers` with VSync (wglSwapIntervalEXT(1) by default).
  10. If VSync is unavailable/disabled by driver, main loop applies deterministic 60 Hz software pacing fallback.

Runtime layered draw GL-state contract (mesh + image/text in same frame):
- Always rebind the fullscreen quad VAO before image/text draws. Mesh rendering may leave a different VAO (or VAO 0) bound, which can break core-profile `gl_VertexID` sprite passes.
- Before per-frame `glClear(GL_DEPTH_BUFFER_BIT)`, force `glDepthMask(GL_TRUE)` so depth clear is effective.
- During layered pass transitions, image/text overlays run with depth test disabled and depth writes disabled; mesh draws must restore both depth test and depth writes.
- End each layered pass with depth writes enabled to keep the next frame depth-clear behavior deterministic.

### Renderer Optimizations

RenderFrame applies several optimizations to reduce per-frame overhead and binary size:

- **Content rect caching**: Viewport pillarbox/letterbox computation is cached and only recalculated on window resize (tracked via `last_window_width/height` in `RendererState`).
- **Early rejection**: Zero-opacity overlays are filtered before sorting, reducing merge workload.
- **Sorting deduplication**: Three identical insertion-sort blocks (shader/image/text) share one `InsertionSortByLayer()` helper (~1KB code size reduction).
- **Stack-to-state arrays**: Draw order arrays (`shader_draw_order[128]`, `image_draw_order[128]`, `text_draw_order[128]`) moved from RenderFrame stack to RendererState (~1.5KB PE reduction).
- **Image batching**: `BeginImageOverlayBatch()` / `DrawImageOverlayBatched()` / `EndImageOverlayBatch()` amortize GL state setup across consecutive images. Smart batching begins on first image and ends when a non-image item is encountered (+4KB binary size for per-frame speed improvement).

Net effect: +4KB binary size (deduplication savings outweighed by batching code), reduced per-frame overhead. Acceptable for both demo and intro profiles (size/speed tradeoff approved).

---

## Asset Embedding Pipeline

Binary assets (images, meshbin) are embedded into the executable at CMake configure time as C `unsigned char[]` arrays inside generated headers (`embedded_image_assets.h`, `embedded_mesh_assets.h`).

**Hex encoding**: done via `execute_process(COMMAND python -c ...)` in `CMakeLists.txt`. Python encodes the full binary in milliseconds. The previous CMake `foreach` loop was O(filesize) in CMake script and caused multi-minute hangs for large files — do not revert to it.

**Incremental configure**: `tools/scene_block_editor.py` maintains `build/embed_fingerprint.json` — a SHA-1 fingerprint of all CMake embed inputs plus build flags. If the fingerprint matches on Do It All / Build, the cmake configure step is skipped entirely. The fingerprint is written after every successful configure and invalidated by any asset, source, flag, or project JSON change.

**Inputs fingerprinted**: `scene_data.txt`, `cues.txt`, `font_config.txt`, `text_config.txt`, `scene3d.txt`, all shader GLSL sources (`shader_common.glsl`, `shader_footer.glsl`, `scenes/*.glsl`, `vertex.glsl`), `CMakeLists.txt`, the active project JSON, and all binary asset files listed in the project `"assets"` block.

---

## Startup Gate

`WaitForWindowReady(&platform_state, 1500_ms_timeout)` is called before any renderer or audio init.
It polls every ~16ms until the Win32 window reports: visible, non-minimized, non-zero client area,
for 5 consecutive frames. This prevents GL init on a not-yet-ready surface on slower compo machines.

---

## Sequence / Scene Lifecycle

```
0s                              startup_fade_in_seconds
|——— kStartupFadeIn ————————————|
                                |——— kStartupHold ————|
                                                      |——— kStartupTransition ————|
                                                                                  |——— kMain ——→ cycles
```

During `kMain`, the scene cycle wraps every sum of authored active scene durations.

Cross-blend begins `main_scene_transition_seconds` before the end of each scene.

Startup phase exposure/fade targets are derived from the first active scene look config,
and startup/main transition timing is read from the authored `timeline | ...` row in
`assets/scene_data.txt`.

Cue timing rules:
- `Active Start/End` controls when an image cue exists.
- `Effect Start/End` controls when effect motion/modulation is applied.
- Carry-to-next-scene extends active/effect windows by at most one authored scene hop.
- Cue timing is evaluated in a shared scene-cycle model so in-scene, cross-scene, and wrap behavior use one logic path.

---

## GL / Shader Pipeline

```
Vertex shader:   pass-through, emits UV from gl_Vertex.xy
Fragment shader: #version 330 compatibility

Uniforms per frame:
  resolution, time, exposure, fade
  scene_a_id, scene_b_id, scene_blend
  a_palette_low, a_palette_mid, a_palette_high, a_speed, a_intensity, a_warp
  a_dyn0, a_dyn1, a_dyn2, a_dyn3
  b_palette_low, b_palette_mid, b_palette_high, b_speed, b_intensity, b_warp
  b_dyn0, b_dyn1, b_dyn2, b_dyn3
  a_st, b_st

Scene dispatch (RenderScene):
  integer scene IDs are authored per scene row (`shader_scene_id`) and dispatched in
  generated `assets/shader/fragment.glsl`.
  Authoring source-of-truth is split across:
  `assets/shader/shader_common.glsl`, `assets/shader/scenes/*.glsl`, and
  `assets/shader/shader_footer.glsl`.
  IDs are not limited to 0..2.

Dyn-time policy:
- `time` remains the global deterministic visual time base.
- `a_st` / `b_st` are CPU-integrated per-scene shader times.
- Runtime integrates `scene_shader_time += dt * speed * dyn0_speed_factor` and passes
  those values as uniforms so dyn0 speed curves remain smooth without phase pops.

Post-blend grading (all scenes):
  horizon_fog, bottom_fog
  pow(col, 1.12)   ← black crush + contrast
  vignette
  final *= exposure

Optional image overlay pass:
  image texture slot lookup
  centered transform from normalized x/y/scale
  alpha blend by cue opacity after active/effect/fade evaluation in scene-cycle time

**Special case**: Image overlay rendering is skipped when shader scene ID 34 (`flag_wave`) is active, because that shader dynamically samples the image texture and applying the static overlay would cause double-rendering.
```

Runtime strictness:
- Authored scene data must load.
- Timeline row must be present.
- Missing/invalid authored scene data aborts startup instead of falling back silently.
- The authored shader pipeline bundle is validated on load; invalid pass graphs should fail fast instead of partially executing. If the pipeline bundle is missing, the runtime still accepts the legacy shader-cue path.

---

## Build Profiles

| Profile | `REV_IS_INTRO` | `REV_RENDER_GL` | `REV_ENABLE_3D` | Size limit |
|---|---|---|---|---|
| `demo` | 0 | ON | optional | none |
| `intro64k` | 1 | ON | optional | 65 536 B |
| `intro128k` | 1 | ON | optional | 131 072 B |
| `intro256k` | 1 | ON | optional | 262 144 B |

Diagnostics (`#if REV_ENABLE_DIAGNOSTICS`) still gate local-only logging and filesystem fallbacks,
but normal workspace builds keep them OFF so release compiles launch quietly without debug messaging.

Build commands:
```powershell
cmake -S . -B build
cmake --build build --config Release
```
Preset shortcuts: `cmake --preset intro64k && cmake --build --preset intro64k`

---

## Standalone Editor Completion Gates

The standalone editor (`himym_editor`) is considered complete only when it satisfies all gates below without introducing new architecture layers.

Required workflow gates:

- Create/import/edit/save project data from the docked shell.
- Export runtime files consumed by current loaders.
- Build and launch through explicit editor actions.
- Preserve deterministic action order in `Do It All`: save -> export -> build -> launch.

No-bloat gates:

- No plugin system, no runtime module discovery, no background orchestration service.
- No generalized editor framework abstractions when direct file-local logic is sufficient.
- No new external dependencies for completion work.

Verification gates:

- `cmake --build build --config Release --target himym_editor`
- `.\build\Release\himym_editor.exe --headless --import-legacy assets/mono_scene_blocks.json --out build\_smoke_import.himym.json`
- `.\build\Release\himym_editor.exe --headless --project build\_smoke_import.himym.json --export-runtime`

These gates are intended to keep completion focused on correctness, reliability, and deterministic outputs rather than feature growth.
