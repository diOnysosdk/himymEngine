# Shader System - Modular Architecture

The shader system uses a centralized registry in `shader_presets.cpp` to manage all available shaders. This makes it easy to add new shaders without modifying multiple files.

## Architecture

**Single Source of Truth:**
- **`shader_presets.h/cpp`** - Central registry of all shaders with embedded GLSL code
- **`editor_modals.cpp`** - Shader selection UI (automatically reads from registry)
- **`minimal_intro/main.cpp`** - Runtime can use `GetShaderSourceById()` to fetch shaders

## How to Add a New Shader

Adding a new shader requires editing only **one file**: `shader_presets.cpp`

### Step 1: Add Entry to `g_shader_presets` Array

Open `revision_libs/rev_editor/src/shader_presets.cpp` and add a new entry:

```cpp
const ShaderPreset g_shader_presets[] = {
    // ... existing shaders ...
    
    {
        10,  // Unique ID (increment from last shader)
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

### Step 2: Rebuild

```powershell
cmake --build build --config Release -j 8
```

That's it! The new shader will automatically appear in:
- Editor's shader selection dropdown
- Runtime's shader picker (if using `GetShaderSourceById()`)

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
    10,
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

## Benefits of This Architecture

✅ **Single Point of Change** - Add shaders in one place  
✅ **Type Safety** - Shader structure enforced at compile time  
✅ **Editor Sync** - UI automatically reflects available shaders  
✅ **Runtime Access** - `GetShaderSourceById()` for dynamic loading  
✅ **Easy Testing** - Modify shaders and rebuild instantly  

## Future Enhancements

Possible improvements to consider:
- Hot-reload shader source files from disk
- Shader validation on load
- Shader categories/tags for organization
- Custom uniform parameters per shader
- Shader thumbnails/previews in editor
