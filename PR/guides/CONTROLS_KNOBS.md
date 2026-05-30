# Controls & Knobs Reference — revision2026

Scene/timeline knobs live in `src/sequence/scene_management.cc`.
Font library settings live in `assets/font_config.txt`. Text and scroll knobs live in `assets/text_config.txt`, both exported by `tools/scene_block_editor.py` and loaded at startup.

Build command: `cmake --build build --config Release`

---

## Scene Timeline (`kTimelineConfig`)

| Macro | Default | Unit | Description |
|---|---|---|---|
| *(warmup_seconds)* | 0.25 | s | Black warmup window before music init and scene-time advance |
| *(startup_fade_in_seconds)* | 1.0 | s | Duration of initial black-to-scene fade |
| *(startup_hold_seconds)* | 1.5 | s | Static title card hold time |
| *(startup_transition_seconds)* | 0.0 | s | Transition from title to Scene A |
| *(main_scene_a_seconds)* | 30.0 | s | Duration of Scene A (NebulaDrift) |
| *(main_scene_b_seconds)* | 30.0 | s | Duration of Scene B (RibbonAurora) |
| *(main_scene_c_seconds)* | 30.0 | s | Duration of Scene C (NocturneFog) |
| *(main_scene_transition_seconds)* | 1.25 | s | Cross-blend window between scenes |

> These are set directly in the `kTimelineConfig` struct initializer, not as named macros.

---

## Scene Visuals (`kSceneVisualA/B/C`)

Per-scene shader palette and motion parameters, also set directly in struct initializers.

| Field | Type | Description |
|---|---|---|
| `palette_low` | `ColorRgb` | Minimum colour of scene palette |
| `palette_high` | `ColorRgb` | Maximum colour of scene palette |
| `speed` | `float` | Base animation speed multiplier |
| `intensity` | `float` | Visual intensity / contrast |
| `warp` | `float` | Domain-warp / noise distortion strength |

---

## Scene Look / Camera (`kMainSceneA/B/CLookConfig`)

Controls how `ContentParams` maps `exposure` and `fade` over time within each scene.

| Field | Default (A/B/C) | Description |
|---|---|---|
| `exposure_base` | 0.76 / 0.74 / 0.72 | Base exposure at scene start |
| `exposure_ramp` | 0.02 / 0.00 / 0.18 | Exposure delta over scene duration |
| `fade_base` | 0.04 / 0.08 / 0.03 | Base fade value |
| `fade_ramp` | -0.04 / -0.08 / -0.03 | Fade delta over scene duration |

---

## Rhythm (`kRhythmConfig`)

Tempo reference knobs kept in scene management for authored timing conventions.
Current runtime does not expose beat/bar pulse fields in `SequenceState` uniforms.

| Field | Default | Description |
|---|---|---|
| `bpm` | 125.0 | Beats per minute of the track |
| `beats_per_bar` | 8 | Beats per bar |
| `phrase_bars` | 16 | Bars per phrase |
| `beat_sharpness` | 16.0 | pow() exponent controlling beat attack sharpness |
| `bar_sharpness` | 15.0 | pow() exponent controlling bar attack sharpness |

---

## Cursor Visibility

Authored in `assets/scene_data.txt`, exported by `tools/scene_block_editor.py`.
Applied at runtime via `IsCursorVisibleDefault()` and `IsCursorVisibleForScene(MainSceneId)` in `scene_management.cc`.

### Timeline row (`cursor_default`)

| Value | Meaning |
|---|---|
| `hide` | Cursor hidden by default for all scenes (recommended for compo) |
| `show` | Cursor visible by default unless overridden per scene |

Set in the editor's **Cursor** combobox in the timeline bar.

### Timeline row (`framing_mode`, optional)

| Value | Meaning |
|---|---|
| `stretch` | Fill the full window (default behavior) |
| `lock_16_9` | Keep authored 16:9 framing with centered letterbox/pillarbox as needed |

Set in the editor's timeline framing control.

### Per-scene row (`cursor_override`)

| Value | Meaning |
|---|---|
| `inherit` | Use whatever `cursor_default` specifies for this scene |
| `hide` | Force cursor hidden for this scene, regardless of default |
| `show` | Force cursor visible for this scene, regardless of default |

Set in the editor's per-scene **Scene Settings → Mouse Pointer** combobox.

**Current authored values** (`assets/scene_data.txt`):
- Timeline default: `hide`
- Scene A: `inherit` → effectively hidden
- Scene B: `show` → cursor visible
- Scene C: `inherit` → effectively hidden

---

## Image Overlay Timing

Authored in the image overlay modal in `tools/scene_block_editor.py` and exported to `assets/image_cues.txt`.

| Field | Meaning |
|---|---|
| `Active Start` | When the image becomes live within the source scene |
| `Active End` | When the image stops being live (`scene end` / `next scene end` may export as implicit `-1`) |
| `Effect Start` | When the animation/effect begins |
| `Effect End` | When the animation/effect stops (`scene end` / `next scene end` may export as implicit `-1`) |
| `Carry To Next Scene` | Allows the image to continue into the next authored scene only |

Runtime rule:
- image timing is evaluated in a scene-cycle model built from the authored scene order and scene durations
- carried images keep their source-scene timing semantics across the boundary
- effect timing is independent from active timing except that the effect window is clamped to the active window

---

## Optional 3D Stage / Shared Curve Editor

Authored in the 3D scene settings modal in `tools/scene_block_editor.py` and exported to `assets/scene3d.txt` and `assets/shader_curves.txt`.

### Base inspector fields

| Key | Meaning |
|---|---|
| `fov_degrees` | Camera field of view in degrees |
| `camera_distance` | Camera pull-back distance from the stage |
| `rotation_yaw_deg`, `rotation_pitch_deg`, `rotation_roll_deg` | Base 3D orientation angles in degrees |
| `rotation_speed_deg` | Continuous authored yaw spin in degrees per second |
| `offset_x/y/z` | Stage offset controls |
| `model_scale` | Model scale multiplier |
| `material_ambient/diffuse/specular/shininess/rim` | Lightweight material shaping knobs |
| `light_yaw_deg`, `light_pitch_deg` | Main light direction in degrees |
| `light_intensity` | Main light strength multiplier |
| `light_r/g/b` | Main light color |
| `tint_r/g/b` | Stage tint color |
| `instance_count`, `instance_step_x/y/z`, `instance_yaw_step_deg`, `instance_scale_step` | Lightweight repeated-copy layout controls |

### Common curve targets for the 3D stage

Use the shared curve editor for:
- `scene3d_fov_degrees`
- `scene3d_camera_distance`
- `scene3d_offset_x`, `scene3d_offset_y`, `scene3d_offset_z`
- `scene3d_model_scale`
- `scene3d_material_ambient`, `scene3d_material_diffuse`, `scene3d_material_specular`, `scene3d_material_shininess`, `scene3d_material_rim`
- `scene3d_tint_r`, `scene3d_tint_g`, `scene3d_tint_b`
- `scene3d_light_yaw_deg`, `scene3d_light_pitch_deg`
- `scene3d_light_intensity`, `scene3d_light_r`, `scene3d_light_g`, `scene3d_light_b`
- `scene3d_yaw_deg`, `scene3d_pitch_deg`, `scene3d_roll_deg`

Important notes:
- These curve values use the **same units as the inspector**, not normalized `0..1` offsets.
- The editor keeps stable authoring ranges for unit-space curves (for example yaw/pitch/roll around ±360° from the current base angle) so large rotations are easy to draw.
- Blender light objects are **not** imported through the current OBJ/MTL → `.meshbin` path; use the editor's `light_yaw_deg`, `light_pitch_deg`, `light_intensity`, and `light_r/g/b` controls for runtime lighting.
- After editing curves or light settings, run **Export** / **Do It All** before rebuilding so the embedded assets refresh.

---

## Text Overlay

Static title and credits are rendered from authored GDI atlases during startup.
Fonts are defined as named entries in `assets/font_config.txt`. Rows may optionally include a font source path (for example `fonts/MyFont.ttf`) that is registered as a private runtime font before atlas build; missing/unloadable sources fall back to the configured face. Text and scroller settings, including which font key each text element uses, are exported to `assets/text_config.txt`.

| Key | Default | Description |
|---|---|---|
| `font | key | face | height | first | last | source(optional)` | `font | default | Consolas | 48 | 32 | 126 |` | One font-library row in `assets/font_config.txt` |
| `title_font` | `default` | Font key used for the startup title |
| `credits_font` | `default` | Font key used for the startup credits |
| `title_r/g/b` | `0.96 / 0.96 / 0.96` | Title text RGB color components (0–1 each) |
| `title_a` | `0.98` | Title text opacity (0–1) |
| `credits_r/g/b` | `0.64 / 0.72 / 0.80` | Credits text RGB color components (0–1 each) |
| `credits_a` | `0.82` | Credits text opacity (0–1) |
| *(margin_x)* | 50 | Left margin in pixels |
| *(margin_top)* | 60 | Top margin in pixels |
| *(line_spacing)* | 56 | Line spacing in pixels |
| *(title)* | `"REVISION 2026"` | First line of overlay text |
| *(credits)* | `"by your_handle / your_group"` | Second line of overlay text |

---

## Sinus Scroll — Basic Motion

| Key | Default | Unit | Description |
|---|---|---|---|
| `scroll_font` | `default` | — | Font key used for the sinus scroller |
| `scroll_message` | `"  REVISION 2026  ::  ..."` | — | The scrolling text string |
| `scroll_speed` | 360.0 | px/s | Horizontal scroll travel speed |
| `scroll_loop_restart` | 60.0 | px | Extra offscreen gap before loop restart |
| `scroll_fade_end` | 1.00 | 0–1 | `main_scene_blend` value at which scroll is fully invisible |

---

## Sinus Scroll — Character Layout

| Key | Default | Description |
|---|---|---|
| `scroll_char_scale` | 1.00 | Glyph height multiplier relative to font atlas cell |
| `scroll_char_gap` | 3 | Extra pixel gap between each glyph (integer pixels) |
| `scroll_baseline_norm` | 0.16 | Vertical centre lane: 0=bottom of viewport, 1=top |

---

## Sinus Scroll — Wave (Vertical Ripple)

| Key | Default | Description |
|---|---|---|
| `scroll_wave_amp` | 30.0 | Peak vertical displacement in pixels |
| `scroll_wave_freq` | 0.00775 | Spatial frequency (cycles per pixel along X) |
| `scroll_wave_speed` | 0.50 | Animation rate multiplier for the wave phase |
| `scroll_wave_size_min` | 0.00 | Minimum glyph scale at wave peak (0=invisible, 1=no effect) |
| `scroll_wave_gap_scale` | 1.00 | 0=fixed gap width, 1=gap scales proportionally with glyph size |

---

## Sinus Scroll — Y-Axis Twist (Horizontal Rotation)

Simulates glyphs rotating on their Y axis as they travel left to right, producing a "tumbling" effect.

| Key | Default | Description |
|---|---|---|
| `scroll_y_axis_amount` | 1.00 | Blend: 0=no rotation, 1=full Y-axis rotation |
| `scroll_min_width` | 0.25 | Minimum glyph width scale when fully sideways (0–1) |
| `scroll_twist_max_angle_deg` | 90.0 | Maximum rotation angle at screen edges (degrees) |
| `scroll_twist_travel_dist` | 1.00 | Fraction of screen width used to reach max angle |
| `scroll_twist_offset` | 0.0 | Constant rotation bias added to all glyphs (radians) |
| `scroll_twist_wave` | 1.50 | How much the sinus wave position couples into twist angle |
| `scroll_twist_speed` | 1.00 | Independent time-driven rotation speed (rad/sec, 0=static) |

---

## Sinus Scroll — X-Axis Tilt (Vertical Squash)

| Key | Default | Description |
|---|---|---|
| `scroll_x_axis_amount` | 1.00 | Blend: 0=no squash, 1=full X-axis squash |
| `scroll_x_axis_min` | 0.75 | Minimum glyph height scale at maximum tilt (0–1) |

---

## Sinus Scroll — Bevel Depth Offsets

Controls position offsets for the two bevel outline passes (highlight and shadow).

| Key | Default | Unit | Description |
|---|---|---|---|
| `scroll_bevel_offset_y` | 0.0 | px | Y-axis bevel offset distance |
| `scroll_bevel_offset_x` | 2.0 | px | X-axis bevel offset distance |
| `scroll_depth_y` | 2.0 | px | Constant vertical bias of the bevel layer |
| `scroll_depth_speed` | 0.00 | rad/s | Sinusoidal animation speed of depth offset |

---

## Sinus Scroll — Bevel Shading and Colour

Two offset passes: highlight (upper-left) and shadow (lower-right), both tinted by the bevel colour.

| Key | Default | Description |
|---|---|---|
| `scroll_bevel_alpha` | 1.00 | Opacity of bevel passes (0–1) |
| `scroll_bevel_highlight` | 1.20 | Intensity multiplier for the highlight pass |
| `scroll_bevel_shadow` | 1.20 | Intensity multiplier for the shadow pass |
| `scroll_bevel_tint_r` | 1.00 | Bevel tint red channel |
| `scroll_bevel_tint_g` | 0.00 | Bevel tint green channel |
| `scroll_bevel_tint_b` | 0.00 | Bevel tint blue channel |
| `scroll_bevel_tint_blend` | 1.00 | 0=grayscale ramp, 1=fully tinted with bevel colour |

---

## Sinus Scroll — Front Face Lighting

Controls the brightness of the main front-facing glyph quad.

| Key | Default | Description |
|---|---|---|
| `scroll_face_base` | 0.85 | Base brightness of the front-facing glyph |
| `scroll_face_gain` | 0.75 | Extra brightness when glyph is fully front-facing (abs_cos = 1) |

---

## Quick Recipes

**Faster, more dramatic scroll:**
```text
scroll_speed | 500.000
scroll_wave_amp | 80.000
scroll_wave_size_min | 0.100
```

**Subtle slow ambient crawl:**
```text
scroll_speed | 120.000
scroll_wave_amp | 20.000
scroll_wave_speed | 0.200
scroll_twist_speed | 0.200
```

**Flat non-twisting scroll:**
```text
scroll_y_axis_amount | 0.000
scroll_x_axis_amount | 0.000
scroll_twist_speed | 0.000
```

**Coloured bevel (e.g. cyan):**
```text
scroll_bevel_tint_r | 0.300
scroll_bevel_tint_g | 0.900
scroll_bevel_tint_b | 1.000
scroll_bevel_tint_blend | 1.000
```
