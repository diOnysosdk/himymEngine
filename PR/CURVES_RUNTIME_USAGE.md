# Using Animation Curves in Runtime

## Overview
The curve system allows you to animate cue parameters over time. When you export your project from the editor, curve data is included in `cues.txt` and must be loaded and evaluated in your runtime code.

## Changes Made

### 1. Export Format (`cues.txt`)

**Image Cues** now include curve assignments:
```
# OLD FORMAT (14 fields):
asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end

# NEW FORMAT (18 fields):
asset_key|asset_path|x|y|scale|opacity|cue_start|cue_end|layer_order|effect_type|fade_in_start|fade_in_end|fade_out_start|fade_out_end|curve_x|curve_y|curve_scale|curve_opacity
```

**Curves Section** format:
```
[curves]
# curve_id|wrap_mode|duration|point_count
0|clamp|1.000|2
  0.000|0.500|0.500|0.500|linear
  1.000|0.800|0.500|0.500|linear
1|loop|2.000|3
  0.000|0.000|0.500|0.500|linear
  0.500|1.000|0.500|0.500|ease_in_out
  1.000|0.000|0.500|0.500|linear
```

### 2. Runtime Loading

```cpp
#include "rev_runtime.h"

// Load curves first (before cues)
rev::curve::Curve curves[32];
int curve_count = rev::runtime::LoadCurves("intros/myintro/cues.txt", curves, 32);

// Load cues (now includes curve assignments)
rev::runtime::ImageCue img_cue;
if (rev::runtime::LoadImageCue("intros/myintro/cues.txt", &img_cue)) {
    // img_cue.curve_x, curve_y, curve_scale, curve_opacity are now loaded
    // (-1 means no curve assigned)
}
```

### 3. Runtime Evaluation

When rendering, evaluate curves based on **elapsed time** from cue start:

```cpp
void RenderImageCue(const rev::runtime::ImageCue& cue, 
                    const rev::curve::Curve* curves, 
                    float current_time)
{
    // Start with base values
    float anim_x = cue.x;
    float anim_y = cue.y;
    float anim_scale = cue.scale;
    float anim_opacity = cue.opacity;
    
    // Calculate elapsed time from cue start
    float elapsed_time = current_time - cue.cue_start;
    
    // Evaluate curves if assigned and cue is active
    if (elapsed_time >= 0.0f) {
        if (cue.curve_x >= 0) {
            const rev::curve::Curve* curve = &curves[cue.curve_x];
            float t = elapsed_time / curve->duration;
            anim_x = rev::curve::Evaluate(*curve, t);
        }
        
        if (cue.curve_y >= 0) {
            const rev::curve::Curve* curve = &curves[cue.curve_y];
            float t = elapsed_time / curve->duration;
            anim_y = rev::curve::Evaluate(*curve, t);
        }
        
        if (cue.curve_scale >= 0) {
            const rev::curve::Curve* curve = &curves[cue.curve_scale];
            float t = elapsed_time / curve->duration;
            anim_scale = rev::curve::Evaluate(*curve, t);
        }
        
        if (cue.curve_opacity >= 0) {
            const rev::curve::Curve* curve = &curves[cue.curve_opacity];
            float t = elapsed_time / curve->duration;
            anim_opacity = rev::curve::Evaluate(*curve, t);
        }
    }
    
    // Use anim_x, anim_y, anim_scale, anim_opacity for rendering
    // ... your OpenGL rendering code here ...
}
```

## Key Concepts

### Time Calculation
- **Elapsed Time** = `current_time - cue.cue_start`
- **Normalized Time** = `elapsed_time / curve.duration`
- The curve evaluates from 0.0 to 1.0 over its duration
- Wrap modes control behavior outside 0-1 range

### Wrap Modes
- **Clamp**: Hold first/last value (default)
- **Loop**: Repeat the curve (0→1, 0→1, ...)
- **PingPong**: Bounce back and forth (0→1, 1→0, 0→1, ...)
- **Mirror**: Mirror the curve pattern

### Example Timeline
```
Cue starts at 5.0 seconds
Curve duration is 2.0 seconds
Wrap mode is Loop

current_time=5.0  → elapsed=0.0  → t=0.0   → curve at start
current_time=6.0  → elapsed=1.0  → t=0.5   → curve at midpoint
current_time=7.0  → elapsed=2.0  → t=1.0   → curve completes (loops)
current_time=8.0  → elapsed=3.0  → t=1.5   → curve at midpoint again (loop)
current_time=9.0  → elapsed=4.0  → t=2.0   → curve completes 2nd loop
```

## Backwards Compatibility

The runtime loader is backwards compatible:
- Old `cues.txt` files without curve fields will still load
- Curve fields default to `-1` (no curve)
- If no curves are assigned, cues behave exactly as before

## Full Example

```cpp
#include "rev_runtime.h"
#include <vector>

struct IntroData {
    std::vector<rev::runtime::ImageCue> image_cues;
    rev::curve::Curve curves[32];
    int curve_count;
};

bool LoadIntro(const char* cues_path, IntroData* intro) {
    // Load all curves first
    intro->curve_count = rev::runtime::LoadCurves(cues_path, intro->curves, 32);
    
    // Load image cues (each might reference curves)
    FILE* f = nullptr;
    fopen_s(&f, cues_path, "r");
    if (!f) return false;
    
    char line[1024];
    bool in_section = false;
    
    while (fgets(line, sizeof(line), f)) {
        // ... parse [image_cues] section ...
        // For each line, create ImageCue and load it
        rev::runtime::ImageCue cue;
        if (/* parse succeeded */) {
            intro->image_cues.push_back(cue);
        }
    }
    
    fclose(f);
    return true;
}

void RenderIntro(const IntroData& intro, float current_time) {
    for (const auto& cue : intro.image_cues) {
        // Check if cue is active
        if (current_time < cue.cue_start || current_time > cue.cue_end) {
            continue;
        }
        
        // Evaluate curves and render
        RenderImageCue(cue, intro.curves, current_time);
    }
}
```

## Notes

- **Performance**: Curve evaluation is fast (linear search + interpolation)
- **Memory**: Up to 32 curves, each can have variable point count
- **Thread Safety**: Curve evaluation is read-only and thread-safe
- **Cleanup**: Call `rev::curve::DestroyCurve()` on each curve when done
