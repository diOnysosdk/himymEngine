# Demo Intro - 60 Second Production

A complete demoscene-style intro showcasing all Phase 1 & 2 libraries in a polished 60-second production.

## Features

### Four Distinct Scenes

**Scene 1: Plasma Waves (0-15s)**
- Animated plasma effect with sine wave interference
- Speed increases from slow to fast, then settles
- Complexity builds throughout the scene
- Smooth RGB color cycling

**Scene 2: Tunnel Effect (13-30s)**
- Classic demoscene tunnel with perspective
- Variable speed and twist animation
- Striped pattern that flows toward viewer
- Vignette effect for depth
- Overlaps with Scene 1 for smooth transition (13-15s)

**Scene 3: Fractal Zoom (28-45s)**
- Mandelbrot-like iterative fractal
- Continuous zoom into the fractal
- Rotation speed increases over time
- Dynamic color mapping based on iteration count
- Overlaps with Scene 2 (28-30s)

**Scene 4: Radial Burst (43-60s)**
- Radial rays emanating from center
- Pulse waves expanding outward
- Number of rays increases over time
- Pulsation speed accelerates
- Glowing center hotspot
- Overlaps with Scene 3 (43-45s)

### Technical Highlights

- **Timeline Management**: 4 overlapping cues with 2-second fade in/out
- **Animation Curves**: 8 unique curves controlling various parameters
  - Scene 1: speed, complexity
  - Scene 2: speed, twist
  - Scene 3: zoom, rotation
  - Scene 4: rays, pulse
- **Shader Variety**: 4 distinct fragment shaders
- **Smooth Transitions**: 2-second crossfades between scenes

## Building

```powershell
# Build
cmake --build build --config Release

# Run
.\build\bin\Release\demo_intro.exe
```

## Runtime

- **Duration**: 60 seconds
- **Resolution**: 1920x1080 fullscreen
- **Exit**: Automatic after 60s or press ESC

## Animation Timeline

```
0s    ─────┬─────────────┬─────────────┬─────────────┬─────────────┬───── 60s
          5s           15s           30s           45s
         
Scene 1: Plasma      ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░
Scene 2: Tunnel                ░░▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░
Scene 3: Fractal                          ░░▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░
Scene 4: Burst                                      ░░▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓

Legend: ▓ = full opacity, ░ = fading
```

## Size Check

Expected size:
- Uncompressed: ~25-30 KB
- Compressed (kkrunchy): <10 KB

This validates our framework can create real intros within size constraints!

## What This Demonstrates

✅ All Phase 1 & 2 libraries working together  
✅ Complex shader effects  
✅ Smooth scene transitions  
✅ Curve-based parameter animation  
✅ Timeline-based sequencing  
✅ Production-ready intro structure  
✅ Size-optimized executable  

Perfect foundation for future productions!
