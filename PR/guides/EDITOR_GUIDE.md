# Scene Block Editor Guide

**Complete walkthrough of the Python/tkinter scene authoring tool**

## Overview

The Scene Block Editor is a monolithic Python/tkinter application (8000+ lines) for authoring intro/demo timelines. It manages:
- Scene blocks (timing, shaders, effects)
- Asset references (images, text, music, 3D meshes)
- Shader curves (animated parameters)
- Export to runtime format (`assets/cues.txt`)
- Build integration (CMake, run intro)

**Philosophy**: All complexity lives in the editor, runtime stays simple.

---

## Main UI Layout

```
┌─────────────────────────────────────────────────────────────────────┐
│ [Save] [Export] [Shader Curves] [Build] [Run] [Do It All] ...      │ Top Bar
├──────────────────┬──────────────────────────────────────────────────┤
│ Timeline         │ Properties Panel                                  │
│ ┌──────────────┐ │ • Scene/Effect settings                          │
│ │ Opening      │ │ • Shader modal (colors, values, curves)          │
│ │ ├─ Shader 1  │ │ • Image/Text cues (overlays)                     │
│ │ ├─ Image 1   │ │ • Music controls                                 │
│ │ └─ Text 1    │ │ • 3D Stage inspector (optional)                  │
│ │              │ │                                                   │
│ │ Climax       │ │ Current: "Opening" scene                          │
│ │ ├─ Shader 2  │ │ Duration: 10.0s                                   │
│ │ └─ Effect 1  │ │ Shader: plasma_chill                              │
│ │              │ │                                                   │
│ │ [+ Scene]    │ │ [Edit Shader...] [Edit Image...] [Edit Text...]  │
│ │ [+ Effect]   │ │                                                   │
│ │ [+ Cue]      │ │                                                   │
│ │ [Delete]     │ │                                                   │
│ └──────────────┘ │                                                   │
├──────────────────┼──────────────────────────────────────────────────┤
│ Asset Browser    │ 3D Layout Preview (when 3D stage enabled)         │
│ (workspace)      │                                                   │
│ ☐ tunnel_bg.png  │                                                   │
│ ☐ logo.png       │                                                   │
│ ☐ music.xm       │                                                   │
└──────────────────┴──────────────────────────────────────────────────┘
```

---

## Core Workflows

### Workflow 1: Create New Scene

1. **Click "+ Scene"** (bottom left)
2. **Name it** (e.g., "Opening")
3. **Set duration** (e.g., 10.0 seconds)
4. **Click "Edit..."** to open shader modal
5. **Choose shader preset** from dropdown
6. **Customize colors/values** or use randomize buttons
7. **Click "Apply & Close"**

**Result**: Scene block with shader timing from 0.0s to 10.0s.

### Workflow 2: Add Shader to Scene

1. **Select scene** in timeline
2. **Click shader "+ Cue"** button
3. In shader modal:
   - Pick shader preset (e.g., `plasma_vibrant`)
   - Adjust **Shader Cue Start/End** timing
   - Set **Fade In/Out** durations
   - Configure **Layer Role** (background/overlay)
4. **Apply & Close**

**Result**: Shader plays during specified time window with fades.

### Workflow 3: Animate with Curves

1. **Top bar: Click "Shader Curves"** button
2. In curve editor:
   - **Target dropdown**: Choose `(common)` or specific shader
   - **Param dropdown**: Choose parameter (e.g., `speed`, `dyn0`)
   - **Click canvas** to add points
   - **Drag points** to shape curve
   - **Adjust easing** with In/Out Ease sliders
3. **Close curve editor** (auto-saves)
4. **"Do It All"** to rebuild and test

**Result**: Shader parameter animates over time following your curve.

### Workflow 4: Add Text Overlay

1. **Select scene** in timeline
2. **Click text "+ Cue"** button
3. In text modal:
   - **Step 1**: Choose object preset (e.g., `title_main`)
   - **Step 2**: Choose FX preset (e.g., `fade_in_out`)
   - Adjust **X/Y Position** (0.5, 0.5 = center)
   - Set **Active Start/End** timing
   - Set **Effect Start/End** for animation window
4. **Apply & Close**

**Result**: Text appears with fade effect during specified timing.

### Workflow 5: Add Music

1. **Click "+ Music"** button
2. In music modal:
   - **Asset Key**: Give it a name (e.g., `main_track`)
   - **Path**: Select XM file from workspace
   - **Start/End**: Music cue timing (usually 0.0 to scene end)
3. **Apply & Close**

**Result**: XM module plays during intro, timeline syncs to music.

---

## Shader Modal Deep Dive

### Sections

**1. Shader Preset Dropdown**
- Lists all shaders with friendly names
- Shows shader_scene_id in gray text
- Changing dropdown updates all parameters

**2. Palette Colors (RGB)**
- **Low, Mid, High** color triplets
- Manual entry: `0.1 0.3 0.8` (space-separated floats)
- **Pick...** buttons open color picker
- Color swatches show live preview

**3. Randomize / Reset Buttons**
- **🎨 Randomize Colors**: Harmonious palettes (5 color theory schemes)
- **📈 Randomize Curves**: Creates 3-5 random curve points for all params
- **🎲 Randomize Values**: Randomizes speed/intensity/warp/exposure/fade/opacity
- **↺ Reset Values**: Restores defaults (prevents shader disappearing)

**4. Numeric Parameters**
- **Speed**: Shader time multiplier (1.0 = normal)
- **Intensity**: Overall effect strength (1.0 = normal)
- **Warp**: Distortion amount (0.5 = default)
- **Exposure Base/Ramp**: Brightness control (0.76 / 0.02 defaults)
- **Fade Base/Ramp**: Fade curve shaping (0.04 / -0.04 defaults)

**5. Shader Cue Timing**
- **Start (s)**: When shader begins (scene-relative)
- **End (s)**: When shader ends (or -1.0 for implicit scene end)
- **Fade In/Out (s)**: Crossfade durations
- **Timing Hint**: Shows actual runtime timing

**6. Layer Controls**
- **Role**: `background` or `overlay`
- **Opacity**: 0.0 (transparent) to 1.0 (opaque)
- **Blend Mode**: `alpha`, `additive`, `multiply`, `screen`
- **Layer Order**: Lower draws first (z-order)

**7. Shader Curves**
- Inline summary: Lists parameters with curve points
- **Shader Curves...** button opens full editor
- Parameters: `speed`, `intensity`, `warp`, `exposure`, `fade`, `dyn0`-`dyn3`

**8. Action Buttons**
- **Shader Curves...**: Open curve editor for this shader
- **Apply + Stay**: Save changes, keep modal open
- **Apply & Close**: Save and close
- **Cancel**: Discard changes

---

## Curve Editor Deep Dive

### Interface

```
┌─────────────────────────────────────────────────────────────┐
│ Target: [shader_id:0 (plasma)] ▼  Param: [speed] ▼          │
├──────────────┬──────────────────────────────────────────────┤
│ [Add Dynamic]│ Point T: [0.5]    In Ease: [0.3]             │
│ [Delete]     │ Point V: [1.2]    Out Ease: [0.3]            │
│ [Reset Target]│ Segment Mode: [smooth] ▼                    │
│              │                                               │
│ [Add Point]  │                                               │
│ [Delete Point]│                                              │
├──────────────┴───────────────────────────────────────────────┤
│                                                               │
│  1.5┤     ●                                                   │
│     │    ╱ ╲                                                  │
│  1.0┤   ╱   ╲                                                 │
│     │  ╱     ╲                                                │
│  0.5┤ ●       ●                                               │
│     │                                                         │
│  0.0├─────┬─────┬─────┬─────┬─────┬─────┬                   │
│     0.0  0.2  0.4  0.6  0.8  1.0  1.2   Time (normalized)    │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

### Operations

**Add Point**: Click canvas or "Add Point" button → sets t to click position, v to current value  
**Select Point**: Click near point (highlight changes)  
**Move Point**: Drag selected point (t and v update)  
**Delete Point**: Select point → "Delete Point" button  
**Adjust Easing**: Select point → change In/Out Ease sliders (0.0-1.0)  
**Change Mode**: Select point → choose segment mode dropdown

### Segment Modes
- **linear**: Straight line interpolation
- **ease_in**: Accelerate into point
- **ease_out**: Decelerate from point
- **ease_in_out**: Smooth S-curve
- **smoothstep**: Hermite smoothstep (0.0-1.0 range)
- **hold**: Hold previous value (step function)

### Value Range
- **Neutral**: 0.0 (no effect)
- **Standard**: -1.0 to +1.0
- **Extended**: -2.0 to +2.0 (for extreme effects)
- **Clamped**: Editor UI clamps to [-2.0, +2.0], runtime accepts any value

### Targets
- **`(common)`**: Global curves affecting all shaders
  - `exposure`, `fade`, `speed`, `intensity`, `warp`, `scene_blend`
  - `scene3d_*` parameters (if 3D enabled)
- **`shader_id:N`**: Per-shader curves
  - `speed`, `intensity`, `warp`, `exposure`, `fade` (shader-local overrides)
  - `dyn0`, `dyn1`, `dyn2`, `dyn3` (shader-specific dynamics)
  - `palette_low_r/g/b`, `palette_mid_r/g/b`, `palette_high_r/g/b`

---

## Export Pipeline

### What Happens on Export

1. **Validate project**: Check for missing assets, invalid timing
2. **Generate shader_cues**: Flatten timeline blocks into shader cue rows
3. **Generate shader_curves**: Export all curve points (t, v, ease, mode)
4. **Generate shader_pipeline**: Build shader layer order and compositing
5. **Generate image_cues**: Export image overlays with timing
6. **Generate text_cues**: Export text objects with effects
7. **Generate music_cues**: Export XM references with timing
8. **Write `assets/cues.txt`**: Single pipe-delimited text file

### cues.txt Format (excerpt)

```
[shader_cues]
shader_scene_id|palette_low_r|...|shader_cue_start_s|shader_cue_end_s|...
0|0.1|0.3|0.8|0.45|0.25|0.7|0.8|0.2|0.6|1.0|1.0|0.5|0.76|0.02|0.04|-0.04|0.0|10.0|0.5|0.5|0|1.0|0|0

[shader_curves]
target|param|point_count
shader_id:0|speed|3
0.0|0.5|0.0|0.0|linear
0.5|1.2|0.3|0.3|smooth
1.0|0.8|0.0|0.0|linear

[shader_pipeline]
order|shader_scene_id|start|end|fade_in|fade_out|implicit_end|layer_role|opacity|blend|layer_order|speed|intensity|warp
0|0|0.0|10.0|0.5|0.5|0|0|1.0|0|0|1.0|1.0|0.5
```

**Runtime** parses this at startup (no JSON parser needed).

---

## Build Integration

### "Do It All" Button

**Steps** (automated):
1. **Save**: Write project JSON to disk
2. **Export**: Generate `assets/cues.txt`
3. **Build**: Run `cmake --build build --config Release`
4. **Run**: Launch `build\Release\intro.exe`

**Result**: Full workflow in one click!

### Manual Build

```powershell
# Reconfigure (if CMakeLists.txt changed)
cmake -S . -B build

# Build only
cmake --build build --config Release

# Run
.\build\Release\intro.exe
```

### Build Diagnostics

```powershell
# Build with diagnostics (filesystem asset fallback, verbose logging)
cmake -S . -B build_diagnostics -DREV_DIAGNOSTICS=ON
cmake --build build_diagnostics --config Release
.\build_diagnostics\Release\intro.exe

# Check logs
type build_diagnostics\runtime_startup.log
```

---

## Asset Management

### Workspace Assets

**Location**: `assets/` folder (textures, music, meshes)

**Workflow**:
1. **Import**: Click "Import File..." in asset browser
2. **Select**: Pick file from disk → copies to `assets/textures/` or `assets/music/`
3. **Link**: "Use Selected Workspace Asset" button assigns to current scene

### Text Assets

**Types**:
- `title_main`: Main title text
- `credits_main`: Credits/greetings
- `scroll_text`: Horizontal scroller
- `multiline_text`: Multi-line display

**Workflow**:
1. Font settings tab: Set font, size, color
2. Text cue modal: Select text asset key, set position/timing
3. Effect preset: Choose animation style (fade, type-on, etc.)

### 3D Meshes (Optional)

**Prerequisite**: Build with `-DREV_ENABLE_3D=ON`

**Workflow**:
1. **Convert OBJ/MTL** to meshbin:
   ```powershell
   python tools/obj_to_meshbin.py assets/meshes/cube.obj
   ```
   Output: `assets/meshes/cube.meshbin`

2. **Assign to scene**:
   - Select scene → 3D Stage tab
   - **Choose .meshbin file** from dropdown
   - Adjust camera, lighting, material properties

3. **Animate with curves**:
   - Open Shader Curves editor
   - Target: `(common)`
   - Parameters: `scene3d_yaw_deg`, `scene3d_offset_x`, etc.

---

## Keyboard Shortcuts

**Timeline**:
- `Delete` - Delete selected block
- `Up/Down` - Navigate timeline
- `Enter` - Open primary modal (Edit...)

**Curve Editor**:
- `Click` - Add/select point
- `Drag` - Move point
- `Delete` - Delete selected point

**Global**:
- `Ctrl+S` - Quick save
- `F5` - Export (runtime format)
- `F6` - Build
- `F7` - Run

---

## Tips & Best Practices

### 1. Use "Do It All" Often
- Safest workflow: edit → "Do It All" → test → repeat
- Ensures export, build, and run stay synchronized

### 2. Name Scenes Meaningfully
- Bad: "Scene1", "Scene2"
- Good: "Intro", "Build-up", "Climax", "Outro"

### 3. Start with Shader Presets
- Pick preset → randomize colors → fine-tune
- Faster than manual parameter entry

### 4. Test Curves Incrementally
- Add 1-2 points → test → add more
- Easier to debug than complex curves upfront

### 5. Use Reset Values Button
- If shader disappears after randomizing → hit "↺ Reset Values"
- Returns to safe defaults (speed=1.0, intensity=1.0, etc.)

### 6. Keep Scene Duration Rounded
- Prefer 10.0s, 15.0s, 20.0s over 10.3726s
- Easier to reason about timing and cues

### 7. Export Before Building
- Editor changes live in project JSON
- Runtime reads `assets/cues.txt` (exported format)
- Always export before building!

---

## Troubleshooting

### "Shader not visible in intro"
- Check shader cue timing: Start < End, both within scene duration
- Verify opacity > 0.0
- Check layer order (background vs. overlay)
- Try "↺ Reset Values" if randomized

### "Curve changes not working"
- Ensure target matches shader (e.g., `shader_id:0` for plasma)
- Check parameter name (case-sensitive: `dyn0`, not `Dyn0`)
- Verify curve points are sorted by time (t)
- Re-export and rebuild

### "Text not rendering"
- Confirm font is installed on system
- Check text asset key matches (e.g., `title_main`)
- Verify Active Start/End timing is within scene duration
- Ensure Effect Start/End defines animation window

### "Music not playing"
- Check music cue Start > 0.0 (not negative)
- Verify XM file is in `assets/music/` folder
- Ensure music cue End > Start
- Look for XM decoder errors in `runtime_startup.log`

### "Editor crashes on startup"
- Check Python version: `python --version` (need 3.11+)
- Verify tkinter: `python -m tkinter` (should open window)
- Look for corrupt project JSON (restore from backup)

---

## Advanced Features

### Custom Shader Presets

**File**: `assets/shader/presets.json`

```json
{
  "my_preset": {
    "shader_scene_id": 0,
    "speed": 1.5,
    "intensity": 1.2,
    "warp": 0.7,
    "palette_low": [0.2, 0.1, 0.5],
    "palette_mid": [0.5, 0.3, 0.8],
    "palette_high": [0.9, 0.6, 1.0]
  }
}
```

**Reload**: Click "Reload" button in shader dropdown area.

### Scene Block Duplication

1. Select scene in timeline
2. Click **"Duplicate"** button
3. New scene created with same settings
4. Adjust timing/parameters as needed

### Timeline Filtering

**Filter bar** (above timeline):
- Type to filter by name
- Shows only matching blocks
- **Clear** button resets filter

---

## Next Steps

- **[Shader Guide](SHADER_GUIDE.md)** - Write your own GLSL scenes
- **[Curve System Guide](CURVE_SYSTEM_GUIDE.md)** - Master curve animation
- **[3D Stage Guide](3D_STAGE_GUIDE.md)** - Enable 3D rendering
- **[API Reference](../architecture/API_REFERENCE.md)** - C++ runtime API

---

**Last Updated**: May 30, 2026  
**Version**: 1.0
