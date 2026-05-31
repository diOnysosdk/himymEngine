# Scene Editor Vision: Full Demo Production Tool

**Goal**: Timeline-based editor for authoring complete demoscene productions (intros, demos)

---

## Current State ✅

### Working Features

**Timeline System**:
- Scene blocks (add/delete/reorder)
- Scene properties (name, duration)
- Total duration calculation
- Scene selection/navigation

**Shader Cues**:
- 10 shader presets (plasma, tunnel, raymarcher, fractal, etc.)
- Full modal with dropdown preset selection
- Color palette (low/mid/high RGB pickers)
- Parameters (speed, intensity, warp, exposure, fade)
- Curve assignments (5 parameters → curve slots)
- Layer controls (opacity, blend mode, order)
- Timing (cue start/end, fade in/out)

**Curve Editor**:
- Canvas-based Bézier curve drawing
- Add/delete/move control points
- 32 curve slots
- Visual grid and preview

**Asset Browser**:
- File listing (Music, Images, Shaders, All Files)
- WIN32_FIND_DATA directory scanning
- CollapsibleHeader organization

**Export Pipeline**:
- JSON → cues.txt conversion
- Pipe-delimited format
- Shader/curve metadata export

**Build Integration**:
- CMake build invocation
- Launch minimal_intro.exe
- Status notifications with timer

**Project Management**:
- Save/Load with Win32 file dialogs
- JSON serialization with backwards compatibility
- Auto-save workflow

---

## Missing Features ❌

### High Priority (Core Functionality)

**1. Music Cues** ⭐ NEXT
- XM file selection from asset browser
- Music timing (start time, duration)
- Volume control
- Fade in/out
- Loop support
- BPM/pattern sync markers

**2. Image Cues**
- PNG/JPG file selection
- Placement (position, scale, rotation)
- Fade in/out/opacity
- Layer compositing
- Texture filtering mode

**3. Text Cues**
- Text content editor
- Font selection (system fonts)
- Typography (size, style, color)
- Position and alignment
- Fade in/out
- Scroll/typewriter effects

**4. Live Preview Viewport**
- Embedded OpenGL render
- Timeline scrubbing
- Play/pause/stop controls
- Current time display
- Preview resolution settings

**5. Timeline Playback**
- Play from current position
- Scrub to specific time
- Frame-accurate stepping
- Loop region selection
- Sync with music playback

### Medium Priority (Enhanced Authoring)

**6. Multi-Layer Compositing**
- Layer stack visualization
- Drag to reorder layers
- Per-layer blend modes
- Global post-processing

**7. Camera System** (for 3D scenes)
- Camera position curves
- Look-at target
- FOV/zoom controls
- Camera shake effects

**8. Transition Effects**
- Crossfade between scenes
- Wipe patterns
- Custom transition shaders
- Duration/easing control

**9. Color Grading**
- LUT/color correction
- Brightness/contrast/saturation
- Vignette/film grain
- Per-scene grading

**10. Mesh Placement** (if 3D enabled)
- OBJ file import
- Transform controls (position, rotation, scale)
- Material assignment
- Animation curves for transforms

### Low Priority (Polish & QoL)

**11. Timeline Zoom/Pan**
- Horizontal zoom slider
- Pan with middle mouse drag
- Fit all/fit selection

**12. Keyboard Shortcuts**
- Space: Play/pause
- Home/End: Go to start/end
- Arrow keys: Frame step
- Delete: Remove selected cue
- Ctrl+D: Duplicate cue

**13. Undo/Redo**
- Action history stack
- Ctrl+Z/Ctrl+Y

**14. Asset Thumbnails**
- Image preview in browser
- Shader preset previews
- Music waveform display

**15. Templates & Presets**
- Project templates (intro, demo, credits)
- Shader preset library
- Curve preset library
- Scene templates

**16. Validation & Warnings**
- Missing asset detection
- Overlapping cue warnings
- Performance hints (shader complexity)
- Export validation

---

## Implementation Priority

### Phase 1: Core Content Types (Week 1-2)
1. **Music Cues** - XM playback with timing
2. **Image Cues** - Static images with fade
3. **Text Cues** - Simple text rendering

### Phase 2: Preview & Playback (Week 3)
4. **Live Preview** - Embedded viewport
5. **Timeline Playback** - Play/scrub with sync

### Phase 3: Composition (Week 4)
6. **Layer System** - Visual stack + reordering
7. **Transitions** - Scene crossfades
8. **Color Grading** - Post-processing

### Phase 4: Advanced (Week 5+)
9. **Camera System** - 3D camera curves
10. **Mesh Placement** - 3D object placement
11. **Timeline Polish** - Zoom/pan/shortcuts
12. **Undo/Redo** - History system

### Phase 5: Production Ready (Week 6+)
13. **Validation** - Error checking
14. **Templates** - Project/preset library
15. **Thumbnails** - Asset previews
16. **Documentation** - Complete user guide

---

## Architecture Goals

### Data Model
- **Unified Cue System**: ShaderCue, MusicCue, ImageCue, TextCue all follow same pattern
- **Scene Containers**: Each scene holds array of cues per type
- **Timeline Evaluation**: GetActiveCues(time) returns all visible cues at timestamp
- **Export Format**: Extend cues.txt with [music_cues], [image_cues], [text_cues] sections

### Editor UX
- **Modal Editing**: Each cue type gets modal dialog (like shader modal)
- **Asset Picker**: Click asset browser → populate cue field
- **Visual Feedback**: Color-code cue types on timeline
- **Drag & Drop**: Asset browser → timeline adds cue

### Runtime Integration
- **Loader System**: Parse all cue sections from cues.txt
- **Render Pipeline**: Compose layers back-to-front with blending
- **Audio Sync**: Music timeline drives master clock
- **Deterministic**: All cues evaluated from single time value

---

## Next Steps

**Immediate**: Implement Music Cues
- Add `MusicCue` struct to rev_editor.h
- Add music cue array to SceneBlock
- Create `RenderMusicModal()` with file picker + timing
- Add "+ Music Cue" button to properties panel
- Export music cues to [music_cues] section
- Update minimal_intro to load and play XM files

**After Music**: Image Cues → Text Cues → Preview Viewport → Playback

**Long Term**: Full production pipeline with real-time preview and advanced composition
