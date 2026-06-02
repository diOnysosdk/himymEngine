# API Reference — revision2026

All public APIs are in namespace `revision2026`. All source lives under `src/`.

---

## HiMYM Runtime/API Addendum (2026-06)

Current HiMYM runtime/editor behavior for imported meshes relies on these APIs and data contracts:

- `rev::gltf::ImportResult`:
    - Primary mesh (`mesh`) plus full material table (`materials`, `material_count`)
    - Optional imported light (`has_light`, `light_pos`)
- `rev::mesh::MaterialSlot`:
    - Material mapping via `material_index`
    - Per-slot texture binding via `base_color_texture`
- `rev::mesh::Mesh`:
    - Imported-light fallback fields (`has_imported_light`, `imported_light_pos`)

Render contract for imported meshes:

- Alpha composition: cue/fade alpha * slot/material alpha * sampled texture alpha (when textured)
- Opaque/transparent handling:
    - Do not mark meshes transparent solely due to texture presence
    - Render opaque slots first, then transparent slots

These additions keep runtime and preview behavior aligned for mixed textured + color-only glTF material slots.

---

## `app/profile_config.h`

### `enum class ProfileId`
| Value | Meaning |
|---|---|
| `kDemo` | Full demo profile (no size cap) |
| `kIntro64k` | 64 KiB size-capped intro |
| `kIntro128k` | 128 KiB size-capped intro |
| `kIntro256k` | 256 KiB size-capped intro |

### `struct BuildConfig`
| Field | Type | Description |
|---|---|---|
| `profile` | `ProfileId` | Active build profile |
| `render_gl` | `bool` | OpenGL renderer compiled in |
| `intro_size_limit` | `bool` | Non-zero for size-capped intro profiles |
| `enable_3d` | `bool` | Optional compile-time 3D path enabled |

### Functions
```cpp
ProfileId   GetProfileId();    // returns current profile from compile definitions
BuildConfig GetBuildConfig();  // constructs BuildConfig from compile-time flags
```

---

## `platform/platform_win32.h`

### `struct PlatformState`
| Field | Type | Description |
|---|---|---|
| `should_exit` | `bool` | Set to true when ESC/Alt+F4 received |
| `frame_index` | `uint64_t` | Monotonically increasing frame counter |
| `window_handle` | `HWND` | Win32 window handle (as `void*`) |
| `instance_handle` | `HINSTANCE` | Win32 instance handle (as `void*`) |
| `window_x/y/width/height` | `int` | Final window position and size |

### Functions
```cpp
void   InitializePlatform(PlatformState* state);
void   PumpMessages(PlatformState* state);             // processes Win32 message queue one frame
void   RequestExit(PlatformState* state);              // sets should_exit = true
void   SetCursorVisibility(PlatformState* state, bool visible);  // show/hide OS cursor
void   ShutdownPlatform(PlatformState* state);         // restores system cursors + destroys window
double GetTimeSeconds();                               // steady_clock wall-clock time in seconds
void   SleepMilliseconds(uint32_t milliseconds);       // Win32 Sleep wrapper for pacing fallback
```

#### Cursor control notes
- `SetCursorVisibility()` records the requested authored state, but the runtime now applies hiding only inside its own window.
- Hide path: use an invisible client-area cursor in `WM_SETCURSOR` / mouse events while the intro window is active and foreground.
- Show path: restore the normal arrow cursor on focus loss, deactivation, or shutdown.
- The desktop-wide OS cursor is no longer replaced, which makes smoke-test kills and rapid relaunches safer.

---

## `audio/module_music.h`

### `struct MusicState`
| Field | Type | Description |
|---|---|---|
| `xm_context` | `void*` | Opaque `jar_xm_context_t*` pointer |
| `wave_out_device` | `void*` | Opaque `HWAVEOUT` device handle |
| `playback_time_seconds` | `double` | Accumulated playback time (samples-based) |
| `sample_rate` | `int` | Active runtime sample rate |
| `initialized` | `bool` | Whether audio subsystem was successfully set up |

### `struct MusicDebugStats`
| Field | Type | Description |
|---|---|---|
| `done_buffer_count` | `int` | Number of completed WinMM buffers seen in the latest update |
| `refill_budget` | `int` | Buffers available for refill in the latest update |
| `refill_count` | `int` | Buffers actually refilled in the latest update |

### Functions
```cpp
bool   InitializeMusic(MusicState* state, const char* requested_asset_key);
void   UpdateMusicPlayback(MusicState* state);   // call every frame after cue-driven init
double GetMusicTimeSeconds(const MusicState* state);
void   GetMusicDebugStats(MusicDebugStats* out_stats);
void   ShutdownMusic(MusicState* state);
```

Notes:
- Music init is cue-driven: pass the active authored `asset_key` from `MusicCueLoader`.
- If no authored music cue is active, the runtime stays silent.

---

## `audio/audio_device_winmm.h`

### `struct AudioDeviceMME`
4-buffer circular WAVEHDR ring. Do not access fields directly; use the functions below.

### Functions
```cpp
bool InitializeAudioDeviceMME(AudioDeviceMME* device, uint32_t sample_rate, uint32_t channels);
bool SubmitAudioSamplesMME(AudioDeviceMME* device, const int16_t* samples, uint32_t num_samples);
void UpdateAudioDeviceMME(AudioDeviceMME* device);           // recycles completed buffers
int  GetNextFreeBufferMME(AudioDeviceMME* device);           // returns -1 when all busy
void PrefillAudioBuffersMME(AudioDeviceMME* device, const int16_t* samples, uint32_t samples_per_buffer);
void ShutdownAudioDeviceMME(AudioDeviceMME* device);
```

---

## `audio/xm_embedded.h`

```cpp
bool LoadXMFromAssets(const char* asset_key, const unsigned char** out_data, unsigned int* out_size);
```
Resolves a specific authored XM asset by `asset_key` from the embedded project assets bundle.
Diagnostics builds may still use filesystem fallback for local iteration.

---

## `sequence/sequence.h`

### `enum class SequencePhase`
| Value | Meaning |
|---|---|
| `kStartupFadeIn` | Fade from black on program open |
| `kStartupHold` | Static title card hold |
| `kStartupTransition` | Cross-fade into main scene |
| `kMain` | Cycling A → B → C main scenes |

### `struct SequenceState`
| Field | Type | Description |
|---|---|---|
| `phase` | `SequencePhase` | Current phase |
| `current_scene` | `MainSceneId` | Active main scene |
| `next_scene` | `MainSceneId` | Scene being blended toward |
| `phase_time_seconds` | `double` | Time elapsed inside current phase |
| `global_time_seconds` | `double` | Absolute runtime visual time |
| `main_scene_time_seconds` | `double` | Time elapsed in current main scene (scene-local) |
| `main_scene_blend` | `double` | 0→1 cross-fade progress to next scene |
| `exposure` | `float` | Runtime exposure value |
| `fade` | `float` | Runtime fade value |
| `text_reveal` | `float` | Startup text reveal value |
| `image_opacity/x/y/scale` | `float` | Scene-driven image overlay controls |
| `image_texture_slot` | `int` | Renderer texture slot index (`-1` = none) |

Notes:
- `UpdateSequence()` keeps `main_scene_time_seconds` scene-local for scene logic and transition blending.
- Image carry/effect continuity is resolved later in `main_win32.cc` using a scene-cycle timing model derived from `global_time_seconds`, not by extending `main_scene_time_seconds` itself.

### Functions
```cpp
void InitializeSequence(SequenceState* state);
void UpdateSequence(double time_seconds, SequenceState* state);
```

---

## `sequence/scene_management.h`

Config structs and accessors. All values are `constexpr`, authored in `scene_management.cc`.

### Structs

**`TimelineConfig`** — phase durations in seconds.

Fields include `warmup_seconds`, which inserts a startup black/warmup window before music init and scene-time progression.

**`SceneVisualConfig`** — per-scene shader palette (`palette_low`, `palette_high`), `speed`, `intensity`, `warp`.

**`SceneLookConfig`** — per-scene exposure/fade base and ramp values for `SequenceState` mapping.

**`RhythmConfig`** — BPM, beats/bar, phrase_bars, sharpness tempo reference values.

### Accessors
```cpp
const TimelineConfig&      GetTimelineConfig();
const SceneVisualConfig&   GetSceneVisualConfig(MainSceneId id);
const SceneLookConfig&     GetSceneLookConfig(MainSceneId id);   // unified look accessor
const SceneLookConfig&     GetMainSceneALookConfig();
const SceneLookConfig&     GetMainSceneBLookConfig();
const SceneLookConfig&     GetMainSceneCLookConfig();
const RhythmConfig&        GetRhythmConfig();
bool                       IsCursorVisibleDefault();              // reads authored timeline cursor_default
bool                       IsCursorVisibleForScene(MainSceneId id); // resolves scene override → timeline default
```

---

## `content/scene_data_loader.h`

Loads authored scene/timeline data from embedded text (and file override in diagnostics builds).

### `struct SceneDataEntry`
| Field | Type | Description |
|---|---|---|
| `scene_id` | `char` | `'A'`, `'B'`, or `'C'` |
| `duration_seconds` | `float` | Scene duration |
| `shader_scene_id` | `int` | Shader branch ID dispatched for this scene |
| `cursor_visibility_mode` | `int` | `-1` = inherit timeline default, `0` = hide, `1` = show |
| `palette_low/high_r/g/b` | `float` | Palette colour channels |
| `speed`, `intensity`, `warp` | `float` | Shader motion parameters |
| `exposure_base/ramp`, `fade_base/ramp` | `float` | Look config values |

### `struct SceneTimelineData`
| Field | Type | Description |
|---|---|---|
| `startup_fade_in_seconds` | `float` | Authored startup fade duration |
| `startup_hold_seconds` | `float` | Authored title hold duration |
| `startup_transition_seconds` | `float` | Authored startup→main transition |
| `main_scene_transition_seconds` | `float` | Authored cross-blend window |
| `cursor_visible_default` | `bool` | Timeline-level cursor default (hide/show) |

### `class SceneDataLoader`
```cpp
bool LoadFromText(const char* text);
bool LoadFromFile(const char* path);
bool IsLoaded() const;
bool HasTimelineData() const;
int GetLoadedSceneCount() const;
const SceneDataEntry* GetLoadedData() const;
const SceneTimelineData& GetTimelineData() const;
```

Global instance: `extern SceneDataLoader g_scene_data_loader;`

---

## `content/scene3d_loader.h`

Loads optional 3D stage config from `scene3d.txt` (embedded in release, filesystem override only in diagnostics builds).

### `struct Scene3DConfig`
| Field | Type | Description |
|---|---|---|
| `enabled` | `bool` | Whether the optional 3D stage should render |
| `draw_preview_cube` | `bool` | Draw the preview cube when no mesh is available |
| `opacity` | `float` | Base stage opacity |
| `start_seconds`, `end_seconds` | `float` | Active time window for the stage (`end < 0` = implicit end) |
| `fade_in_seconds`, `fade_out_seconds` | `float` | Stage fade durations |
| `fov_degrees`, `camera_distance` | `float` | Camera projection and distance controls |
| `camera_target_enabled`, `camera_target_x/y/z` | `bool`, `float` | Optional look-at target that keeps the view locked while the offset/distance controls act as the camera travel position |
| `rotation_speed_deg` | `float` | Continuous authored yaw spin speed |
| `rotation_yaw_deg`, `rotation_pitch_deg`, `rotation_roll_deg` | `float` | Direct base orientation controls for the 3D stage |
| `model_scale` | `float` | Model scale multiplier |
| `instance_count` | `int` | Number of repeated mesh copies to draw for the stage (clamped lightweight instancing path) |
| `instance_step_x/y/z` | `float` | Per-copy position step applied around the stage center |
| `instance_yaw_step_deg` | `float` | Extra yaw offset added per repeated copy |
| `instance_scale_step` | `float` | Per-copy scale offset used to taper or fan repeated copies |
| `offset_x/y/z` | `float` | Stage/model offsets |
| `tint_r/g/b` | `float` | Stage tint color |
| `material_ambient/diffuse/specular/shininess/rim` | `float` | Lightweight authored material shaping knobs |
| `light_yaw_deg`, `light_pitch_deg` | `float` | Direction of the lightweight authored stage light in degrees |
| `light_target_enabled`, `light_from_x/y/z`, `light_target_x/y/z` | `bool`, `float` | Optional helper fields that derive the light direction from a source point toward a target point |
| `light_intensity` | `float` | Main light strength multiplier |
| `light_r/g/b` | `float` | Main light color channels |
| `mesh_key`, `mesh_path` | `char[64]`, `char[128]` | Embedded/offline mesh source |
| `texture_key`, `texture_path` | `char[64]`, `char[128]` | Optional stage-wide diffuse texture override |
| `material_override_count`, `material_override_slots/texture_keys/texture_paths` | `int`, fixed char arrays | Optional per-slot diffuse overrides for connected OBJ/MTL material ranges |

### `class Scene3DLoader`
```cpp
bool LoadFromText(const char* text);
bool LoadFromFile(const char* filepath);
bool IsLoaded() const;
const Scene3DConfig& GetConfig() const;
int GetConfigs(Scene3DConfig* out_configs, int max_configs) const;
Scene3DConfig GetConfigForScene(char scene_id) const;
int GetConfigCountForScene(char scene_id) const;
int GetConfigsForScene(char scene_id, Scene3DConfig* out_configs, int max_configs) const;
bool HasSceneTimingOverride(char scene_id) const;
```

Runtime rule:
- Global `scene3d` timing continues across scene changes by default.
- `scene3d.txt` is backward-compatible with legacy single-object keys (`mesh_path`, `scene_a.opacity`, etc.), but scenes can now also author multiple distinct objects via `scene_<id>.object_<n>.<field>` keys.
- Connected imported meshes can keep their OBJ/MTL textures automatically, or override specific slots with `scene_<id>.object_<n>.material_<m>.slot/texture_key/texture_path` entries when one connected export still needs different object/material textures.
- `GetConfigsForScene(scene_id, ...)` returns the fixed-size authored object set for that scene (up to 8 objects), allowing multiple meshes to render together while keeping the runtime static and deterministic.
- Imported `.meshbin` material slots can carry lightweight OBJ/MTL material data (`Ka/Kd/Ks/Ke/Ns/d/Tr`), diffuse texture paths, and basic tangent-space normal-map paths from entries like `bump` / `map_Bump` / `norm`; the optional 3D renderer consumes those when present.
- `HasSceneTimingOverride(scene_id)` reports whether that scene explicitly overrides restart-driving timing fields (`enabled`, `opacity`, `start/end`, `fade_in/out`), in which case the local 3D time window is evaluated against scene-local time for that scene.
- The stage can still draw up to 8 lightweight repeated mesh copies from one authored row using `instance_count` plus the `instance_step_*`, `instance_yaw_step_deg`, and `instance_scale_step` controls.
- When `camera_target_enabled` is on, the existing `camera_distance` and `offset_x/y/z` values are interpreted as the camera eye for that shot, which makes the current curve tracks (`scene3d_camera_distance`, `scene3d_offset_x/y/z`) useful for simple travel moves without adding a heavier path system.
- Common curve tracks in `shader_curves.txt` named `scene3d_fov_degrees`, `scene3d_camera_distance`, `scene3d_offset_x`, `scene3d_offset_y`, `scene3d_offset_z`, `scene3d_model_scale`, `scene3d_material_ambient`, `scene3d_material_diffuse`, `scene3d_material_specular`, `scene3d_material_shininess`, `scene3d_material_rim`, `scene3d_tint_r`, `scene3d_tint_g`, `scene3d_tint_b`, `scene3d_yaw_deg`, `scene3d_pitch_deg`, `scene3d_roll_deg`, `scene3d_light_yaw_deg`, `scene3d_light_pitch_deg`, `scene3d_light_intensity`, and `scene3d_light_r/g/b` animate the 3D stage using the same units shown in the inspector.
- The curve editor intentionally keeps stable value ranges for those unit-space controls (for example yaw/pitch/roll around ±360° from the current base angle) so the authored points can produce obvious 3D motion instead of tiny fractional changes.

Global instance: `extern Scene3DLoader g_scene3d_loader;`

---

## `content/music_cue_loader.h`

Loads authored music cue rows that map timeline start times to XM asset keys.

### `struct MusicCueEntry`
| Field | Type | Description |
|---|---|---|
| `start_time_seconds` | `double` | Absolute global timeline time for the cue |
| `asset_key` | `char[64]` | Project asset key that resolves to an embedded XM |
| `loop` | `bool` | Loop flag from authored cue data |
| `crossfade_seconds` | `float` | Reserved authored crossfade setting |

### `class MusicCueLoader`
```cpp
bool LoadFromText(const char* text);
bool LoadFromFile(const char* filepath);
int GetCueCount() const;
const MusicCueEntry* GetCues() const;
bool IsLoaded() const;
const MusicCueEntry* GetActiveCue(double time_seconds) const;
```

Runtime rule:
- `GetActiveCue()` returns `nullptr` before the first cue starts or when no valid `asset_key` is present, which keeps clean projects silent by default.

Global instance: `extern MusicCueLoader g_music_cue_loader;`

---

## `content/image_cue_loader.h`

Loads image overlay cue rows with authored active/effect timing and optional carry-to-next-scene.

### `struct ImageCueEntry`
| Field | Type | Description |
|---|---|---|
| `scene_id` | `char` | Source scene id: `'A'`, `'B'`, or `'C'` |
| `asset_key` | `char[64]` | Asset lookup key |
| `opacity` | `float` | Base overlay opacity |
| `x`, `y` | `float` | Normalized overlay position |
| `scale` | `float` | Overlay scale relative to viewport height |
| `start_seconds` | `float` | Active window start in source-scene authored time |
| `end_seconds` | `float` | Active window end in source-scene authored time (`<0` = implicit end) |
| `fade_in_seconds` | `float` | Active fade-in duration |
| `fade_out_seconds` | `float` | Active fade-out duration |
| `blend_mode` | `int` | `0` = alpha, `1` = additive, `2` = multiply, `3` = screen |
| `effect_kind` | `int` | Authored effect id |
| `effect_p0..effect_p3` | `float` | Effect parameters |
| `effect_start_seconds` | `float` | Effect window start in source-scene authored time |
| `effect_end_seconds` | `float` | Effect window end in source-scene authored time (`<0` = implicit end) |
| `carry_to_next_scene` | `int` | `0` = scene-local only, `1` = may continue into next scene |
| `layer_order` | `int` | Draw order across overlays (`lower draws first`) |

Timing semantics:
- `Active Start/End` defines when the asset exists.
- `Effect Start/End` defines when animation is applied.
- Runtime converts these authored values into cue-private absolute time using the source scene's position in the current cycle.
- Carry is limited to one scene hop.

### Functions
```cpp
bool LoadFromText(const char* text);
bool LoadFromFile(const char* path);
bool IsLoaded() const;
const ImageCueEntry* FindCueForScene(char scene_name) const;
int GetCueCount() const;
const ImageCueEntry* GetCues() const;
```

Compatibility notes:
- Loader accepts the current row format with `carry_to_next_scene` and older legacy row shapes where supported.
- Runtime preserves older authored `scene end` semantics for effect end when possible.

---

## `content/text_cue_loader.h`

Loads scene text cue rows and text object references from `assets/text_cues.txt`.

### `struct TextCueEntry`
| Field | Type | Description |
|---|---|---|
| `scene_id` | `char` | Source scene id: `'A'`, `'B'`, or `'C'` |
| `kind` | `char[32]` | Text object kind (`title_main`, `credits_main`, `scroll_text`, `multiline_text`) |
| `font_key` | `char[32]` | Named font-atlas key |
| `text` | `char[384]` | Text value (supports escaped `\\n` in authored rows) |
| `opacity` | `float` | Base overlay alpha |
| `x`, `y` | `float` | Normalized anchor coordinates |
| `scale` | `float` | Text scale multiplier |
| `start_seconds` | `float` | Active window start in source-scene authored time |
| `end_seconds` | `float` | Active window end in source-scene authored time (`<0` = implicit end) |
| `fade_in_seconds` | `float` | Active fade-in duration |
| `fade_out_seconds` | `float` | Active fade-out duration |
| `effect_kind` | `int` | Text effect id (`0` none, `1` wave, `2` pulse alpha, `3` drift, `4` reveal_ltr, `5` reveal_fade, `6` reveal_lines) |
| `effect_p0..effect_p3` | `float` | Effect parameters |
| `effect_start_seconds` | `float` | Effect window start in source-scene authored time |
| `effect_end_seconds` | `float` | Effect window end in source-scene authored time (`<0` = implicit end) |
| `blend_mode` | `int` | `0` = alpha, `1` = additive, `2` = multiply, `3` = screen |
| `layer_order` | `int` | Draw order across overlays (`lower draws first`) |

Timing semantics:
- `Active Start/End` defines when the text overlay exists.
- `Effect Start/End` defines when text animation is applied.
- For non-scroll text cues, normalized `x`/`y` are anchor coordinates (`0.5`, `0.5` centers the text object).

### Functions
```cpp
bool LoadFromText(const char* text);
bool LoadFromFile(const char* path);
bool IsLoaded() const;
int GetCueCount() const;
const TextCueEntry* GetCues() const;
```

---

## `content/shader_curve_loader.h`

Loads and evaluates common/per-shader curves.

Common parameters accepted by runtime:
- shader-wide controls: `exposure`, `fade`, `scene_blend`
- 3D stage controls: `scene3d_fov_degrees`, `scene3d_camera_distance`, `scene3d_offset_x`, `scene3d_offset_y`, `scene3d_offset_z`, `scene3d_model_scale`, `scene3d_material_ambient`, `scene3d_material_diffuse`, `scene3d_material_specular`, `scene3d_material_shininess`, `scene3d_material_rim`, `scene3d_tint_r`, `scene3d_tint_g`, `scene3d_tint_b`, `scene3d_yaw_deg`, `scene3d_pitch_deg`, `scene3d_roll_deg`, `scene3d_light_yaw_deg`, `scene3d_light_pitch_deg`, `scene3d_light_intensity`, `scene3d_light_r`, `scene3d_light_g`, `scene3d_light_b`

Per-shader parameters accepted by runtime:
- `speed`, `intensity`, `warp`
- `dyn0`, `dyn1`, `dyn2`, `dyn3`
- `palette_low_r`, `palette_low_g`, `palette_low_b`
- `palette_mid_r`, `palette_mid_g`, `palette_mid_b`

---

## `content/shader_pipeline_loader.h`

Loads and validates the authored shader pass graph exported by the Python editor.

### `enum class ShaderPipelinePassType`
| Value | Meaning |
|---|---|
| `kScene` | Fullscreen scene pass |
| `kOverlay` | Overlay-style shader pass |
| `kMaterial` | Material/shading pass |
| `kPostFx` | Post-processing pass |
| `kOutput` | Final output pass |

### `struct ShaderPipelinePass`
| Field | Type | Description |
|---|---|---|
| `scene_id` | `char` | Authored scene id or `-` for final output |
| `pass_id` | `char[32]` | Stable authored pass identifier |
| `pass_type` | `ShaderPipelinePassType` | Pass role in the authored graph |
| `shader_scene_id` | `int` | Shader branch id used by scene/overlay passes |
| `start_seconds`, `end_seconds` | `float` | Authored active window (`end < 0` = implicit end) |
| `fade_in_seconds`, `fade_out_seconds` | `float` | Active fade envelope for the pass |
| `inputs` | `char[128]` | Input slot list |
| `outputs` | `char[128]` | Output slot name |
| `dependencies` | `char[128]` | Upstream pass id list |
| `enabled` | `bool` | Whether the pass is active |
| `blend_mode` | `int` | Overlay blend mode |
| `opacity` | `float` | Overlay opacity |
| `layer_order` | `int` | Deterministic composition order |

### `class ShaderPipelineLoader`
```cpp
bool   LoadFromText(const char* text);
bool   LoadFromFile(const char* filepath);
bool   IsLoaded() const;
int    GetPassCount() const;
const ShaderPipelinePass* GetPasses() const;
```

Global instance: `extern ShaderPipelineLoader g_shader_pipeline_loader;`

Runtime note:
- When `[shader_pipeline]` data is present, the runtime prefers pipeline-authored overlay passes and falls back to legacy `shader_cues` only if the pipeline is absent.
- `palette_high_r`, `palette_high_g`, `palette_high_b`

Point rows in `assets/shader_curves.txt` are exported as `t:v:in_ease:out_ease:mode`, and `scene3d_*` values are evaluated in unit space without a final `0..1` clamp.

Dyn timing semantics:
- `dyn0` controls per-scene shader time progression rate.
- Runtime integrates per-scene shader time on CPU and passes it as dedicated uniforms (`a_st`, `b_st`).
- Shader code should use those uniforms for animated phase input instead of multiplying a changing dyn-speed factor against large absolute wall time.

### Functions
```cpp
bool LoadFromText(const char* text);
bool LoadFromFile(const char* path);
bool IsLoaded() const;
bool EvaluateCommon(ShaderCurveParam param, double time_seconds, float* out_value) const;
bool EvaluateShader(int shader_id, ShaderCurveParam param, double time_seconds, float* out_value) const;
```

---

## `renderer/renderer.h`

### `struct RendererState`
Owns GL handles, shader program, and registered image overlay texture slots.
Do not share across threads. Get/release via `InitializeRenderer`/`ShutdownRenderer`.
When `REV_ENABLE_3D=ON`, it also owns the fixed-size `scene3d_objects[]`, `scene3d_object_alpha[]`, and `mesh_scenes[]` arrays, so keep it under explicit runtime ownership (heap allocation or equivalent long-lived owner) rather than stack-allocating it in `main_win32.cc`.

Overlay composition semantics:
- `RenderFrame` merges shader/image/text overlays into one stable draw order using authored `layer_order` (`lower draws first`) across overlay types.
- Equal-layer ties are stable by bucket: negative-layer shader overlays, then image overlays, then non-negative-layer shader overlays, then text overlays.

Key fields:
| Field | Type | Description |
|---|---|---|
| `window_handle` | `HWND` | Owned window handle |
| `device_context` | `HDC` | WGL device context |
| `gl_render_context` | `HGLRC` | OpenGL 3.3 context |
| `shader_program` | `ShaderProgram` | Compiled GLSL program |
| `font_library` | `FontLibraryConfig` | Authored named font-atlas definitions |
| `font_atlases[]` | `FontAtlasRuntime[]` | Realized GL atlas textures keyed by font name |
| `font_atlas_count` | `int` | Number of active realized font atlases |
| `startup_title_layout` | `TextGlyphLayout` | Precached startup title glyph positions |
| `startup_credits_layout` | `TextGlyphLayout` | Precached startup credits glyph positions |
| `scroll_layout` | `TextGlyphLayout` | Precached scroll-message glyph positions |
| `text_layout_cache[]` | `CachedTextLayout[]` | Runtime cache for dynamic scene text layouts |
| `image_slots[]` | `RendererImageSlot[]` | Overlay textures indexed by asset key |
| `image_slot_count` | `int` | Number of active slots |

### Functions
```cpp
bool InitializeRenderer(const void* window_handle,
    const FontLibraryConfig& font_config,
    const TextOverlayConfig& text_config,
    RendererState* state);
void RenderFrame(RendererState& state,
    float exposure,
    float fade,
    double time,
    double scene_a_shader_time,
    double scene_b_shader_time,
    const SceneVisualConfig& scene_a,
    const SceneVisualConfig& scene_b,
    float scene_blend,
    const float scene_a_dyn[4],
    const float scene_b_dyn[4],
    int shader_overlay_count,
    const SceneVisualConfig* shader_overlay_visual,
    const float* shader_overlay_dyn4,
    const float* shader_overlay_time,
    const float* shader_overlay_opacity,
    const int* shader_overlay_blend_mode,
    const int* shader_overlay_layer_order,
    int image_overlay_count,
    const int* image_texture_slots,
    const float* image_opacity,
    const int* image_blend_mode,
    const int* image_layer_order,
    const float* image_x,
    const float* image_y,
    const float* image_scale,
    float text_reveal,
    float scroll_alpha,
    int scene_text_overlay_count,
    const char* const* scene_text_kind,
    const char* const* scene_text_font_key,
    const char* const* scene_text,
    const float* scene_text_x,
    const float* scene_text_y,
    const float* scene_text_scale,
    const float* scene_text_alpha,
    const float* scene_text_color_r,
    const float* scene_text_color_g,
    const float* scene_text_color_b,
    const float* scene_text_color_a,
    const int* scene_text_blend_mode,
    const int* scene_text_layer_order,
    const int* scene_text_effect_kind,
    const float* scene_text_effect_p0,
    const float* scene_text_effect_p1,
    const float* scene_text_effect_p2,
    const float* scene_text_effect_p3,
    const double* scene_text_effect_time,
    const double* scene_text_scroll_time);
void ShutdownRenderer(RendererState* state);

int RegisterImageTexture(RendererState* state, const char* asset_key, uint32_t gl_id, int width, int height);
int FindImageSlot(const RendererState& state, const char* asset_key);
```

---

## `renderer/image_overlay.h`

### `struct ImageTexture`
| Field | Type | Description |
|---|---|---|
| `gl_id` | `uint32_t` | GL texture ID (0 = not loaded) |
| `width` | `int` | Image pixel width |
| `height` | `int` | Image pixel height |
| `asset_key` | `char[64]` | Unique asset identifier |

### Functions
```cpp
uint32_t LoadImageTexture(const char* filepath, int* out_width = nullptr, int* out_height = nullptr);
uint32_t LoadImageTextureFromMemory(const unsigned char* data, unsigned int size, int* out_width = nullptr, int* out_height = nullptr);

unsigned char* LoadImagePixels(const char* filepath, int* out_width = nullptr, int* out_height = nullptr);
unsigned char* LoadImagePixelsFromMemory(const unsigned char* data, unsigned int size, int* out_width = nullptr, int* out_height = nullptr);
void FreeImagePixels(unsigned char* pixels);

// Single image overlay draw (performs full GL state setup/teardown per call)
void DrawImageOverlay(uint32_t gl_texture_id,
    float x, float y, float scale,
    float opacity,
    int blend_mode,
    int viewport_width, int viewport_height,
    int image_width, int image_height);

// Batched image overlay draw (amortizes GL state changes across multiple images)
void BeginImageOverlayBatch();
void DrawImageOverlayBatched(uint32_t gl_texture_id,
    float x, float y, float scale,
    float opacity,
    int blend_mode,
    int viewport_width, int viewport_height,
    int image_width, int image_height);
void EndImageOverlayBatch();
```

**Image overlay semantics:**
- `x`, `y`: normalized [0,1] screen center position (0.5, 0.5 = screen center)
- `scale`: relative to screen height in NDC (1.0 = 50% of viewport height)
- `opacity`: [0,1] alpha multiplier
- `blend_mode`: 0=normal, 1=additive, 2=multiply, 3=screen

**Batching workflow:**
1. Call `BeginImageOverlayBatch()` once before drawing multiple consecutive images
2. Call `DrawImageOverlayBatched()` for each image (only changes texture + blend mode)
3. Call `EndImageOverlayBatch()` after the last image
4. Batching amortizes ~10 GL state changes per image when drawing multiple overlays

Notes:
- Image loading uses GDI DIB for PNG decode (zero extra dependencies).
- `LoadImagePixels` variants decode to RGBA for CPU-side sampling.
- RenderFrame uses smart batching: begins batch on first image, ends when non-image item encountered.

---

## `renderer/shader.h`

### `struct ShaderProgram`
Caches all GL uniform locations. Never access `program_id` directly.

### Functions
```cpp
bool CompileShaderProgram(ShaderProgram* program);
void UseShaderProgram(const ShaderProgram& program);
void SetShaderUniforms(const ShaderProgram&,
    float res_x, float res_y,
    float exposure, float fade, double time,
    float a_st, float b_st,
    const SceneVisualConfig& scene_a,
    const SceneVisualConfig& scene_b,
    float scene_blend,
    const float scene_a_dyn[4],
    const float scene_b_dyn[4]);
void DeleteShaderProgram(ShaderProgram* program);
void UnbindShaderProgram();

// Flag texture support (for flag_wave shader, scene ID 34)
bool LoadFlagTexture(const char* image_key_or_path);
void SetFlagTextureScale(float scale);
void BindFlagTexture(const ShaderProgram& program);
void CleanupFlagTexture();
```

`CompileShaderProgram` uses embedded shader text from generated `embedded_assets.h`:
- `kEmbeddedVertexShader`
- `kEmbeddedFragmentShader`

The fragment source is generated by CMake from split shader authoring files:
- `assets/shader/shader_common.glsl`
- `assets/shader/scenes/*.glsl`
- `assets/shader/shader_footer.glsl`

**Flag Texture API** (for `flag_wave` shader, scene ID 34):
- `LoadFlagTexture(key)` loads an image from embedded assets by project key
- `SetFlagTextureScale(scale)` sets the scale uniform (1.0 = original, <1.0 = zoom out, >1.0 = zoom in)
- `BindFlagTexture(program)` binds the texture and scale uniform to the shader program (call after `SetShaderUniforms`)
- `CleanupFlagTexture()` releases OpenGL texture resources

See [docs/FLAG-WAVE-SHADER.md](FLAG-WAVE-SHADER.md) for usage guide.

---

## `renderer/shader_sources.h`

```cpp
const char* GetVertexShaderSource();
const char* GetFragmentShaderSource();
```
Legacy helpers returning inline GLSL source strings. Runtime compilation path uses
embedded assets (`kEmbeddedVertexShader`, `kEmbeddedFragmentShader`) in `shader.cc`.
