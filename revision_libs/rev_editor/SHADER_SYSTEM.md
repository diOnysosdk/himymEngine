# Shader System - Modular Architecture

For the user-facing authoring workflow and GLSL examples, see
[`PR/guides/SHADER_GUIDE.md`](../../PR/guides/SHADER_GUIDE.md).

The shader system uses a single authoritative shader registry in the editor presets, and runtime compiles from that same source.

## Architecture

**Single Shader Registry:**
- **`revision_libs/rev_editor/src/shader_presets.cpp`** - authoritative GLSL source + IDs
- **`editor_modals.cpp`** - shader selection dropdown (reads from presets)
- **`examples/minimal_intro/main.cpp`** - runtime compiles via `GetShaderSourceById()` and `g_shader_preset_count`

The previous duplicated runtime `fragment_shaders[]` array has been removed to prevent drift.

## Shadertoy Compatibility

The pipeline pass UI supports both file import and direct authoring. **New /
Paste GLSL** opens a large multiline editor with a GLSL 330 `mainImage`
template; **Edit GLSL** loads the currently assigned source. Saves write to
`project_assets`, update the pass's portable source path, and invalidate the
editor preview cache so same-path edits compile immediately.

Channel authoring is type-aware: textures expose load/path/clear controls,
Buffer A-D expose source actions for the referenced buffer pass, self-feedback
exposes source actions for the current pass, and audio/none omit irrelevant
file controls. Same-name GLSL and texture reloads invalidate the preview cache.

`rev_shader` accepts fragment sources that define Shadertoy's
`mainImage(out vec4, in vec2)` entry point without a `main()` function. It
normalizes missing or different version directives to `#version 330 core` in
memory, then injects the GLSL 330 declarations and a normalized-UV bridge for:

- `iResolution`, `iTime`, `iTimeDelta`, `iFrame`, and `iMouse`
- `iChannel0` through `iChannel3`

The four channels currently alias the shader cue's four optional noise-map
textures. Editor preview uses deterministic 60 Hz frame values and a zero mouse
until preview-relative pointer input is authored. Standalone playback supplies
measured frame delta and window-relative mouse coordinates.

Preset 47, **Shadertoy Neon Lattice**, is the single-pass compatibility
reference shader. Project pipelines extend that contract with Buffer A-D,
texture inputs, explicit previous-frame feedback, and live audio input.

The shared runtime contract now reserves project-level pipelines containing an
Image pass and Buffer A-D. Each pass has four typed channels, an authored GLSL
source path, and a resolution scale in `(0, 1]`. Channel types cover textures,
Buffer A-D outputs, explicit previous-frame self-feedback, and an audio-spectrum
source. `BuildShaderPassOrder` validates missing inputs, disabled dependencies,
direct self-dependencies, and cycles while producing deterministic buffer-first,
Image-last execution order. Project JSON persists the complete pipeline registry
and shader cues persist a `shader_pipeline_index` reference; `-1` keeps the
existing preset-only behavior. Flat export uses separate
`[shader_pipeline_cues]`, `[shader_pipelines]`, `[shader_pipeline_passes]`, and
`[shader_pipeline_channels]` sections so legacy shader-row offsets remain
unchanged. The packer discovers enabled GLSL pass sources and texture-channel
assets from those sections. The Shader Parameters modal can create and assign
pipelines, edit Image/Buffer A-D sources and resolution scales, configure all
four channels, and reports graph validation errors live. GLSL sources and
texture channels have browse controls that copy selected files into the project
assets directory and store portable `project_assets/...` paths.

Editor preview, standalone playback, and packed playback compile enabled pass
sources, allocate two scaled RGBA targets per pass, bind texture/current-buffer/
previous-frame channels, execute the validated order, swap feedback targets,
and composite the Image pass through the existing shader-layer blend path. The
editor invalidates cached pass resources when the authored graph or preview
resolution changes. `Audio Spectrum` binds a Shadertoy-compatible 512x2 R8
texture built from the active XM output: spectrum magnitudes occupy row 0 and
the normalized waveform occupies row 1. With no active audio both rows receive
the deterministic silent representation. A missing or invalid pipeline
continues through its preset fallback.

## OpenGL Runtime Requirement

The platform requests an OpenGL 3.3 core context and now verifies that the
active context is at least version 3.3 before returning a window. A legacy WGL
fallback is accepted only when the driver still supplies OpenGL 3.3 or newer.
Standalone verbose logging reports the GL version, GLSL version, vendor, and
renderer. Required shader and baseline rendering entry points are validated
before use so an unsupported driver fails at startup instead of crashing later.

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
- `vec3 u_position` - Virtual texture-space position
- `vec3 u_rotation` - Virtual texture-space Euler rotation
- `vec3 u_motion` - Texture-space velocity multiplied by shader time

### Procedural Noise Compatibility

The current noise system is procedural GLSL noise, not sampler-backed image textures. A shader opts in by declaring the `u_noise_*` uniforms and applying the resulting field to its coordinates or material values. The five presets below now evaluate three independently seeded noise layers, so one cue can produce broad, medium, and fine detail from the same settings:

- **11 Spiral Galaxy** - nebula, dust, and spiral distortion
- **14 Liquid Chrome** - fluid coordinates and metallic reflection bands
- **16 Fire Shader** - flame turbulence and rising distortion
- **17 Cosmic Nebula Volumetric** - volumetric ray coordinates and cloud density
- **22 Warped Cathedral** - domain warp, arches, and light-ray detail

Other presets are viable candidates but are not opted in yet:

- **Easy 2D candidates:** 2 Tunnel Neon, 4 Fractal Mandelbrot, 5 Voronoi Cells, 6 Wave Distortion, 9 Glow Orbs, 10 Matrix Rain, 12 Wormhole, 13 DNA Helix, 15 Kaleidoscope, 18 Star Scroller 360, 19 Kaleido Fracture, 20 Signal Riot, 21 Electric Vortex.
- **3D or specialized candidates:** 3 Raymarcher SDF, 7 Particle System, 8 Starfield, 23 3D Checkerboard, 24 3D Fog Volume. These need field placement specific to their geometry or ray parameterization.
- **Already noise-driven but intentionally unchanged:** 1 Plasma Vibrant and 0 Horizontal Gradient Bands already consume the shared noise controls; both now also use `u_motion` for noise travel.

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

## Shader Registry

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
12-17. **Additional procedural scenes** - Volumetric, animated, and cinematic variations
18. **Star Scroller 360** - Directional starfield scroller with warp-controlled heading
19. **Kaleido Fracture** - Recursive mirrored shards with chromatic separation
20. **Signal Riot** - Scanline interference, block tearing, and RGB misalignment
21. **Electric Vortex** - Coiling lightning filaments around a breathing singularity
22. **Warped Cathedral** - Domain-warped arches, rays, and recursive fBm detail
23. **3D Checkerboard** - Perspective checkerboard with virtual XYZ placement, Euler rotation, and texture travel
24. **3D Fog Volume** - Procedural volumetric fog with virtual XYZ placement, Euler rotation, and directional travel
25. **Cloud Flight** - Layered volumetric cloud flight with broad, detail, and wispy procedural fields
26. **Noise Triplanar Marble** - Three-axis procedural noise projection with marble veins
27. **Noise Erosion Mask** - Layered ridges, erosion, and sediment bands
28. **Noise Reaction Field** - Animated cellular field built from offset noise layers
29. **Noise Flow Map** - Iterated gradient-driven flow ribbons
30. **Noise Terrain Relief** - Height-field relief with procedural lighting

The registry is authoritative; inspect `g_shader_presets` for the current IDs
rather than relying on a duplicated count in this document.

### Noise Texture Slots

Shader cues support **four optional sampler-backed noise textures**. Their paths are stored in `noise_map_0` through `noise_map_3` and are resolved relative to the project assets directory (with the existing runtime asset fallbacks). The editor and runtime load them through the shared image loader and bind them to texture units 4 through 7 as `u_noise_map_0` through `u_noise_map_3`.

The procedural `u_noise_*` controls remain independent and continue to work when no maps are assigned. The new cloud and specialized noise presets blend assigned maps into their procedural fields; with zero assigned maps they retain their procedural-only behavior. Empty slots are represented by `-` in exported `cues.txt` rows, and older rows without the four trailing fields remain compatible.

### Virtual 3D Shader Controls

Shader cues remain fullscreen compositing passes, but presets 23 and 24 evaluate a virtual 3D coordinate space. Their controls are:

- **Position XYZ** - translates the procedural plane or volume
- **Rotation XYZ** - Euler rotation in degrees
- **Motion XYZ** - texture-space velocity, multiplied by time and shader speed

Older project JSON and cues exports default these optional fields to zero.

## Benefits of This Architecture

✅ **Structured Registry** - Shader metadata and code in one place  
✅ **Type Safety** - Shader structure enforced at compile time  
✅ **Editor Sync** - UI automatically reflects available shaders  
✅ **Compile-Time Validation** - Syntax errors caught during build  
✅ **Easy Testing** - Modify shaders and rebuild instantly

## Limitations

⚠️ **ID Conflicts** - If IDs don't match, runtime displays wrong shader  
⚠️ **Rebuild Required** - Changes require full recompilation  

## Future Enhancements

Possible improvements to consider:
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
