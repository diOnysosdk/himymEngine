# Quick Extension Patterns

## Adding a New Scene (Nebula → Nebula + Aurora)

### Step 1: Add shader function
In `assets/shader/fragment.glsl`, add a new procedural scene branch in `renderScene(...)`:

```glsl
else if (id == 26) {
    // Implement aurora curtains, etc.
    float w = sin(uv.y * 2.0 + t);
    col = mix(vec3(0.0, 0.5, 0.8), vec3(0.2, 0.8, 0.5), w * 0.5 + 0.5);
}
```

### Step 2: Dispatch in fragment shader
In the fragment shader main, add a simple scene selector:

```glsl
// In renderScene(int id, ...)
if (id == 0) {
    // existing scene
} else if (id == 26) {
    // your new scene
}
```

### Step 3: Update sequence to choose scene
In `src/sequence/sequence.cc`, track which scene is active:

```cpp
struct SequenceState {
    MainSceneId current_scene = MainSceneId::kMainSceneA;
};

// In UpdateSequence:
if(time_seconds < kScene1Duration + kScene2Duration) {
    state->current_scene = MainSceneId::kMainSceneB;
}
```

### Step 4: Pass to shader
In `src/sequence/scene_management.cc`, set `shader_scene_id` for the scene you want to map to your new branch (for example 26), then rebuild.

**Total code addition**: usually ~10-30 lines shader plus small scene config updates.

---

## Audio vs Visual Timing Model

### Step 1: Replace audio stub
Replace `src/audio/audio_stub.cc` with:
- `jar_xm` static decode
- WinMM `waveOut` sample submission

### Step 2: Get music time
Update `GetMusicTimeSeconds()` to return accumulated samples/samplerate.

### Step 3: Keep clocks separated
Use music time only for audio state/cue logic. Visuals should run on a smooth frame-delta visual timeline:
```cpp
UpdateMusicPlayback(&music_state);  // audio path

visual_time_seconds += visual_dt;    // smooth visual clock
UpdateSequence(visual_time_seconds, &sequence_state);

ApplyCommonCurveModulation(visual_time_seconds, &exposure, &fade, &scene_blend);
ApplyShaderCurveModulation(scene_id, visual_time_seconds, &scene_visual);
EvaluateShaderDynChannels(scene_id, visual_time_seconds, scene_dyn);
```

Cue/effect windows for image/text should use one shared scene-local clock derived from sequence (for example `cue_scene_time_seconds`).

**Total lines**: ~80-100 lines in audio/module_music.cc

---

## Adding Text Overlay

If space permits, add GDI font atlas + legacy `glRasterPos`/`glCallLists`:

### Step 1: Create font atlas in renderer init
```cpp
// In renderer::InitializeRenderer():
CreateFontAtlas(&renderer_state.font_texture, font_face, font_height);
```

### Step 2: Render text after main scene
```cpp
// In renderer::RenderFrame(), after fullscreen quad:
if(show_text_overlay && text_reveal > 0) {
    RenderTextOverlay(&renderer_state, text_string, text_reveal);
}
```

### Step 3: Control via sequence
Pass `text_reveal` from `SequenceState` → `ContentParams` → `RenderFrame()`.

**Total lines**: ~60-80 lines in renderer

---

## Looping Multiple Times

To loop the entire main section N times without modifying sequence logic:

```cpp
// In UpdateSequence():
double total_main_duration = kMainLoop Duration;
double loop_number = (time_seconds - startup_total) / total_main_duration;
bool should_exit_at_end_of_loop = (loop_number >= desired_loops);

// Or just let it loop indefinitely (modulo arithmetic)
state->phase_time_seconds = fmod(time_seconds - startup_total, 
                                  total_main_duration);
```

---

## Size Accounting

Current baseline (Nebula + fade/exposure only): ~20 KB uncompressed

Estimated additions:
- Second procedural scene: +2-3 KB (shader code)
- Audio (jam_xm + WinMM): +8-12 KB
- Image overlay path (texture decode + quad pass): +3-5 KB

**Intro64k budget**: 64 KB raw, ~8-10 KB compressed. Nebula + audio + text = ~44 KB uncompressed → fits easily when compressed.

---

## Testing Pattern
1. Change a knob in sequence.cc (e.g., `kStartupFadeInSeconds`)
2. Rebuild: `cmake --build build --config Release`
3. Run from `build/Release/intro.exe`
4. Press ESC to exit

No separate config loading, no runtime knob system — all compile-time.
