# Shader Authoring Guide

**Write your own GLSL shader scenes for HowIMetYourMod**

## Overview

HowIMetYourMod uses a **scene-based shader dispatch** system: one massive fragment shader with 38+ scene branches. Each scene is authored in a separate `.glsl` file and concatenated at build time.

**Key concepts**:
- **Split shader files**: One file per scene (`assets/shader/scenes/*.glsl`)
- **Scene dispatch**: Runtime switches on `u_scene_id` uniform
- **Shared helpers**: Common functions in `shader_common.glsl`
- **CMake build**: Concatenates split files → `fragment.glsl` → embedded in runtime
- **Curve-driven**: Animate parameters with editor curves (no hardcoded values)

---

## Shader Pipeline Flow

```
Editor (Python)
    ↓ Export
assets/cues.txt (shader_cues, curves, pipeline)
    ↓ CMake Read

CMake Build:
  1. Read shader_common.glsl (helpers)
  2. Glob & sort assets/shader/scenes/*.glsl
  3. Strip @editor_* metadata
  4. Concatenate all → fragment.glsl
  5. Append shader_footer.glsl (main function close)
    ↓ Embed

embedded_assets.h (fragment_shader string)
    ↓ Runtime Load

OpenGL Shader Compilation
    ↓ Render

Fullscreen Quad (immediate mode)
```

---

## File Structure

```
assets/shader/
├── shader_common.glsl       # Shared helpers (palette, vignette, etc.)
├── shader_footer.glsl        # Main function close (gl_FragColor assignment)
├── fragment.glsl             # Generated (gitignore)
├── presets.json              # Shader preset library
└── scenes/
    ├── 00_plasma.glsl        # Scene 0: Plasma effect
    ├── 01_tunnel.glsl        # Scene 1: Tunnel
    ├── 02_starfield.glsl     # Scene 2: Starfield
    ├── ...
    ├── 36_rasterbars_classic.glsl
    ├── 37_rasterbars_plasma.glsl
    └── 38_rasterbars_metaballs.glsl
```

**Naming convention**: `<id>_<name>.glsl`  
- `<id>`: Two-digit scene ID (00-99)
- `<name>`: Descriptive name (lowercase, underscores)

---

## Shader Scene Template

### Basic Scene Structure

```glsl
// @shader_id 42
// @name my_effect
// @editor_dyn dyn0 | Speed multiplier for animation.
// @editor_dyn dyn1 | Zoom level or depth amount.
// @editor_dyn dyn2 | Color cycle rate.
// @editor_dyn dyn3 | Distortion intensity.

if (id == 42) {
    // Convert normalized dyn values [-1, 1] to usable ranges
    float anim_speed = mix(0.5, 3.0, (dyn.x + 1.0) * 0.5);
    float zoom = mix(0.5, 2.0, (dyn.y + 1.0) * 0.5);
    float color_rate = mix(0.1, 5.0, (dyn.z + 1.0) * 0.5);
    float distort = mix(0.0, 0.5, (dyn.w + 1.0) * 0.5);
    
    // Time with speed multiplier
    float t = st * anim_speed;
    
    // Your effect logic here
    vec2 p = uv * 2.0 - 1.0;
    p *= zoom;
    
    // Example: Simple plasma
    float v = sin(p.x * 10.0 + t) + sin(p.y * 10.0 + t * 0.7);
    v += sin((p.x + p.y) * 5.0 + t * 0.5);
    v *= 0.25;
    
    // Map to palette
    col = palette(v * color_rate);
    
    // Apply distortion
    col = mix(col, vec3(dot(col, vec3(0.33))), distort);
    
    // Apply intensity
    col *= ins;
    
    // Vignette
    col *= vignette(uv);
}
```

### Metadata Comments

```glsl
// @shader_id 42
// Unique ID (0-99), must match filename
// Used by runtime to dispatch to this scene

// @name my_effect
// Friendly name for editor dropdown
// Shows as "my_effect" in shader preset list

// @editor_dyn dyn0 | Description of dynamic parameter 0.
// Documents what dyn0-dyn3 control
// Shows in editor curve UI as tooltip/label
```

**Critical**: `@shader_id` must be unique and match filename!

---

## Available Uniforms

### Standard Uniforms

```glsl
uniform float u_time;          // Global time in seconds (0.0 at intro start)
uniform vec2 u_resolution;     // Screen resolution (usually 1920x1080)
uniform int u_scene_id;        // Active scene ID for dispatch

// Shader-specific parameters
uniform vec3 u_palette_low;    // Color triplet (0.0-1.0 RGB)
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;

uniform float u_speed;         // Time multiplier (1.0 = normal)
uniform float u_intensity;     // Effect strength (1.0 = normal)
uniform float u_warp;          // Distortion amount (0.5 = default)

uniform vec4 u_dyn;            // Dynamic parameters (dyn0, dyn1, dyn2, dyn3)
                               // Range: [-1.0, +1.0] (0.0 = neutral)

uniform float u_exposure;      // Brightness/exposure (0.76 = default)
uniform float u_fade;          // Overall fade amount (0.0-1.0)
```

### Optional Uniforms (3D Stage)

```glsl
#ifdef REV_ENABLE_3D
uniform mat4 u_model_matrix;   // Per-mesh transform
uniform mat4 u_view_matrix;    // Camera view
uniform mat4 u_proj_matrix;    // Projection
uniform vec3 u_light_dir;      // Directional light
uniform vec3 u_light_color;
uniform vec3 u_ambient_color;
#endif
```

---

## Shared Helpers (shader_common.glsl)

### Palette Function

```glsl
vec3 palette(float t) {
    // Cosine palette (Inigo Quilez style)
    vec3 a = u_palette_low;
    vec3 b = u_palette_mid - u_palette_low;
    vec3 c = vec3(1.0, 1.0, 1.0);
    vec3 d = u_palette_high;
    return a + b * cos(6.28318 * (c * t + d));
}
```

**Usage**: `vec3 color = palette(time_or_value);`

### Vignette

```glsl
float vignette(vec2 uv) {
    vec2 d = abs(uv - 0.5) * 2.0;
    return 1.0 - dot(d, d) * 0.5;
}
```

**Usage**: `col *= vignette(uv);` (darkens edges)

### Dynamic Parameter Helpers

```glsl
// Convert bipolar [-1, 1] to unipolar [0, 1]
float dynUni(float v) { return (clamp(v, -1.0, 1.0) + 1.0) * 0.5; }

// Convert bipolar with dead zone (values near 0 stay 0)
float dynBiDZ(float v, float dz) {
    float a = abs(v);
    if (a < dz) return 0.0;
    return sign(v) * ((a - dz) / (1.0 - dz));
}

// Scale value to range [min, max]
float dynMul(float v, float vmin, float vmax) {
    return vmin + v * (vmax - vmin);
}
```

**Usage**:
```glsl
float speed = dynMul(dynUni(dyn.x), 0.5, 3.0);  // Map dyn0 to [0.5, 3.0]
float zoom = dynMul(dynUni(dyn.y), 0.8, 2.0);   // Map dyn1 to [0.8, 2.0]
```

### Noise Functions

```glsl
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);  // Smoothstep
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
```

**Usage**: `float n = noise(uv * 10.0 + time);`

---

## Effect Patterns

### Pattern 1: Plasma

```glsl
if (id == 0) {
    float speed = mix(0.5, 2.0, dynUni(dyn.x));
    float complexity = mix(5.0, 20.0, dynUni(dyn.y));
    float t = st * speed;
    
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    
    float v = 0.0;
    v += sin(p.x * complexity + t);
    v += sin(p.y * complexity + t * 0.7);
    v += sin((p.x + p.y) * complexity * 0.5 + t * 0.5);
    v += sin(length(p) * complexity + t * 0.3);
    v *= 0.25;
    
    col = palette(v + t * 0.1);
    col *= ins * vignette(uv);
}
```

### Pattern 2: Tunnel

```glsl
if (id == 1) {
    float speed = mix(0.3, 2.0, dynUni(dyn.x));
    float twist = mix(0.0, 3.0, dynUni(dyn.y));
    float t = st * speed;
    
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float r = length(p);
    float a = atan(p.y, p.x);
    
    // Tunnel depth
    float depth = 1.0 / (r + 0.1) + t;
    
    // Twist angle over depth
    a += depth * twist;
    
    // UV for tunnel texture
    vec2 tunnel_uv = vec2(a / 6.28318, depth);
    
    // Pattern
    float pattern = sin(tunnel_uv.x * 10.0) * sin(tunnel_uv.y * 10.0);
    
    col = palette(pattern * 0.5 + 0.5);
    col *= ins * (1.0 - r * 0.5);  // Center bright, edges dark
}
```

### Pattern 3: Starfield

```glsl
if (id == 2) {
    float speed = mix(0.1, 1.0, dynUni(dyn.x));
    float density = mix(10.0, 50.0, dynUni(dyn.y));
    float t = st * speed;
    
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    
    // Layer parallax stars
    vec3 star_col = vec3(0.0);
    for (float layer = 1.0; layer <= 3.0; layer += 1.0) {
        vec2 layer_p = p * density / layer + t * layer * 0.1;
        vec2 cell = floor(layer_p);
        vec2 frac_p = fract(layer_p) - 0.5;
        
        float star = hash(cell);
        if (star > 0.95) {
            float brightness = 1.0 - length(frac_p) * 10.0;
            brightness = max(0.0, brightness);
            star_col += vec3(brightness) * palette(star) / layer;
        }
    }
    
    col = star_col * ins;
}
```

### Pattern 4: Rasterbars

```glsl
if (id == 36) {
    float scroll_speed = mix(0.5, 2.5, dynUni(dyn.x));
    float bar_freq = mix(8.0, 24.0, dynUni(dyn.y));
    float wave_amp = mix(0.0, 0.3, dynUni(dyn.z));
    float bar_width = mix(0.5, 3.0, dynUni(dyn.w));
    
    float t = st * scroll_speed;
    
    // Horizontal sine wave displacement
    float wave = sin(uv.y * 15.0 + t * 2.0) * wave_amp;
    
    // Vertical bars with motion
    float bar = sin((uv.y + t) * bar_freq);
    bar = smoothstep(0.0, bar_width * 0.1, abs(bar));
    bar = 1.0 - bar;
    
    // Color cycle
    float hue = fract(uv.y * 3.0 + t * 0.5);
    col = palette(hue) * bar * ins * vignette(uv);
}
```

---

## Curve Integration

### Curve-Driven Parameters

**Editor workflow**:
1. Open "Shader Curves" modal
2. Select target: `shader_id:42` (your shader)
3. Select parameter: `dyn0`, `speed`, `intensity`, etc.
4. Add curve points, adjust easing
5. Export → Build → Test

**Runtime evaluation**:
- Curves are evaluated per-frame based on global time
- Results are passed to shader as uniforms (`u_dyn`, `u_speed`, etc.)
- Shader sees final value, no interpolation needed

**Conversion pattern** (0.0-centered to range):
```glsl
// dyn.x is in [-1, 1] from curve (neutral = 0.0)
float speed = mix(0.5, 3.0, (dyn.x + 1.0) * 0.5);  // Map to [0.5, 3.0]
```

---

## Advanced Techniques

### Multi-Pass Composition

**Layer stacking** (via shader_pipeline in editor):
1. Scene 1: Background plasma (layer_role=background, opacity=1.0)
2. Scene 2: Tunnel overlay (layer_role=overlay, opacity=0.7, blend=additive)
3. Scene 3: Text glow (layer_role=overlay, opacity=0.5, blend=screen)

**Runtime** blends layers in order (lower layer_order draws first).

### Reaction to Music

**Use `dyn0`-`dyn3` for reactive animation**:
- Editor curves can be authored to match music beats
- Example: `dyn0` pulses on kick drum (curve: 0.0 → 1.0 → 0.0 over beat)

```glsl
float bass_impact = max(0.0, dyn.x);  // Only positive values
float scale = 1.0 + bass_impact * 0.5;  // Scale up on beat
p *= scale;
```

### Shader Forking with Duplicator

**Duplicate existing shader**:
```powershell
python tools/shader_scene_duplicator.py \
    --source 0_plasma.glsl \
    --name plasma_aggressive \
    --speed 2.5 \
    --intensity 1.8 \
    --palette_low 0.8 0.1 0.1 \
    --palette_mid 1.0 0.5 0.3 \
    --palette_high 1.0 0.9 0.2
```

**Result**: New file `39_plasma_aggressive.glsl` with overrides.

---

## Debugging Shaders

### Runtime Diagnostics

```powershell
# Build with diagnostics
cmake -S . -B build_diagnostics -DREV_DIAGNOSTICS=ON
cmake --build build_diagnostics --config Release
.\build_diagnostics\Release\intro.exe
```

**Logs**: `build_diagnostics/runtime_startup.log`

### Shader Compilation Errors

**CMake output** shows GLSL errors:
```
ERROR: 0:123: 'foo' : undeclared identifier
ERROR: 0:124: 'vec4' : syntax error
```

**Common errors**:
- Missing semicolon
- Undeclared variable
- Type mismatch (vec3 vs. float)
- Wrong uniform name (check `shader_common.glsl`)

### Visual Debugging

**Color-code parts of your shader**:
```glsl
// Debug: Show UV coordinates as color
col = vec3(uv, 0.0);  // Red=X, Green=Y

// Debug: Show dyn values
col = vec3((dyn.x + 1.0) * 0.5, (dyn.y + 1.0) * 0.5, 0.0);

// Debug: Show time
col = vec3(fract(t * 0.1));  // Cycles every 10 seconds
```

---

## Optimization Tips

### Minimize Branching

**Bad**:
```glsl
if (some_condition) {
    col = expensive_calculation();
} else {
    col = another_expensive_calculation();
}
```

**Good**:
```glsl
float factor = step(0.5, some_value);  // 0 or 1
col = mix(calculation_a(), calculation_b(), factor);
```

### Reduce Texture Lookups

**Texture lookups are expensive**:
- Reuse sampled values when possible
- Batch lookups (sample once, use multiple times)

### Use Built-in Functions

**Prefer** `smoothstep`, `mix`, `clamp` over manual math.

**Example**:
```glsl
// Bad
float fade = min(1.0, max(0.0, (value - 0.2) / 0.6));

// Good
float fade = smoothstep(0.2, 0.8, value);
```

---

## Shader Scene Checklist

Before adding a new shader:

- [ ] **Unique ID**: Check with `python tools/shader_id_finder.py --next`
- [ ] **Filename**: `<id>_<name>.glsl` matches `@shader_id`
- [ ] **Metadata**: `@shader_id`, `@name`, `@editor_dyn` comments present
- [ ] **Dispatch**: `if (id == <your_id>) { ... }` in shader code
- [ ] **Uniforms**: Only use declared uniforms (no `u_custom_param`)
- [ ] **Helpers**: Use functions from `shader_common.glsl`
- [ ] **Curves**: Map dyn0-dyn3 to meaningful controls
- [ ] **Intensity**: Multiply final color by `ins`
- [ ] **Vignette**: Apply `vignette(uv)` for polish
- [ ] **Preset**: Add entry to `assets/shader/presets.json`
- [ ] **Test**: Build and run with "Do It All"

---

## Example: Complete Shader Scene

**File**: `assets/shader/scenes/99_example_complete.glsl`

```glsl
// @shader_id 99
// @name example_complete
// @editor_dyn dyn0 | Rotation speed.
// @editor_dyn dyn1 | Scale pulsation amount.
// @editor_dyn dyn2 | Color shift rate.
// @editor_dyn dyn3 | Noise intensity.

if (id == 99) {
    // Map dynamics to ranges
    float rot_speed = mix(0.0, 3.0, (dyn.x + 1.0) * 0.5);
    float scale_amount = mix(0.8, 1.5, (dyn.y + 1.0) * 0.5);
    float color_rate = mix(0.5, 2.0, (dyn.z + 1.0) * 0.5);
    float noise_ins = max(0.0, dyn.w);  // Only positive
    
    // Time with speed
    float t = st * u_speed;
    
    // Centered coordinates
    vec2 p = (uv * 2.0 - 1.0);
    p.x *= u_resolution.x / u_resolution.y;
    
    // Rotation
    float angle = t * rot_speed;
    float c = cos(angle);
    float s = sin(angle);
    p = mat2(c, -s, s, c) * p;
    
    // Scale pulsation
    float scale = scale_amount + sin(t * 2.0) * 0.2;
    p *= scale;
    
    // Pattern
    float pattern = sin(p.x * 10.0) * sin(p.y * 10.0);
    pattern += sin(length(p) * 5.0 + t);
    pattern *= 0.5;
    
    // Noise overlay
    float n = noise(p * 5.0 + t);
    pattern = mix(pattern, n, noise_ins * 0.3);
    
    // Color mapping
    vec3 base_col = palette(pattern * color_rate + t * 0.1);
    
    // Apply intensity and vignette
    col = base_col * ins * vignette(uv);
    
    // Optional: Add glow at center
    float glow = 1.0 / (length(p) + 1.0);
    col += u_palette_high * glow * 0.2;
}
```

**Preset** (`assets/shader/presets.json`):
```json
{
  "example_complete_default": {
    "shader_scene_id": 99,
    "speed": 1.0,
    "intensity": 1.2,
    "warp": 0.5,
    "palette_low": [0.1, 0.2, 0.5],
    "palette_mid": [0.5, 0.3, 0.8],
    "palette_high": [0.9, 0.7, 1.0]
  }
}
```

---

## Next Steps

- **[Curve System Guide](CURVE_SYSTEM_GUIDE.md)** - Animate shader parameters
- **[Editor Guide](EDITOR_GUIDE.md)** - Use shader scenes in timeline
- **[Tools Reference](../tools/SHADER_TOOLS.md)** - Duplicator and ID finder
- **[API Reference](../architecture/API_REFERENCE.md)** - Runtime shader API

---

**Last Updated**: May 30, 2026  
**Version**: 1.0
