# OpenGL in revision2026 — How It Works

This document explains the entire OpenGL rendering pipeline as it exists in this project: how the context gets created, how the shader runs, what every uniform does, and how the frame is produced each tick. It is written for someone who can read C++ and GLSL but is not necessarily familiar with Win32 GL setup.

---

## 1. Getting a GL Context on Windows (WGL)

OpenGL on Windows is bootstrapped through **WGL** — the Windows GL interface layer — rather than a platform-neutral library like GLFW. There is no external windowing helper. Every step is done directly in `src/renderer/renderer.cc`.

### Step 1 — Pixel Format

Before any GL object can be created, the window's device context (HDC) must be configured with a pixel format that describes the framebuffer layout (RGBA 32-bit, 24-bit depth, double buffered):

```cpp
SetupPixelFormat(state->device_context);  // calls ChoosePixelFormat + SetPixelFormat
```

This must happen exactly once, before any GL context is created on that HDC.

### Step 2 — Legacy Context Bootstrap

`wglCreateContext` only creates a legacy GL 1.x context. A modern context cannot be created directly — you need an existing current context first to call `wglGetProcAddress`:

```cpp
state->gl_render_context = wglCreateContext(state->device_context);
wglMakeCurrent(state->device_context, state->gl_render_context);
```

### Step 3 — Upgrade to GL 3.3 Compatibility

Once the legacy context is current, `wglCreateContextAttribsARB` is fetched as an extension function and used to create the real context:

```cpp
const int attribs[] = {
    WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
    WGL_CONTEXT_MINOR_VERSION_ARB, 3,
    WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
    0,
};
HGLRC modern_context = wglCreateContextAttribsARB(state->device_context, 0, attribs);
```

The legacy context is then deleted and the modern one is made current. If `wglCreateContextAttribsARB` is unavailable or returns null, the renderer fails and the show is aborted.

**Why compatibility profile?** The fullscreen quad and image overlay pass are drawn with legacy immediate-mode calls (`glBegin`/`glEnd`). Switching to core profile would require VBO/VAO scaffolding, increasing runtime code size.

### Step 4 — VSync

`wglSwapIntervalEXT(1)` is called once after context creation. VSync is always on — no runtime toggle.

---

## 2. Extension Function Loading

OpenGL functions above version 1.1 are not in `opengl32.lib` on Windows. They must be fetched as function pointers via `wglGetProcAddress` after the context is current. This is done in `CompileShaderProgram` in `src/renderer/shader.cc`:

```cpp
glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
glUniform1f     = (PFNGLUNIFORM1FPROC)   wglGetProcAddress("glUniform1f");
// ... etc.
```

Each pointer is checked before use. If the driver is too old and `glCreateProgram` comes back null, the renderer fails early with a clear diagnostic.

---

## 3. Shader Compilation

Shaders are compiled at runtime from embedded text assets (`kEmbeddedVertexShader`, `kEmbeddedFragmentShader`).

Shader authoring source-of-truth is split across:
- `assets/shader/shader_common.glsl`
- `assets/shader/scenes/*.glsl`
- `assets/shader/shader_footer.glsl`

CMake concatenates those files into generated `assets/shader/fragment.glsl` and also embeds the combined text into `kEmbeddedFragmentShader`.
In diagnostics builds, filesystem overrides from `assets/shader/vertex.glsl` and generated `assets/shader/fragment.glsl` are loaded when present.

There are two shaders:

| Shader | Embedded symbol | Role |
|--------|------------------|------|
| Vertex | `kEmbeddedVertexShader` | Passes geometry through, computes `uv` |
| Fragment | `kEmbeddedFragmentShader` | Scene dispatch, blend, grading |

Compilation sequence in `CompileShaderProgram`:

```
glCreateShader(GL_VERTEX_SHADER)
  → glShaderSource, glCompileShader
  → check GL_COMPILE_STATUS

glCreateShader(GL_FRAGMENT_SHADER)
  → glShaderSource, glCompileShader
  → check GL_COMPILE_STATUS

glCreateProgram
  → glAttachShader (both)
  → glLinkProgram

glGetUniformLocation  (called once per uniform, results stored in ShaderProgram struct)
```

Uniform locations are cached in `ShaderProgram` at compile time so there are no string lookups per frame.

---

## 4. Vertex Shader

```glsl
#version 330 compatibility

out vec2 uv;

void main() {
  uv = gl_Vertex.xy * 0.5 + 0.5;
  gl_Position = gl_Vertex;
}
```

`gl_Vertex` is a legacy built-in, available in the compatibility profile. It receives the raw clip-space positions from `glVertex2f`. The shader converts them to a `[0,1]×[0,1]` UV and passes it to the fragment shader.

---

## 5. Drawing the Fullscreen Quad

The entire scene is drawn as two triangles covering clip space `[-1,1]²`:

```cpp
glBegin(GL_TRIANGLES);
glVertex2f(-1.0f, -1.0f);  glVertex2f( 1.0f, -1.0f);  glVertex2f( 1.0f,  1.0f);
glVertex2f(-1.0f, -1.0f);  glVertex2f( 1.0f,  1.0f);  glVertex2f(-1.0f,  1.0f);
glEnd();
```

There are no VBOs, VAOs, or index buffers. The compatibility profile allows this immediate-mode path. For a 6-vertex fullscreen pass the cost is negligible.

---

## 6. Fragment Shader — Overview

The fragment shader is where all rendering happens. Every pixel on screen runs this shader once per frame. The structure is:

```
uniforms in
  ↓
helpers: hash, noise, fbm, Palette, Beat, Bass, PhraseLfo, StarField
  ↓
scene functions: SceneNebulaDrift, SceneRibbonAurora, SceneNocturneFog
  ↓
RenderScene()  — dispatches by scene_id
  ↓
main()
  → blend two scenes (col_a, col_b)
  → phrase modulation
  → grading pass (fog, contrast, vignette)
  → exposure × (1 - fade)
  → frag_color
```

### 6.1 Uniforms

All uniforms are set every frame in `SetShaderUniforms`. None are ever left at their default zero value during a normal frame.

| Uniform | Type | Source | Meaning |
|---------|------|--------|---------|
| `time` | float | `GetTimeSeconds()` | Wall-clock animation time |
| `a_st` | float | CPU-integrated scene A shader time | Scene A phase time after `dt * speed * dyn0_factor` integration |
| `b_st` | float | CPU-integrated scene B shader time | Scene B phase time after `dt * speed * dyn0_factor` integration |
| `exposure` | float | `SequenceState.exposure` (+ common curve modulation) | Overall brightness multiplier |
| `fade` | float | `SequenceState.fade` (+ common curve modulation) | Fade-to-black amount (0=visible, 1=black) |
| `scene_a_id` | int | `GetSceneVisualConfig(current_scene).scene_id` | Currently active authored shader scene ID |
| `scene_b_id` | int | `GetSceneVisualConfig(next_scene).scene_id` | Scene blending toward |
| `scene_blend` | float | `sequence_state.main_scene_blend` | 0=fully A, 1=fully B |
| `a_palette_low` | vec3 | `SceneVisualConfig.palette_low` | Dark colour for scene A |
| `a_palette_high` | vec3 | `SceneVisualConfig.palette_high` | Bright colour for scene A |
| `a_speed` | float | `SceneVisualConfig.speed` | Motion rate multiplier for scene A |
| `a_intensity` | float | `SceneVisualConfig.intensity` | Output brightness scalar for scene A |
| `a_warp` | float | `SceneVisualConfig.warp` | Shape/complexity nudge for scene A |
| `a_dyn0..a_dyn3` | float | shader curves (`shader_id:N`) | Extra authored modulation channels for scene A (`dyn0` drives integrated `a_st`) |
| `b_palette_*` etc. | — | Next scene's `SceneVisualConfig` | Same set for the blend target scene |
| `b_dyn0..b_dyn3` | float | shader curves (`shader_id:N`) | Extra authored modulation channels for scene B (`dyn0` drives integrated `b_st`) |

Important timing rule:
- Avoid `st = time * speed * dyn_speed` style phase computation in GLSL.
- Use CPU-integrated `a_st` / `b_st` for animated phase so dyn-speed modulation remains smooth over long runs.

### 6.2 Scene Dispatch

`renderScene` is called twice per frame (once for each blend slot) and routes by authored integer scene ID.

Implemented IDs are authored in split scene files under `assets/shader/scenes/*.glsl` and compiled into generated `assets/shader/fragment.glsl`.
Unknown IDs should be handled by shader fallback logic.

### 6.3 Helper Functions

**`hash(vec2 p)`** — Classic sine-based pseudo-random scalar. Fast, no texture lookup required.

**`noise(vec2 p)`** — Smooth value noise: bilinear interpolation of four `hash` calls at integer grid corners, with a smoothstep fade curve preventing linear interpolation artefacts.

**`fbm(vec2 p)`** — Fractal Brownian Motion: 4 octaves of value noise summed with halving amplitude per octave. Used for nebula clouds, fog structure, and ridge profiles.

**`Palette(low, high, t)`** — Linear interpolation between two colours. `t` should be in `[0,1]`.

**`StarField(coord, density, radius, twinkle_rate)`** — Places one star per grid cell at a random sub-cell position. Uses a smooth radial falloff (`smoothstep`) centred on that position, and a sine twinkle. The key design: stars are positioned at a *random offset within their cell*, not at the cell centre, and the radial falloff is smooth — this avoids the block-artefact pattern that results from thresholding `hash` directly on pixel coordinates.

---

## 7. Grading Pass

After the two scenes are blended, a fixed post-processing pass runs on `col`:

```glsl
// Atmospheric fog
float horizon_fog = exp(-8.0 * abs(uv.y - 0.33));   // horizon band
float bottom_fog  = smoothstep(0.0, 0.55, 1.0 - uv.y); // ground lift
float fog_mix = clamp(0.2 * horizon_fog + 0.3 * bottom_fog, 0.0, 0.55);
col = mix(col, vec3(0.03, 0.035, 0.05), fog_mix);

// Black crush → gamma → contrast
col = max(col - vec3(0.02), vec3(0.0));   // lift black point to crush near-blacks
col = pow(col, vec3(1.12));               // slight gamma darkening
col = clamp((col - 0.5) * 1.18 + 0.5, 0.0, 1.0);  // contrast expand around mid

// Vignette
float vignette = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
col *= 0.72 + 0.28 * pow(clamp(vignette * 18.0, 0.0, 1.0), 0.42);

// Final exposure and fade-to-black
col *= exposure * (1.0 - fade);
```

`fade=0` means fully visible. `fade=1` means pure black. The startup fade-in drives `fade` from 1→0 over the first second.

---

## 8. Image Overlay Pass

After the shader fullscreen quad, the shader program is unbound and an optional image overlay is drawn:

1. A scene cue from `image_cues.txt` resolves an asset key to a renderer image slot.
2. Runtime computes overlay opacity and effect state from cue-private active/effect windows built on top of the scene-cycle timing model.
3. `DrawImageOverlay` renders one alpha-blended quad in normalized screen space using cue `x/y/scale`.
4. If no valid slot is active (`image_texture_slot == -1`), overlay pass is skipped.

Intro/release sourcing policy:
- Authored runtime assets are embedded-only in intro/release behavior.
- Filesystem fallback loading is diagnostics-only (`REV_ENABLE_DIAGNOSTICS`) and not part of compo behavior.

## 8.1 Scene Text Overlay Pass

After startup text is drawn, authored scene text overlays are rendered from `assets/text_cues.txt`.

Text object kinds:
- `title_main`
- `credits_main`
- `scroll_text`
- `multiline_text`

Timing semantics:
- `Active Start/End` controls overlay existence.
- `Effect Start/End` controls animation only.

Placement semantics:
- For non-scroll text objects, normalized `x`/`y` are anchor coordinates in screen space.
- `x=0.5`, `y=0.5` places the text object at screen center.

Reveal/effect semantics for non-scroll text:
- `reveal_ltr`
- `reveal_fade`
- `reveal_lines`

---

## 9. Frame Loop Summary

Each frame in the main loop triggers:

```
wglMakeCurrent(dc, gl_context)        // ensure context is current (required per frame)
UseShaderProgram(shader_program)      // glUseProgram
SetShaderUniforms(...)                // glUniform1f/1i/3f for all uniforms
glBegin/glVertex/glEnd                // two-triangle fullscreen quad
if(image overlay active):
  UnbindShaderProgram()               // glUseProgram(0)
  DrawImageOverlay(...)               // one alpha-blended textured quad
SwapBuffers(dc)                       // present (VSync stalls here)
```

---

## 10. Where to Edit Things

| Goal | File | What to change |
|------|------|---------------|
| Change scene colours/speed/feel | `src/sequence/scene_management.cc` | `kSceneVisualA/B/C` palette, speed, intensity, warp |
| Change exposure/fade per scene | `src/sequence/scene_management.cc` | `kMainSceneALookConfig` etc. |
| Change startup timing or default cursor visibility | `assets/scene_data.txt` | Update `timeline | ...` row |
| Add/rewrite a scene shader | `assets/shader/scenes/*.glsl` | Add/update scene ID branch file with `@shader_id` metadata |
| Change grading (fog/contrast) | `assets/shader/shader_common.glsl` (or `shader_footer.glsl`) | Update shared grading/look code |
| Tune curves for common/shader params | `assets/shader_curves.txt` | Add target/param/mode/keys rows |
| Adjust image overlays per scene | `assets/image_cues.txt` | Asset key, fade windows, transform |
| Adjust scene text objects per scene | `assets/text_cues.txt` | Kind, active/effect timing, reveal params, anchor `x/y` |
| Change default scene mapping/looks | `src/sequence/scene_management.cc` | `kSceneVisualA/B/C` and scene look configs |
