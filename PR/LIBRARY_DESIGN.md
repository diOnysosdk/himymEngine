# Library Design for Modular Intro/Demo Framework

**Reusable C++ components for Windows intro/demo productions**

## Overview

This document describes the modular library architecture for building intro/demo runtimes. Each library is a self-contained, reusable component that can be integrated into new projects.

---

## Core Philosophy

### Design Principles

1. **Single Responsibility** - Each library does one thing well
2. **Zero Dependencies** - Libraries don't depend on each other (except explicit contracts)
3. **Header-Only When Possible** - Ease of integration
4. **Minimal Surface Area** - Small, focused API
5. **Competition Ready** - Size-conscious, no bloat

### Build Strategy

```cmake
# Each library as CMake target
add_library(rev_xm STATIC src/xm_player.cpp)
add_library(rev_mesh STATIC src/mesh_loader.cpp)
add_library(rev_platform STATIC src/platform_win32.cpp)

# Client links what they need
target_link_libraries(my_intro PRIVATE rev_xm rev_mesh rev_platform)
```

---

## Library Catalog

### 1. rev_platform - Platform Abstraction

**Purpose**: Win32 window, OpenGL context (WGL), timing, input

**Public API** (`rev_platform.h`):
```cpp
namespace rev::platform {

struct WindowConfig {
    int width = 1920;
    int height = 1080;
    bool fullscreen = true;
    const char* title = "Intro";
};

struct Window {
    void* hwnd;
    void* hdc;
    void* hglrc;
    bool should_close;
};

// Lifecycle
Window* CreateWindow(const WindowConfig& config);
void DestroyWindow(Window* window);
bool PollEvents(Window* window);
void SwapBuffers(Window* window);

// Timing
double GetTime();  // Seconds since init
void Sleep(double seconds);

// Input
bool IsKeyPressed(Window* window, int vk_code);
bool IsMouseButtonPressed(Window* window, int button);
void GetMousePosition(Window* window, int* x, int* y);

// OpenGL
void* GetProcAddress(const char* name);
bool LoadGLFunctions();  // Load core GL 3.3 functions

}  // namespace rev::platform
```

**Implementation**:
- `src/platform_win32.cpp` - Win32 window + WGL bootstrap
- `src/platform_timing.cpp` - QueryPerformanceCounter timing
- `src/platform_gl_loader.cpp` - Manual GL function loading

**Size**: ~15KB compiled

---

### 2. rev_xm - XM Module Player

**Purpose**: Static XM decoder and playback (no DLL dependencies)

**Public API** (`rev_xm.h`):
```cpp
namespace rev::xm {

struct Player {
    void* context;  // Opaque libxm context
    float* buffer;
    int sample_rate;
    int buffer_size;
};

// Lifecycle
Player* CreatePlayer(const void* xm_data, size_t xm_size, int sample_rate = 48000);
void DestroyPlayer(Player* player);

// Playback
void Update(Player* player, float* output, int frame_count);
void SetPosition(Player* player, int pattern, int row);
int GetPosition(Player* player);  // Current pattern
bool IsFinished(Player* player);

// Info
int GetPatternCount(Player* player);
int GetChannelCount(Player* player);
float GetDuration(Player* player);  // Estimated seconds

}  // namespace rev::xm
```

**Implementation**:
- Embeds `libxm` (public domain) as static lib
- `src/xm_player.cpp` - Wrapper around libxm

**Size**: ~30KB compiled (including libxm)

**Usage**:
```cpp
// Embedded XM data
extern const unsigned char music_xm[];
extern const size_t music_xm_len;

auto* player = rev::xm::CreatePlayer(music_xm, music_xm_len);

// In audio callback
float audio_buffer[1024];
rev::xm::Update(player, audio_buffer, 512);  // 512 frames = 1024 samples stereo
```

---

### 3. rev_mesh - 3D Mesh Loading

**Purpose**: Load and render 3D meshes (OBJ/meshbin format)

**Public API** (`rev_mesh.h`):
```cpp
namespace rev::mesh {

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

struct MaterialSlot {
    uint32_t start_index;
    uint32_t count;
    uint32_t diffuse_tex;    // GL texture ID
    uint32_t normal_tex;
};

struct Mesh {
    uint32_t vbo;
    uint32_t ibo;
    Vertex* vertices;
    uint32_t* indices;
    uint32_t vertex_count;
    uint32_t index_count;
    MaterialSlot* material_slots;
    uint32_t material_slot_count;
};

// Loading
Mesh* LoadMeshbin(const void* data, size_t size);
Mesh* LoadOBJ(const char* obj_text, const char* mtl_text = nullptr);
void DestroyMesh(Mesh* mesh);

// Rendering
void UploadToGPU(Mesh* mesh);
void Render(Mesh* mesh, int material_slot_index = -1);  // -1 = all slots

}  // namespace rev::mesh
```

**Implementation**:
- `src/mesh_loader_meshbin.cpp` - Binary format loader
- `src/mesh_loader_obj.cpp` - OBJ/MTL text parser
- `src/mesh_renderer.cpp` - GL buffer upload and rendering

**Size**: ~20KB compiled

---

### 4. rev_shader - Shader Compilation

**Purpose**: GLSL shader compilation, uniform management

**Public API** (`rev_shader.h`):
```cpp
namespace rev::shader {

struct Program {
    uint32_t gl_program;
};

// Compilation
Program* CompileFromSource(const char* vertex_src, const char* fragment_src);
void DestroyProgram(Program* program);
void Use(Program* program);

// Uniforms
int GetUniformLocation(Program* program, const char* name);
void SetFloat(Program* program, int location, float value);
void SetVec2(Program* program, int location, float x, float y);
void SetVec3(Program* program, int location, float x, float y, float z);
void SetVec4(Program* program, int location, float x, float y, float z, float w);
void SetMat4(Program* program, int location, const float* matrix);
void SetInt(Program* program, int location, int value);

}  // namespace rev::shader
```

**Size**: ~10KB compiled

---

### 5. rev_curve - Curve Evaluation

**Purpose**: Time-value curves with easing (for animation)

**Public API** (`rev_curve.h`):
```cpp
namespace rev::curve {

enum class EaseMode {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    Smoothstep,
    Hold
};

struct Point {
    float t;          // Time (normalized 0-1)
    float v;          // Value
    float in_ease;    // In tangent weight
    float out_ease;   // Out tangent weight
    EaseMode mode;
};

struct Curve {
    Point* points;
    int point_count;
};

// Evaluation
float Evaluate(const Curve& curve, float t);
float EvaluateClamped(const Curve& curve, float t, float min, float max);

// Building
Curve CreateCurve(int reserve_points = 16);
void AddPoint(Curve& curve, const Point& point);
void SortPoints(Curve& curve);  // Sort by time
void DestroyCurve(Curve& curve);

}  // namespace rev::curve
```

**Size**: ~8KB compiled

---

### 6. rev_sequence - Timeline/Cue System

**Purpose**: Scene timing, transitions, cue management

**Public API** (`rev_sequence.h`):
```cpp
namespace rev::sequence {

struct Cue {
    float start;
    float end;
    float fade_in;
    float fade_out;
    int id;           // Shader scene ID, mesh ID, etc.
    float opacity;
};

struct Timeline {
    Cue* cues;
    int cue_count;
    float current_time;
};

// Lifecycle
Timeline CreateTimeline(int reserve_cues = 64);
void DestroyTimeline(Timeline& timeline);

// Building
void AddCue(Timeline& timeline, const Cue& cue);
void SortCues(Timeline& timeline);  // Sort by start time

// Playback
void Update(Timeline& timeline, float delta_time);
void SetTime(Timeline& timeline, float time);
float GetTime(const Timeline& timeline);

// Querying
int GetActiveCues(const Timeline& timeline, Cue** out_cues, int max_cues);
float GetOpacity(const Cue& cue, float timeline_time);  // With fades

}  // namespace rev::sequence
```

**Size**: ~12KB compiled

---

### 7. rev_imgui_editor - Editor UI Framework

**Purpose**: ImGui-based editor for scene authoring (C++ replacement for Python editor)

**Public API** (`rev_editor.h`):
```cpp
namespace rev::editor {

struct ProjectData {
    rev::sequence::Timeline timeline;
    rev::curve::Curve* curves;  // Array of curves
    int curve_count;
    // ... shader presets, assets, etc.
};

struct EditorContext {
    ProjectData* project;
    void* imgui_context;
    bool show_timeline;
    bool show_curve_editor;
    bool show_shader_modal;
    // ... UI state
};

// Lifecycle
EditorContext* CreateEditor(rev::platform::Window* window);
void DestroyEditor(EditorContext* editor);

// Project
bool LoadProject(EditorContext* editor, const char* json_path);
bool SaveProject(EditorContext* editor, const char* json_path);
bool ExportCues(EditorContext* editor, const char* cues_path);

// Frame
void BeginFrame(EditorContext* editor);
void RenderUI(EditorContext* editor);
void EndFrame(EditorContext* editor);

// Workflow
bool BuildAndRun(EditorContext* editor, const char* cmake_build_dir);

}  // namespace rev::editor
```

**Implementation**:
- Uses Dear ImGui (MIT license) for UI
- `src/editor_timeline.cpp` - Timeline view (like DAW)
- `src/editor_curve.cpp` - Curve editor canvas
- `src/editor_shader_modal.cpp` - Shader parameter editing
- `src/editor_export.cpp` - JSON → cues.txt conversion
- `src/editor_build.cpp` - CMake subprocess integration

**Size**: ~150KB compiled (including Dear ImGui)

---

## Integration Example

### Minimal Intro Runtime

```cpp
// main.cpp
#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_xm.h"

extern const char fragment_shader_src[];
extern const unsigned char music_xm[];
extern const size_t music_xm_len;

int main() {
    // Platform
    auto* window = rev::platform::CreateWindow({});
    rev::platform::LoadGLFunctions();
    
    // Shader
    const char* vertex_src = R"(
        #version 330
        void main() {
            vec2 pos = vec2((gl_VertexID & 1) * 2 - 1, (gl_VertexID & 2) - 1);
            gl_Position = vec4(pos, 0.0, 1.0);
        }
    )";
    auto* shader = rev::shader::CompileFromSource(vertex_src, fragment_shader_src);
    int u_time = rev::shader::GetUniformLocation(shader, "u_time");
    
    // Music
    auto* music = rev::xm::CreatePlayer(music_xm, music_xm_len);
    
    // Frame loop
    double start_time = rev::platform::GetTime();
    while (!window->should_close && rev::platform::PollEvents(window)) {
        float time = (float)(rev::platform::GetTime() - start_time);
        
        // Update music
        float audio[1024];
        rev::xm::Update(music, audio, 512);
        // TODO: Feed to audio output
        
        // Render
        glClear(GL_COLOR_BUFFER_BIT);
        rev::shader::Use(shader);
        rev::shader::SetFloat(shader, u_time, time);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        rev::platform::SwapBuffers(window);
        
        if (rev::platform::IsKeyPressed(window, VK_ESCAPE)) break;
    }
    
    // Cleanup
    rev::xm::DestroyPlayer(music);
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyWindow(window);
    
    return 0;
}
```

**Result**: ~80KB executable (with all libraries)

---

### Editor Application

```cpp
// editor_main.cpp
#include "rev_platform.h"
#include "rev_editor.h"

int main() {
    // Platform
    rev::platform::WindowConfig config;
    config.fullscreen = false;
    config.width = 1600;
    config.height = 900;
    config.title = "Scene Editor";
    auto* window = rev::platform::CreateWindow(config);
    rev::platform::LoadGLFunctions();
    
    // Editor
    auto* editor = rev::editor::CreateEditor(window);
    rev::editor::LoadProject(editor, "project.json");
    
    // Frame loop
    while (!window->should_close && rev::platform::PollEvents(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        
        rev::editor::BeginFrame(editor);
        rev::editor::RenderUI(editor);
        rev::editor::EndFrame(editor);
        
        rev::platform::SwapBuffers(window);
    }
    
    // Cleanup
    rev::editor::DestroyEditor(editor);
    rev::platform::DestroyWindow(window);
    
    return 0;
}
```

**Result**: ~250KB executable (with ImGui)

---

## Library Packaging

### Directory Structure

```
revision_libs/
├── CMakeLists.txt           # Top-level build
├── README.md                # Library catalog
├── LICENSE                  # MIT or public domain
│
├── rev_platform/
│   ├── include/
│   │   └── rev_platform.h
│   └── src/
│       ├── platform_win32.cpp
│       ├── platform_timing.cpp
│       └── platform_gl_loader.cpp
│
├── rev_xm/
│   ├── include/
│   │   └── rev_xm.h
│   ├── src/
│   │   └── xm_player.cpp
│   └── third_party/
│       └── libxm/           # Public domain XM decoder
│
├── rev_mesh/
│   ├── include/
│   │   └── rev_mesh.h
│   └── src/
│       ├── mesh_loader_meshbin.cpp
│       ├── mesh_loader_obj.cpp
│       └── mesh_renderer.cpp
│
├── rev_shader/
│   ├── include/
│   │   └── rev_shader.h
│   └── src/
│       └── shader.cpp
│
├── rev_curve/
│   ├── include/
│   │   └── rev_curve.h
│   └── src/
│       └── curve.cpp
│
├── rev_sequence/
│   ├── include/
│   │   └── rev_sequence.h
│   └── src/
│       └── sequence.cpp
│
└── rev_editor/
    ├── include/
    │   └── rev_editor.h
    ├── src/
    │   ├── editor_context.cpp
    │   ├── editor_timeline.cpp
    │   ├── editor_curve.cpp
    │   ├── editor_shader_modal.cpp
    │   ├── editor_export.cpp
    │   └── editor_build.cpp
    └── third_party/
        └── imgui/           # MIT license
```

### Top-Level CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(RevisionLibs VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(REV_BUILD_TESTS "Build test executables" ON)
option(REV_BUILD_EXAMPLES "Build example projects" ON)
option(REV_BUILD_EDITOR "Build editor library (requires ImGui)" ON)

# Libraries (always built)
add_subdirectory(rev_platform)
add_subdirectory(rev_xm)
add_subdirectory(rev_mesh)
add_subdirectory(rev_shader)
add_subdirectory(rev_curve)
add_subdirectory(rev_sequence)

# Editor (optional)
if(REV_BUILD_EDITOR)
    add_subdirectory(rev_editor)
endif()

# Tests
if(REV_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Examples
if(REV_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
```

---

## Usage in New Projects

### As Git Submodule

```bash
cd my_intro_project
git submodule add https://github.com/yourname/revision_libs.git libs
git submodule update --init --recursive
```

**CMakeLists.txt**:
```cmake
cmake_minimum_required(VERSION 3.20)
project(MyIntro)

# Add revision libs
add_subdirectory(libs)

# Runtime
add_executable(intro src/main.cpp)
target_link_libraries(intro PRIVATE 
    rev_platform 
    rev_xm 
    rev_shader 
    rev_sequence
)

# Editor (separate executable)
if(REV_BUILD_EDITOR)
    add_executable(editor src/editor_main.cpp)
    target_link_libraries(editor PRIVATE 
        rev_platform 
        rev_editor 
        rev_sequence 
        rev_curve
    )
endif()
```

### As Packaged Release

Download `revision_libs_v1.0.zip` → extract to `libs/`

Same CMake integration as submodule.

---

## Benefits Over Python Editor

### 1. Single Language
- No Python installation required
- No subprocess communication (editor → build → runtime)
- Easier debugging (all C++)

### 2. Better Performance
- ImGui renders at 60+ FPS, even with complex timelines
- No tkinter lag on large projects
- Real-time preview rendering possible

### 3. Native Integration
- Editor can directly manipulate runtime data structures
- Live shader editing (recompile and reload without restart)
- Embedded preview viewport (render intro inside editor)

### 4. Smaller Distribution
- Editor.exe = 250KB vs. Python + tkinter + deps = 50+ MB
- Single executable, no script files

### 5. Better Portability
- Same codebase, same tools, same conventions
- Easy to extend with C++ libraries (no FFI/ctypes)

---

## Lessons Learned from Python Version

### What Worked
✅ Monolithic editor (searchability)  
✅ JSON project format  
✅ Separate export step (runtime data different from project data)  
✅ "Do It All" workflow (save → export → build → run)  
✅ Curve system (powerful, flexible)  
✅ Shader preset library  

### What to Improve
❌ Python/C++ boundary → **All C++ now**  
❌ tkinter performance → **ImGui (60 FPS)**  
❌ Subprocess build integration → **Direct CMake API or simpler shell exec**  
❌ Manual GL function loading scattered → **Centralized in rev_platform**  
❌ Large static globals → **Stack/heap discipline in libraries**  
❌ Non-modular code → **Libraries with clean APIs**  

---

## Size Budget

**Intro Runtime** (all libraries, no editor):
- rev_platform: ~15 KB
- rev_xm: ~30 KB
- rev_shader: ~10 KB
- rev_sequence: ~12 KB
- rev_curve: ~8 KB
- rev_mesh: ~20 KB (optional)
- **Total: ~95 KB** (before compression)

**Editor** (with ImGui):
- All runtime libs: ~95 KB
- rev_editor: ~80 KB
- Dear ImGui: ~70 KB
- **Total: ~245 KB** (before compression)

**After kkrunchy/Crinkler**:
- Intro: ~25-35 KB (3-4x compression typical)
- Editor: Not size-critical, ships uncompressed

---

## Next Steps

1. **Implement rev_platform** - Foundation for everything
2. **Implement rev_shader** - Needed for both runtime and editor
3. **Implement rev_xm** - Audio playback
4. **Implement rev_sequence + rev_curve** - Timeline/animation
5. **Implement rev_editor** - ImGui-based scene authoring
6. **Port existing shaders** - 38+ GLSL scenes to new runtime
7. **Document workflows** - Update FROM_SCRATCH.md for C++ editor

---

**Last Updated**: May 30, 2026  
**Version**: 1.0  
**Status**: Design document (not yet implemented)
