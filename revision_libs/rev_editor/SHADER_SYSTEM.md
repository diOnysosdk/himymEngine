# Shader System - Modular Architecture

The shader system uses a single authoritative shader registry in the editor presets, and runtime compiles from that same source.

## Architecture

**Single Shader Registry:**
- **`revision_libs/rev_editor/src/shader_presets.cpp`** - authoritative GLSL source + IDs
- **`editor_modals.cpp`** - shader selection dropdown (reads from presets)
- **`examples/minimal_intro/main.cpp`** - runtime compiles via `GetShaderSourceById()` and `g_shader_preset_count`

The previous duplicated runtime `fragment_shaders[]` array has been removed to prevent drift.

## How to Add a New Shader

Adding a new shader requires editing **ONE file**.

### Step 1: Add Entry to Editor Registry (`shader_presets.cpp`)

Open `revision_libs/rev_editor/src/shader_presets.cpp` and add a new entry to the `g_shader_presets[]` array:

```cpp
const ShaderPreset g_shader_presets[] = {
    // ... existing shaders ...
    
    {
        11,  // Unique ID (current range is 0-10, so next is 11)
        "Your Shader Name",
        "Brief description of what the shader does",
        R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;
uniform float u_intensity;
uniform float u_warp;

void main() {
    // Your shader code here
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    // Example: simple color cycling
    vec3 col = mix(u_palette_low, u_palette_high, sin(t + length(p)) * 0.5 + 0.5);
    
    fragColor = vec4(col, 1.0);
}
)"
    }
};
```

### Step 2: Rebuild Editor and Runtime

```powershell
cmake --build build --config Release -j 8
```

The new shader will now appear in:
- ✅ Editor's shader selection dropdown
- ✅ Runtime playback (auto-sourced from the same preset entry)

## Available Uniforms

Your shader fragment code has access to these uniforms:

- `float u_time` - Time in seconds since start
- `vec2 u_resolution` - Viewport resolution (width, height)
- `vec3 u_palette_low` - User-configurable color (dark tones)
- `vec3 u_palette_mid` - User-configurable color (mid tones)
- `vec3 u_palette_high` - User-configurable color (bright tones)
- `float u_speed` - Animation speed multiplier (0.1 - 5.0)
- `float u_intensity` - Effect intensity (0.0 - 2.0)
- `float u_warp` - Distortion/warping amount (0.0 - 1.0)
- `float u_exposure_base` - Exposure control (0.0 - 2.0)
- `float u_fade_base` - Opacity/fade (0.0 - 1.0)

## Shader Code Guidelines

1. **Version**: Use `#version 330 core` for OpenGL 3.3 compatibility
2. **Inputs**: Always declare `in vec2 uv;` (0.0-1.0 normalized coordinates)
3. **Output**: Always declare `out vec4 fragColor;`
4. **Aspect Ratio**: Convert UV to aspect-correct coordinates:
   ```glsl
   vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
   ```
5. **Performance**: Keep complexity reasonable - target 60 FPS on mid-range GPUs
6. **Uniforms**: Use the provided uniforms for user control and consistency

## Example: Adding a "Spiral" Shader

```cpp
{
    11,
    "Spiral Vortex",
    "Rotating spiral pattern with smooth color transitions",
    R"(
#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform float u_time;
uniform vec2 u_resolution;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;
uniform float u_intensity;

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(u_resolution.x / u_resolution.y, 1.0);
    float t = u_time * u_speed;
    
    float r = length(p);
    float a = atan(p.y, p.x);
    
    // Create spiral pattern
    float spiral = sin(a * 5.0 + r * 10.0 - t * 2.0) * 0.5 + 0.5;
    spiral = pow(spiral, 2.0 - u_intensity);
    
    // Apply color palette
    vec3 col = mix(mix(u_palette_low, u_palette_mid, spiral), 
                   u_palette_high, smoothstep(0.6, 1.0, spiral));
    
    // Vignette
    col *= smoothstep(1.5, 0.3, r);
    
    fragColor = vec4(col, 1.0);
}
)"
}
```

## Current Shader List (IDs 0-11)

0. **Horizontal Gradient Bands** - Black default with three horizontal fade bands
1. **Plasma Vibrant** - Classic plasma effect
2. **Tunnel Neon** - 3D tunnel with rings
3. **Raymarcher SDF** - Signed distance field raymarching
4. **Fractal Mandelbrot** - Mandelbrot fractal zoom
5. **Voronoi Cells** - Cellular voronoi patterns
6. **Wave Distortion** - Sine wave distortion field
7. **Particle System** - GPU particle simulation
8. **Starfield** - 3D starfield with motion blur
9. **Glow Orbs** - Glowing orb metaballs
10. **Matrix Rain** - Matrix-style digital rain
11. **Spiral Galaxy** - Rotating nebula with stars, spiral arms, multi-layer FBM noise

**Next available ID: 12**

## Benefits of This Architecture

✅ **Structured Registry** - Shader metadata and code in one place  
✅ **Type Safety** - Shader structure enforced at compile time  
✅ **Editor Sync** - UI automatically reflects available shaders  
✅ **Compile-Time Validation** - Syntax errors caught during build  
✅ **Easy Testing** - Modify shaders and rebuild instantly

## Limitations

⚠️ **Manual Synchronization Required** - Editor and runtime registries must be updated separately  
⚠️ **ID Conflicts** - If IDs don't match, runtime displays wrong shader  
⚠️ **Rebuild Required** - Changes require full recompilation  

## Future Enhancements

Possible improvements to consider:
- **Unified shader registry** - Single source of truth shared by editor and runtime
- **Runtime uses `GetShaderSourceById()`** - Eliminate duplicate `fragment_shaders[]` array
- Hot-reload shader source files from disk
- Shader validation on load
- Shader categories/tags for organization
- Custom uniform parameters per shader
- Shader thumbnails/previews in editor

## Troubleshooting

**Problem**: Runtime displays the wrong shader  
**Cause**: Invalid `shader_scene_id` in cue data or missing preset ID  
**Solution**: Verify `shader_scene_id` exists in `g_shader_presets[]` and cues reference valid IDs

**Problem**: New shader appears in editor but not runtime  
**Cause**: Build is stale or shader failed to compile at runtime  
**Solution**: Rebuild `minimal_intro`, then check runtime compile errors for that shader ID

**Problem**: Runtime always falls back to shader ID 0  
**Cause**: Invalid cue ID or hardcoded validation in downstream examples  
**Solution**: Search for hardcoded validation and replace with dynamic check:
```cpp
// ❌ BAD - Hardcoded limit
if (cue.shader_scene_id < 0 || cue.shader_scene_id > 9) {
    cue.shader_scene_id = 0;
}

// ✅ GOOD - Dynamic limit
const int num_shaders = GetRuntimeShaderCount();
if (cue.shader_scene_id < 0 || cue.shader_scene_id >= num_shaders) {
    cue.shader_scene_id = 0;
}
```
**Files to check**: `examples/minimal_intro/main.cpp`, `examples/demo_intro/main.cpp`, `examples/animated_intro/main.cpp`
