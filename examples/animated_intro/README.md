# Animated Intro Test (Phase 2)

Demonstrates Phase 2 animation libraries with curve-based parameter animation and timeline-based color transitions.

## Features

### Curve-based Animation
- **Speed curve**: Animation speed changes over time (slow → fast → slow → normal)
- **Intensity curve**: Pattern intensity pulses with smooth easing

### Timeline with Cues
- **Scene 1** (0-5s): Red color with fade in/out
- **Scene 2** (4-10s): Green color, overlaps with Scene 1
- **Scene 3** (9-15s): Blue color, overlaps with Scene 2
- Colors blend during overlap periods

### Animation Behavior
- Speed varies from 0.3x to 2.0x using EaseInOut
- Intensity pulses between 0.5 and 3.0 using Smoothstep
- Colors fade and blend based on timeline opacity
- Total duration: 15 seconds

## Building

```powershell
# Build (from project root)
cmake --build build --config Release

# Run
.\build\bin\Release\animated_intro.exe
```

## Validation Checklist

Phase 2 validation from ROADMAP.md:

- ✅ Speed curve affects animation rate over time
- ✅ Intensity curve affects pattern detail
- ✅ Timeline cues control color transitions
- ✅ Smooth fading between overlapping cues
- ✅ Colors blend correctly during overlap
- ✅ 15-second runtime with ESC to exit

## What to Expect

**0-4s**: Red pattern, starting slow, speeding up  
**4-5s**: Red → Green blend as Scene 2 fades in  
**5-9s**: Pure green pattern with varying intensity  
**9-10s**: Green → Blue blend as Scene 3 fades in  
**10-15s**: Blue pattern, intensity and speed stabilizing

The pattern should pulse and rotate at different speeds throughout, with smooth color transitions at scene boundaries.

## Technical Details

Uses:
- **rev_curve**: 2 curves (speed, intensity) with 4-5 points each
- **rev_sequence**: Timeline with 3 overlapping cues
- **rev_shader**: Uniforms animated by curves and timeline
- **rev_platform**: 15-second runtime window

Demonstrates all Phase 2 functionality working together.
