# Quick Start Guide

**Get HowIMetYourMod running in 5 minutes**

## Prerequisites

- **Windows 11 64-bit**
- **Visual Studio 2022** (Community or better) with C++ desktop development
- **CMake 3.20+** (comes with VS 2022)
- **Python 3.11+** (for editor)
- **Git** (optional, for version control)

## Step 1: Clone Repository

```powershell
git clone https://github.com/yourhandle/HowIMetYourMod.git
cd HowIMetYourMod
```

## Step 2: Build Runtime

```powershell
# Configure CMake
cmake -S . -B build

# Build Release
cmake --build build --config Release

# Test run (should display shader for 30 seconds then exit)
.\build\Release\intro.exe
```

**Expected result**: Fullscreen window with shader effect, exits after 30s or on ESC.

## Step 3: Launch Editor

```powershell
# Create Python virtual environment (optional but recommended)
python -m venv .venv
.\.venv\Scripts\Activate.ps1

# Launch editor
python tools/scene_block_editor.py
```

**Expected result**: Tkinter window with timeline and properties panel.

## Step 4: Create Your First Scene

1. **Click "+ Scene"** button (bottom left)
2. **Select the scene** in timeline
3. **Click "Edit..."** button
4. In shader modal:
   - Choose shader from dropdown (e.g., "plasma")
   - Adjust colors with "Pick..." buttons or **🎨 Randomize Colors**
   - Click **Apply & Close**
5. **Click "Do It All"** button (top bar)
   - Saves project
   - Exports to `assets/cues.txt`
   - Rebuilds runtime
   - Runs intro automatically

**Result**: Your shader plays for the scene duration!

## Step 5: Explore Features

### Shader Curves
1. In shader modal, click **"Shader Curves..."**
2. Select parameter (e.g., `speed`)
3. **Add Point** → drag to shape curve
4. **Apply + Stay** to test

### Randomize Tools
In shader modal, try the randomize buttons:
- **🎨 Randomize Colors** - Harmonious palettes
- **📈 Randomize Curves** - Animated parameters
- **🎲 Randomize Values** - Numeric exploration
- **↺ Reset Values** - Safe defaults

### Shader Duplication
```powershell
python tools/shader_scene_duplicator.py \
    --source 0_plasma.glsl \
    --name my_plasma \
    --speed 2.0 \
    --intensity 1.5
```

Creates `39_my_plasma.glsl` with your parameters.

## Common Issues

### "intro.exe exits immediately"
- Check `build/runtime_startup.log` for errors
- Verify `assets/cues.txt` exists (run Export in editor)
- Ensure shader_scene_id is not -1 in cues.txt

### "Editor doesn't launch"
- Install tkinter: `python -m pip install tk` (usually built-in)
- Check Python version: `python --version` (must be 3.11+)

### "Shader changes not visible"
- Click **"Do It All"** after editing (not just "Apply")
- Editor changes require Export → Build → Run

### "Build failed: shader concatenation error"
- Check shader syntax in `assets/shader/scenes/*.glsl`
- Ensure no stray `@editor_*` metadata outside comments

## Next Steps

- **[Editor Guide](../guides/EDITOR_GUIDE.md)** - Complete editor walkthrough
- **[Animated Sprite Workflow](../guides/EDITOR_GUIDE.md#workflow-4b-add-animated-sprite-overlay)** - Add frame-sequence sprite cues with playback controls
- **[Shader Guide](../guides/SHADER_GUIDE.md)** - Write your own shaders
- **[Curve System](../guides/CURVE_SYSTEM_GUIDE.md)** - Master curve animation
- **[3D Stage](../guides/3D_STAGE_GUIDE.md)** - Enable optional 3D rendering

## Workflow Summary

```
┌─────────────┐
│ Edit Scene  │
│   (editor)  │
└──────┬──────┘
       │
       v
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Export    │ --> │    Build    │ --> │     Run     │
│ (cues.txt)  │     │  (intro.exe)│     │ (fullscreen)│
└─────────────┘     └─────────────┘     └─────────────┘
       ^                                        │
       └────────────────────────────────────────┘
                    Iterate!
```

**Use "Do It All" button to automate this entire workflow!**

---

**Questions?** See [FROM_SCRATCH.md](FROM_SCRATCH.md) for complete technical details.

**Ready to ship?** See `docs/FOOTPRINT-OPTIMIZATION-PLAN.md` for size optimization.
