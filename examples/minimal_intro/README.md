# Minimal Intro Test

Phase 1 integration test - demonstrates the three core libraries working together.

## What it does

- Opens a 1920x1080 window (windowed mode for testing)
- Creates an OpenGL 3.3 context via WGL
- Compiles a simple animated shader (pulsing gradient)
- Renders for 10 seconds or until ESC is pressed
- Uses all three Phase 1 libraries:
  - `rev_platform` - window, timing, input
  - `rev_shader` - GLSL compilation
  - `rev_xm` - (stub for now, ready for audio)

## Building

From the project root:

```powershell
# Configure
cmake -B build -G "Visual Studio 17 2022"

# Build
cmake --build build --config Release

# Run
.\build\bin\Release\minimal_intro.exe
```

## Validation Checklist

- ✅ Window opens at 1920x1080
- ✅ Animated gradient appears (colors pulse outward)
- ✅ Runs for 10 seconds then exits automatically
- ✅ ESC key immediately closes the window
- ✅ No compilation errors
- ✅ No runtime crashes

## Expected Output

A colorful animated gradient that pulses from the center of the screen. The colors should smoothly transition through the spectrum based on time and distance from center.

## Next Steps

Once this test passes:
1. Add libxm to enable audio playback
2. Proceed to Phase 2: Animation Libraries (curves and timeline)
3. Begin work on the editor
