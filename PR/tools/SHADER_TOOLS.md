# Shader Workflow Tools — Quick Reference

## Overview

The shader system now has streamlined workflows for creating, duplicating, and managing shader scenes.

---

## Tools Available

### 1. **shader_scene_duplicator.py** — Fork Existing Shaders
Duplicate a shader with new ID, name, and optionally tweaked parameters.

**Examples:**
```powershell
# Duplicate with auto-assigned ID
python tools/shader_scene_duplicator.py --source 0 --name nebula_soft --auto-id

# Duplicate with specific ID
python tools/shader_scene_duplicator.py --source 23 --id 36 --name neon_web_fast

# Duplicate with tweaked preset
python tools/shader_scene_duplicator.py --source 0 --name my_nebula --auto-id \
    --speed 1.5 --intensity 1.2 --warp 0.7

# Duplicate without preset update
python tools/shader_scene_duplicator.py --source 0 --name test_shader --auto-id --no-preset

# Reset dyn labels to generic placeholders
python tools/shader_scene_duplicator.py --source 0 --name base_shader --auto-id --reset-dyn-labels
```

**What it does:**
- Copies source `.glsl` file to new numbered file
- Updates `@shader_id`, `@name`, and `else if (id == N)` condition
- Optionally keeps or resets `@editor_dyn` labels
- Auto-finds next available ID or uses specified ID
- Extracts preset from source and creates new row with overrides
- Adds comment mapping to `presets.txt`

---

### 2. **shader_id_finder.py** — Manage Shader IDs
Find, list, validate shader scene IDs.

**Examples:**
```powershell
# Get next available ID
python tools/shader_id_finder.py --next
# Output: 35

# List all used IDs
python tools/shader_id_finder.py --list
# Output: Used shader IDs: 0, 1, 2, ..., 34

# Show detailed ID → name map
python tools/shader_id_finder.py --map

# Check if ID is available
python tools/shader_id_finder.py --check 99
# Output: ID 99 is AVAILABLE

# Find gaps in sequence
python tools/shader_id_finder.py --gaps
# Output: No gaps: sequential range [0, 34]
```

---

### 3. **shader_scene_importer.py** — Import External GLSL
Import Shadertoy shaders or raw GLSL into scene branch format.

**Examples:**
```powershell
# Import Shadertoy mainImage() shader
python tools/shader_scene_importer.py --in shader.glsl --id 35 --name imported \
    --mode mainimage

# Import raw body code
python tools/shader_scene_importer.py --in shader.glsl --id 35 --name imported \
    --mode body

# Use GUI version
python tools/shader_scene_importer_ui.py
```

**Supported modes:**
- `auto` — Auto-detect format
- `body` — Raw GLSL body (no main function)
- `mainimage` — Shadertoy-style `void mainImage(out vec4, in vec2)`
- `branch` — Already in `else if (id == N)` format

---

### 4. **VSCode Snippets** — Code Templates
Type prefix and press Tab to expand.

**Available snippets:**
- `shader_scene` — Full scene branch boilerplate
- `shader_fbm_warp` — FBM domain warping pattern
- `shader_voronoi` — Voronoi/Worley noise
- `shader_polar` — Polar coordinate transform
- `shader_ridged` — Ridged noise for mountains
- `shader_texture` — Texture sampling with transform
- `shader_dyn_muls` — Speed/detail multipliers from dyn
- `shader_rotate` — 2D rotation matrix
- `shader_glow` — Smooth edge glow
- `shader_animate` — Time-based UV animation
- `shader_contrast` — Contrast/remap helper

**How to use:**
1. Open `.glsl` file in VSCode
2. Type prefix (e.g., `shader_scene`)
3. Press Tab
4. Fill in placeholders

---

## Workflows

### Create Brand New Shader

**Option 1: Use Template**
```powershell
# Copy template
cp assets/shader/templates/scene_template.glsl assets/shader/scenes/35_my_shader.glsl

# Edit metadata and code
# Update @shader_id, @name, scene logic

# Add preset row
# Edit assets/shader/presets.txt manually
```

**Option 2: Use Snippets**
1. Create file: `assets/shader/scenes/35_my_shader.glsl`
2. Type `shader_scene` and Tab
3. Fill in ID, name, dyn labels, scene logic
4. Add preset row manually

**Option 3: Import from Shadertoy**
```powershell
# Save Shadertoy code to shader.glsl
python tools/shader_scene_importer.py --in shader.glsl --id 35 --name my_shader \
    --mode mainimage
```

---

### Duplicate & Modify Existing Shader

**Fast variation workflow:**
```powershell
# Find next ID
python tools/shader_id_finder.py --next
# Output: 35

# Duplicate with tweaks
python tools/shader_scene_duplicator.py --source 0 --name nebula_fast --auto-id \
    --speed 1.5 --warp 0.8

# Edit duplicated file
code assets/shader/scenes/35_nebula_fast.glsl

# Rebuild
cmake -S . -B build
cmake --build build --config Release
```

---

### Create Multiple Variations (Preset Aliases)

For variations that only differ in parameters, reuse same shader ID:

```powershell
# Duplicate shader
python tools/shader_scene_duplicator.py --source 0 --id 35 --name nebula_base

# Manually add preset variations to presets.txt:
nebula_soft  | 35 | 0.1 0.3 0.8 | 0.6 0.4 0.8 | 0.8 | 0.9 | 0.3 | ...
nebula_fast  | 35 | 0.1 0.3 0.8 | 0.8 0.2 0.6 | 1.5 | 1.2 | 0.7 | ...
nebula_warm  | 35 | 0.4 0.2 0.1 | 1.0 0.6 0.3 | 1.0 | 1.0 | 0.5 | ...
```

Editor will show all three as separate presets pointing to same shader code.

---

### Check Before Creating

```powershell
# List all existing shaders
python tools/shader_id_finder.py --map

# Check if name/ID would collide
python tools/shader_id_finder.py --check 35
```

---

## File Structure

```
assets/shader/
├── shader_common.glsl          # Uniforms, helpers, renderScene() dispatch
├── shader_footer.glsl          # main() composition
├── presets.txt                 # Editor preset library
├── scenes/
│   ├── 00_nebula_drift.glsl
│   ├── 01_ribbon_aurora.glsl
│   ├── ...
│   └── 35_my_new_shader.glsl   # Your new shader
└── templates/
    └── scene_template.glsl     # Manual creation template
```

**Build process:**
- CMake concatenates: `shader_common.glsl` + all `scenes/*.glsl` (sorted) + `shader_footer.glsl`
- Output: `assets/shader/fragment.glsl` (generated)
- Embedded into runtime

---

## Scene Branch Format

```glsl
// @shader_id 35
// @name my_shader
// @editor_dyn dyn0 | Speed control.
// @editor_dyn dyn1 | Motion amount.
// @editor_dyn dyn2 | Warp/detail.
// @editor_dyn dyn3 | Energy/glow.
else if (id == 35) {
    float d0 = dynBiDZ(dyn.x, 0.06);
    float d1 = dynBiDZ(dyn.y, 0.08);
    float d2 = dynBiDZ(dyn.z, 0.06);
    float d3 = dynBiDZ(dyn.w, 0.06);
    
    // Your shader logic here
    // Available inputs:
    //   uv       — [0,1] texture coordinates
    //   st       — CPU-integrated scene time
    //   t        — Wall-clock time
    //   c_low, c_mid, c_high — palette colors
    //   spd, ins, wrp — speed, intensity, warp
    //   dyn      — vec4(dyn0, dyn1, dyn2, dyn3)
    
    col = mix(c_low, c_high, /* your result */);
}
```

---

## Preset Format

```
label | id | palette_low(r g b) | palette_high(r g b) | speed | intensity | warp | exposure_base | exposure_ramp | fade_base | fade_ramp
```

**Example:**
```
my_shader | 35 | 0.10 0.08 0.05 | 0.95 0.70 0.30 | 1.00 | 1.00 | 0.60 | 0.80 | 0.01 | 0.00 | 0.00
```

---

## Common Patterns

### FBM Domain Warp
```glsl
vec2 q = vec2(fbm(uv * 2.0 + st * 0.07), fbm(uv.yx * 2.3 - st * 0.05));
float n = fbm(uv * 3.0 + q * 0.5 + st * 0.1);
col = mix(c_low, c_high, n);
```

### Voronoi Cells
```glsl
vec2 gv = uv * 8.0;
vec2 id = floor(gv);
vec2 f = fract(gv);
float min_d = 10.0;
for (int j = -1; j <= 1; ++j) {
    for (int i = -1; i <= 1; ++i) {
        vec2 o = vec2(float(i), float(j));
        vec2 pnt = o + vec2(hash(id + o + vec2(3.1, 7.7)), hash(id + o + vec2(11.3, 2.9))) - 0.5;
        min_d = min(min_d, length(f - pnt));
    }
}
col = mix(c_low, c_high, smoothstep(0.3, 0.0, min_d));
```

### Polar Coordinates
```glsl
vec2 p = uv - 0.5;
float r = length(p);
float a = atan(p.y, p.x);
float pattern = sin(a * 8.0 + st) * cos(r * 10.0 - st * 2.0);
col = mix(c_low, c_high, pattern * 0.5 + 0.5);
```

### Ridged Noise
```glsl
float n = fbm(uv * 4.0 + st * 0.1);
float ridged = 1.0 - abs(2.0 * n - 1.0);
ridged = pow(ridged, 2.0);
col = mix(c_low, c_high, ridged);
```

---

## Testing Workflow

```powershell
# 1. Create or duplicate shader
python tools/shader_scene_duplicator.py --source 0 --name test_shader --auto-id

# 2. Edit shader logic
code assets/shader/scenes/35_test_shader.glsl

# 3. Rebuild
cmake -S . -B build
cmake --build build --config Release

# 4. Test in editor
python main.py
# Open scene_block_editor.py → Shader dropdown → Select your shader
```

**Diagnostics mode (faster iteration):**
```powershell
# Build with diagnostics
cmake -S . -B build_diagnostics -DREV_FORCE_DIAGNOSTICS=ON
cmake --build build_diagnostics --config Release

# Edit generated fragment.glsl directly for quick tests
code build_diagnostics/assets/shader/fragment.glsl

# Runtime will reload from filesystem on restart
```

---

## Tips

- **Keep dyn neutral at 0.5**: `dynBiDZ(dyn.x, deadzone)` maps 0.5 → 0.0
- **Use CPU-integrated `st`**: Avoids jitter from `time * speed * dyn_factor`
- **Test with different palettes**: Editor lets you preview color variations
- **Start from similar shader**: Duplicating is faster than creating from scratch
- **Use snippets**: `shader_fbm_warp`, `shader_voronoi`, etc. for common patterns
- **Check ID availability first**: `shader_id_finder.py --check ID`

---

## Troubleshooting

**"Shader ID N is already in use"**
- Run `shader_id_finder.py --map` to see all IDs
- Use `--auto-id` or choose different ID

**"No preset found for source ID N"**
- Duplicator will still create shader file
- Add preset row manually to `presets.txt`

**"Shader not appearing in editor"**
- Ensure `@shader_id` comment is present
- Rebuild to regenerate `fragment.glsl`
- Check `presets.txt` has matching row

**"Shader compiles but looks wrong"**
- Check `else if (id == N)` matches `@shader_id`
- Verify `col` assignment at end of branch
- Test with different palettes in editor

---

## See Also

- `docs/SHADER_WORKFLOW_IMPROVEMENTS.md` — Full improvement plan
- `docs/SHADER_SYSTEM_PHASE_1.md` — Pass graph architecture
- `OPENGL-EXPLAINER.md` — Shader pipeline details
- `assets/shader/templates/scene_template.glsl` — Manual template
