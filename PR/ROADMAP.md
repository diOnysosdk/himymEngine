# Implementation Roadmap: C++ Modular Framework

**Step-by-step plan to build the new architecture from scratch**

This roadmap breaks down [FROM_SCRATCH_V2.md](FROM_SCRATCH_V2.md) into concrete, testable milestones.

---

## Prerequisites

### Tools
- ✅ Visual Studio 2022 (or Clang + ninja)
- ✅ CMake 3.20+
- ✅ Git (for submodules)
- ✅ kkrunchy or Crinkler (for final compression)

### Knowledge
- C++17 basics (namespaces, templates, move semantics)
- Win32 API (window creation, message pump)
- OpenGL 3.3 (shaders, uniforms, buffers)
- CMake build system
- Dear ImGui (optional, for editor)

---

## Phase 1: Foundation Libraries (Week 1)

### Milestone 1.1: rev_platform

**Goal**: Window + OpenGL context + timing

**Tasks**:
1. Create library structure:
   ```bash
   mkdir -p revision_libs/rev_platform/{include,src}
   ```

2. Write `include/rev_platform.h`:
   - `struct Window`
   - `CreateWindow()`, `DestroyWindow()`
   - `PollEvents()`, `SwapBuffers()`
   - `GetTime()`, `IsKeyPressed()`
   - `LoadGLFunctions()`, `GetProcAddress()`

3. Implement `src/platform_win32.cpp`:
   - `WinMain` entry point setup
   - `RegisterClassEx` + `CreateWindowEx`
   - `WGL_ARB_create_context` for GL 3.3
   - Message pump with ESC handling

4. Implement `src/platform_timing.cpp`:
   - `QueryPerformanceFrequency` + `QueryPerformanceCounter`
   - Return double (seconds)

5. Implement `src/platform_gl_loader.cpp`:
   - Manual `wglGetProcAddress` for GL functions
   - Load common GL 3.3 functions (glCreateShader, glUniform*, etc.)

6. Write `CMakeLists.txt`:
   ```cmake
   add_library(rev_platform STATIC ...)
   target_link_libraries(rev_platform PRIVATE opengl32 gdi32 user32)
   ```

**Test**: Compile library, verify no errors

**Validation**: Create test app that opens window for 5 seconds

---

### Milestone 1.2: rev_shader

**Goal**: Compile GLSL, set uniforms

**Tasks**:
1. Write `include/rev_shader.h`:
   - `struct Program`
   - `CompileFromSource()`, `DestroyProgram()`, `Use()`
   - `GetUniformLocation()`, `SetFloat/Vec2/Vec3/Vec4/Int()`

2. Implement `src/shader.cpp`:
   - `glCreateShader()` + `glShaderSource()` + `glCompileShader()`
   - Check `glGetShaderiv(GL_COMPILE_STATUS)`
   - `glCreateProgram()` + `glAttachShader()` + `glLinkProgram()`
   - `glGetUniformLocation()`, `glUniform*()` wrappers

**Test**: Compile library

**Validation**: Test app compiles "void main() { gl_FragColor = vec4(1,0,0,1); }"

---

### Milestone 1.3: rev_xm

**Goal**: XM music playback

**Tasks**:
1. Add libxm to `revision_libs/rev_xm/third_party/`
2. Write `include/rev_xm.h`:
   - `struct Player`
   - `CreatePlayer()`, `DestroyPlayer()`, `Update()`

3. Implement `src/xm_player.cpp`:
   - `xm_create_context_from_data()`
   - `xm_generate_samples()` wrapper

**Test**: Compile library

**Validation**: Test app plays XM for 10 seconds (write samples to file or use WASAPI)

---

### Milestone 1.4: Integration Test

**Goal**: Minimal intro with shader + music

**File**: `examples/minimal_intro/main.cpp`

```cpp
#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_xm.h"

// Embedded assets
const char fragment[] = "void main() { gl_FragColor = vec4(1,0,0,1); }";

int main() {
    auto* window = rev::platform::CreateWindow(1920, 1080, true, "Test");
    rev::platform::LoadGLFunctions();
    
    const char* vertex = "void main() { gl_Position = vec4(0,0,0,1); }";
    auto* shader = rev::shader::CompileFromSource(vertex, fragment);
    
    while (!window->should_close && rev::platform::PollEvents(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        rev::shader::Use(shader);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        rev::platform::SwapBuffers(window);
        if (rev::platform::IsKeyPressed(window, VK_ESCAPE)) break;
    }
    
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyWindow(window);
    return 0;
}
```

**Validation**: Red fullscreen, ESC to exit

---

## Phase 2: Animation Libraries (Week 2)

### Milestone 2.1: rev_curve

**Goal**: Time-value curves with easing

**Tasks**:
1. Write `include/rev_curve.h`:
   - `enum class EaseMode`
   - `struct Point` (t, v, in_ease, out_ease, mode)
   - `struct Curve` (points array, count)
   - `Evaluate()`, `EvaluateClamped()`

2. Implement `src/curve.cpp`:
   - Linear interpolation
   - Smoothstep, ease_in/out formulas
   - Binary search for segment

**Test**: Unit test - create curve with 3 points, evaluate at 0.0, 0.5, 1.0

---

### Milestone 2.2: rev_sequence

**Goal**: Timeline + cue system

**Tasks**:
1. Write `include/rev_sequence.h`:
   - `struct Cue` (start, end, fade_in, fade_out, id, opacity)
   - `struct Timeline` (cues array, current_time)
   - `Update()`, `GetActiveCues()`, `GetOpacity()`

2. Implement `src/sequence.cpp`:
   - Time advancement
   - Fade calculation (linear for now)
   - Active cue filtering

**Test**: Unit test - create timeline with 3 cues, update to various times

---

### Milestone 2.3: Integration Test

**Goal**: Animated shader parameter

**Extend minimal_intro**:
```cpp
// Add curve for u_time multiplier
rev::curve::Curve speed_curve = /* ... */;
float speed = rev::curve::Evaluate(speed_curve, normalized_time);
rev::shader::SetFloat(shader, u_time_loc, time * speed);
```

**Validation**: Shader animates (speed changes over time)

---

## Phase 3: Editor Foundation (Week 3)

### Milestone 3.1: ImGui Integration

**Goal**: Basic ImGui window

**Tasks**:
1. Add Dear ImGui to `revision_libs/rev_editor/third_party/imgui/`
2. Create `rev_editor` library skeleton
3. Write `include/rev_editor.h`:
   - `struct EditorContext`
   - `CreateEditor()`, `DestroyEditor()`
   - `BeginFrame()`, `RenderUI()`, `EndFrame()`

4. Implement `src/editor_context.cpp`:
   - `ImGui_ImplWin32_Init()` + `ImGui_ImplOpenGL3_Init()`
   - Frame lifecycle wrappers

**Test**: Editor app opens with empty ImGui window

---

### Milestone 3.2: Project Load/Save

**Goal**: JSON project persistence

**Tasks**:
1. Define `ProjectData` structure:
   ```cpp
   struct ProjectData {
       rev::sequence::Timeline timeline;
       std::vector<rev::curve::Curve> curves;
       // ... assets, etc.
   };
   ```

2. Implement `src/editor_project.cpp`:
   - `LoadProject()` - Parse JSON (nlohmann/json or custom)
   - `SaveProject()` - Serialize to JSON

**Test**: Create sample project JSON, load it, verify data

---

### Milestone 3.3: Timeline UI

**Goal**: Render scene blocks horizontally

**Tasks**:
1. Implement `src/editor_timeline.cpp`:
   - `ImGui::BeginChild()` for scrollable timeline
   - Draw rectangles for each cue (ImDrawList)
   - Handle click to select
   - Drag to move/resize (update cue start/end)

**Test**: Load project with 3 scenes, render timeline, click to select

---

## Phase 4: Editor Features (Week 4)

### Milestone 4.1: Shader Modal

**Goal**: Edit shader parameters

**Tasks**:
1. Implement `src/editor_shader_modal.cpp`:
   - Preset dropdown (load from presets.json)
   - Color pickers (`ImGui::ColorEdit3`)
   - Sliders for speed, intensity, warp
   - Apply/Cancel buttons

**Test**: Open modal, change colors, Apply, verify ProjectData updated

---

### Milestone 4.2: Curve Editor

**Goal**: Visual curve editing

**Tasks**:
1. Implement `src/editor_curve.cpp`:
   - Custom ImGui widget (canvas with ImDrawList)
   - Render curve as connected lines
   - Click canvas to add point
   - Drag point to move
   - Delete point on right-click

**Test**: Create curve, add 5 points, drag them, verify curve shape

---

### Milestone 4.3: Export

**Goal**: Project → cues.txt

**Tasks**:
1. Implement `src/editor_export.cpp`:
   - `ExportCues()` - Generate pipe-delimited format
   - Shader cue rows (all parameters)
   - Curve rows (target|param|points)
   - Timeline pipeline rows

**Test**: Export project, verify cues.txt matches expected format

---

### Milestone 4.4: Build Integration

**Goal**: "Do It All" button

**Tasks**:
1. Implement `src/editor_build.cpp`:
   - `BuildProject()` - Run `cmake --build build --config Release`
   - `RunIntro()` - `CreateProcess()` to launch intro.exe
   - `DoItAll()` - Save → Export → Build → Run

**Test**: Click button, verify intro launches

---

## Phase 5: 3D Rendering (Optional, Week 5)

### Milestone 5.1: rev_mesh

**Goal**: Load and render 3D meshes

**Tasks**:
1. Write `include/rev_mesh.h`:
   - `struct Vertex`, `struct MaterialSlot`, `struct Mesh`
   - `LoadMeshbin()`, `LoadOBJ()`, `Render()`

2. Implement `src/mesh_loader_meshbin.cpp` (binary format)
3. Implement `src/mesh_loader_obj.cpp` (OBJ/MTL parser)
4. Implement `src/mesh_renderer.cpp` (VBO/IBO upload, glDrawElements)

**Test**: Load cube.obj, render with glDrawElements

---

### Milestone 5.2: Editor 3D Panel

**Goal**: Mesh inspector in editor

**Tasks**:
1. Add mesh file picker to ProjectData
2. Render 3D preview viewport (separate framebuffer)
3. Camera orbit controls (mouse drag)

**Test**: Load mesh, see preview in editor

---

## Phase 6: Polish & Optimization (Week 6)

### Milestone 6.1: Size Optimization

**Goal**: Minimize binary size

**Tasks**:
1. Set compiler flags in CMakeLists.txt:
   ```cmake
   target_compile_options(intro PRIVATE /O1 /GS- /GL)
   target_link_options(intro PRIVATE /LTCG /OPT:REF /OPT:ICF)
   ```

2. Measure per-library size:
   ```bash
   size -A librev_platform.a
   ```

3. Compress with kkrunchy:
   ```bash
   kkrunchy intro.exe --out intro_packed.exe
   ```

**Test**: Verify <30 KB compressed

---

### Milestone 6.2: Testing

**Goal**: Validate all features

**Tests**:
1. Runtime tests:
   - Window creation + GL context
   - Shader compilation
   - XM playback
   - Curve evaluation
   - Timeline playback

2. Editor tests:
   - Load project
   - Edit timeline (add/delete/move scenes)
   - Edit shader parameters
   - Edit curves
   - Export cues.txt
   - Build and run

---

### Milestone 6.3: Documentation

**Goal**: Update guides for C++ editor

**Tasks**:
1. Write **C++ Editor Guide** (replace Python version)
2. Write **Library Integration Examples**
3. Update **API Reference** with all library functions

---

## Validation Checklist

### Runtime
- [ ] Window opens fullscreen 1920x1080
- [ ] Shader compiles and renders
- [ ] XM music plays (at least 30 seconds)
- [ ] ESC key exits immediately
- [ ] Curves animate shader parameters
- [ ] Timeline manages multiple cues with fades
- [ ] (Optional) 3D mesh renders with textures
- [ ] Final size <100 KB uncompressed
- [ ] Final size <30 KB compressed

### Editor
- [ ] Opens windowed 1600x900
- [ ] Loads project.json
- [ ] Timeline renders all scenes
- [ ] Can add/delete/move scenes
- [ ] Shader modal edits parameters
- [ ] Curve editor adds/moves/deletes points
- [ ] Export generates valid cues.txt
- [ ] Build integration compiles intro
- [ ] Run launches intro successfully
- [ ] "Do It All" completes full workflow
- [ ] Final size <300 KB

---

## Success Criteria

**Minimum Viable Product**:
- ✅ Intro runs with shader + music
- ✅ Editor loads/saves projects
- ✅ Timeline UI functional
- ✅ Export → build → run workflow works

**Full Feature Set**:
- ✅ All MVP features
- ✅ Curve editor with visual editing
- ✅ Shader modal with randomization
- ✅ 3D mesh rendering (optional)
- ✅ Size optimized (<100 KB runtime)

**Production Ready**:
- ✅ All Full Feature Set
- ✅ Documentation complete
- ✅ All tests passing
- ✅ Competition-tested (runs on compo machine)

---

## Timeline Summary

```
Week 1: Foundation (rev_platform + rev_shader + rev_xm)
Week 2: Animation (rev_curve + rev_sequence)
Week 3: Editor Foundation (ImGui + timeline UI)
Week 4: Editor Features (shader modal + curve editor + export)
Week 5: 3D Rendering (rev_mesh + editor 3D panel) [optional]
Week 6: Polish (optimization + testing + docs)
```

**Total**: 6 weeks (5 weeks if skipping 3D)

---

## Next Steps

1. **Set up repository**:
   ```bash
   mkdir revision_libs
   cd revision_libs
   git init
   git submodule add https://github.com/ocornut/imgui.git third_party/imgui
   ```

2. **Start with Milestone 1.1** (rev_platform)

3. **Test after each milestone** (don't move forward until validated)

4. **Track progress** in this document (check off milestones)

5. **Iterate** - Refine as you learn

---

**Last Updated**: May 30, 2026  
**Version**: 1.0  
**Status**: Roadmap for implementing FROM_SCRATCH_V2.md
