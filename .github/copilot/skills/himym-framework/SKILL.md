---
name: HiMYM Framework
description: Knowledge base for the HiMYM (How I Met Your Mother) demoscene framework architecture, libraries, and integration
---

# HiMYM Framework Skill

Comprehensive knowledge of the HiMYM demoscene framework architecture, library APIs, and integration patterns.

## Framework Overview

**HiMYM** is a C++17 demoscene framework for creating 4KB-64KB intros on Windows.

### Target Specifications
- **Language**: C++17 (MSVC, GCC, Clang)
- **Platform**: Windows 10/11
- **Graphics**: OpenGL 3.3 Core Profile
- **Size**: 4KB - 64KB compressed executables
- **Build**: CMake 3.20+, Visual Studio 2022

## Library Architecture

### 8 Modular Static Libraries

```
revision_libs/
├── rev_runtime/      # Shared cue structs, parsers, GDI+ helpers, Mat4 math (SOURCE OF TRUTH)
├── rev_platform/     # Window creation, OpenGL context, input
├── rev_shader/       # GLSL shader compilation and management
├── rev_xm/           # XM/MOD music playback (libxm-windows)
├── rev_curve/        # Bézier curve evaluation
├── rev_sequence/     # Timeline sequencing for music sync
├── rev_editor/       # ImGui-based visual authoring tools
└── rev_mesh/         # 3D procedural mesh generation and rendering (VAO/VBO/IBO)
```

**`rev_runtime` is the shared source of truth** for all cue structs (`ImageCue`, `TextCue`, `MusicCue`, `MeshCue`) and their file parsers (`LoadImageCue`, `LoadTextCue`, `LoadMusicCue`, `LoadMeshCue`), plus Mat4 matrix math for 3D rendering.

### Dependency Graph

```
editor_app ──→ rev_editor ──→ rev_runtime, rev_mesh, rev_shader, rev_xm, rev_platform
minimal_intro ──────────────→ rev_runtime, rev_mesh, rev_shader, rev_xm, rev_platform
rev_editor ──→ rev_runtime (shared structs), rev_mesh (3D preview), rev_shader, rev_pack
```

## Cue System

The cue system is the core data flow between the editor and runtime:

| Cue Type | Struct | Fields | cues.txt section |
|----------|--------|--------|-----------------|
| ImageCue | `rev_runtime.h` | 14 fields (key, path, x/y/scale/opacity, timing, effect, fades, layer) | `[image_cues]` |
| TextCue  | `rev_runtime.h` | 16 fields (text, font, x/y/size/color, timing, effect, fades, layer) | `[text_cues]` |
| MusicCue | `rev_runtime.h` | 4 fields (key, path, cue_start, cue_end) | `[music_cues]` |
| MeshCue  | `rev_runtime.h` | 25 fields (key, mesh_type, pos/rot/scale, color, size, param, timing, fades, layer) | `[mesh_cues]` |

**Rule**: never define these structs anywhere except `rev_runtime.h`. Both `rev_editor.h` and `minimal_intro/main.cpp` use them via `using` declarations.

## Library APIs

### rev_platform

**Purpose**: Platform abstraction (window, OpenGL context, input)

```cpp
#include "rev_platform.h"

namespace rev::platform
{
    // Window creation
    bool CreateWindow(int width, int height, const char* title);
    void DestroyWindow();
    bool ShouldClose();
    void PollEvents();
    void SwapBuffers();
    
    // OpenGL context
    bool InitOpenGL();
    
    // Input
    bool IsKeyPressed(int key);
    void GetMousePosition(float* x, float* y);
    
    // Timing
    float GetTime();  // Seconds since start
}
```

**Key Features**:
- Minimal Win32 window creation
- OpenGL 3.3 context setup
- Manual function loading via `wglGetProcAddress`
- Keyboard/mouse input handling

### rev_shader

**Purpose**: GLSL shader compilation and program management

```cpp
#include "rev_shader.h"

namespace rev::shader
{
    // Shader compilation
    uint32_t CreateProgram(const char* vs_source, const char* fs_source);
    void DestroyProgram(uint32_t program);
    
    // Uniform setters
    void SetUniform1f(uint32_t program, const char* name, float value);
    void SetUniform2f(uint32_t program, const char* name, float x, float y);
    void SetUniform3f(uint32_t program, const char* name, float x, float y, float z);
    void SetUniform4f(uint32_t program, const char* name, float x, float y, float z, float w);
    void SetUniformMatrix4fv(uint32_t program, const char* name, const float* matrix);
    
    // Program binding
    void UseProgram(uint32_t program);
}
```

**Key Features**:
- Automatic vertex/fragment shader compilation
- Error reporting with shader logs
- Uniform location caching
- Size-optimized shader minification

### rev_xm

**Purpose**: XM/MOD music playback using libxm-windows

```cpp
#include "rev_xm.h"

namespace rev::xm
{
    struct Player;
    
    // Player lifecycle
    Player* CreatePlayer(const unsigned char* xm_data, size_t xm_size, uint32_t sample_rate);
    void DestroyPlayer(Player* player);
    
    // Playback
    void Update(Player* player, float* audio_buffer, uint32_t frame_count);
    void SetPosition(Player* player, uint8_t pattern_index, uint8_t row_index);
    int GetPosition(Player* player);
    
    // Module info
    bool IsFinished(Player* player);
    int GetPatternCount(Player* player);
    int GetChannelCount(Player* player);
    float GetDuration(Player* player);  // Seconds
}
```

**Key Features**:
- C89-compatible libxm-windows integration
- Support for XM, MOD, S3M formats
- Pattern-based synchronization
- Low CPU overhead (~1-2%)

**Note**: Uses `libxm-windows` fork (C89) because original `libxm` requires C23 `stdbit.h` not available in any compiler.

### rev_curve

**Purpose**: Bézier curve evaluation for animations

```cpp
#include "rev_curve.h"

namespace rev::curve
{
    struct ControlPoint { float x, y; };
    
    struct Curve {
        ControlPoint* control_points;
        uint32_t control_point_count;
    };
    
    // Curve creation
    Curve* CreateCurve(uint32_t max_control_points);
    void DestroyCurve(Curve* curve);
    
    // Control point manipulation
    void AddControlPoint(Curve* curve, float x, float y);
    void SetControlPoint(Curve* curve, uint32_t index, float x, float y);
    
    // Evaluation
    ControlPoint Evaluate(const Curve* curve, float t);  // t in [0, 1]
}
```

**Key Features**:
- Cubic Bézier curve interpolation
- Dynamic control point editing
- Smooth animation curves

### rev_sequence

**Purpose**: Timeline sequencing and cue management

```cpp
#include "rev_sequence.h"

namespace rev::sequence
{
    struct Cue {
        float time;      // Time in seconds
        const char* name;
        bool triggered;
    };
    
    struct Timeline {
        Cue* cues;
        uint32_t cue_count;
        float current_time;
        float duration;
    };
    
    // Timeline creation
    Timeline* CreateTimeline(float duration, uint32_t max_cues);
    void DestroyTimeline(Timeline* timeline);
    
    // Cue management
    void AddCue(Timeline* timeline, float time, const char* name);
    void Update(Timeline* timeline, float dt);
    
    // Querying
    Cue* GetNextCue(Timeline* timeline);
    bool IsCueTriggered(const Timeline* timeline, const char* name);
}
```

**Key Features**:
- Music-to-visual synchronization
- Event triggering at specific times
- Cue-based animation system

### rev_editor

**Purpose**: ImGui-based visual authoring tools

```cpp
#include "rev_editor.h"

namespace rev::editor
{
    // Initialization
    bool Init(void* hwnd);
    void Shutdown();
    
    // Frame management
    void BeginFrame();
    void EndFrame();
    
    // UI windows
    void ShowTimeline(float* current_time, float duration);
    void ShowCurveEditor(rev::curve::Curve* curve);
    void ShowSceneGraph();
    void ShowProperties();
}
```

**Key Features**:
- Dear ImGui (Docking branch) integration
- Timeline editor with playback controls
- Curve editor with draggable control points
- Scene graph hierarchical display
- Property inspector

**Dependencies**: ImGui (included in third_party/)

### rev_mesh

**Purpose**: 3D mesh rendering with VAO/VBO/IBO

```cpp
#include "rev_mesh.h"

namespace rev::mesh
{
    struct Vertex {
        float pos[3];
        float normal[3];
        float uv[2];
    };
    
    struct Mesh {
        uint32_t vbo, ibo, vao;
        Vertex* vertices;
        uint32_t* indices;
        uint32_t vertex_count;
        uint32_t index_count;
        uint32_t max_vertices;
        uint32_t max_indices;
    };
    
    // Mesh lifecycle
    Mesh* CreateMesh(uint32_t max_vertices, uint32_t max_indices);
    void DestroyMesh(Mesh* mesh);
    
    // Mesh building
    void SetVertex(Mesh* mesh, uint32_t index, const Vertex& vertex);
    void SetIndex(Mesh* mesh, uint32_t index, uint32_t value);
    void UploadToGPU(Mesh* mesh);
    
    // Rendering
    void Render(const Mesh* mesh);
    
    // Procedural geometry
    Mesh* CreateCube(float size);
    Mesh* CreateSphere(float radius, uint32_t segments, uint32_t rings);
    Mesh* CreatePlane(float width, float height, uint32_t subdivisions);
    Mesh* CreateTorus(float major_radius, float minor_radius, uint32_t major_segments, uint32_t minor_segments);
}
```

**Key Features**:
- Indexed rendering (VAO/VBO/IBO)
- Vertex format: position, normal, UV
- Procedural geometry generation
- Phong lighting support

## Integration Patterns

### Minimal Intro Template

```cpp
#include "rev_platform.h"
#include "rev_shader.h"

int main()
{
    // Create window and OpenGL context
    rev::platform::CreateWindow(1280, 720, "Intro");
    rev::platform::InitOpenGL();
    
    // Load shader
    const char* vs = "#version 330\nvoid main(){gl_Position=vec4(0);}";
    const char* fs = "#version 330\nout vec4 c;void main(){c=vec4(1,0,0,1);}";
    uint32_t shader = rev::shader::CreateProgram(vs, fs);
    
    // Main loop
    while (!rev::platform::ShouldClose()) {
        float time = rev::platform::GetTime();
        
        glClear(GL_COLOR_BUFFER_BIT);
        rev::shader::UseProgram(shader);
        rev::shader::SetUniform1f(shader, "u_time", time);
        
        // Draw fullscreen quad or raymarching
        
        rev::platform::SwapBuffers();
        rev::platform::PollEvents();
    }
    
    // Cleanup
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyWindow();
    
    return 0;
}
```

### 3D Mesh Rendering

```cpp
#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_mesh.h"

int main()
{
    rev::platform::CreateWindow(1280, 720, "Mesh Demo");
    rev::platform::InitOpenGL();
    
    // Create shader with MVP matrix
    const char* vs = R"(
        #version 330
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;
        uniform mat4 u_mvp;
        out vec3 v_normal;
        void main() {
            gl_Position = u_mvp * vec4(aPos, 1.0);
            v_normal = aNormal;
        }
    )";
    
    const char* fs = R"(
        #version 330
        in vec3 v_normal;
        out vec4 fragColor;
        void main() {
            vec3 light = normalize(vec3(1, 1, 1));
            float diff = max(dot(v_normal, light), 0.0);
            fragColor = vec4(vec3(diff), 1.0);
        }
    )";
    
    uint32_t shader = rev::shader::CreateProgram(vs, fs);
    rev::mesh::Mesh* cube = rev::mesh::CreateCube(1.0f);
    
    glEnable(GL_DEPTH_TEST);
    
    while (!rev::platform::ShouldClose()) {
        float time = rev::platform::GetTime();
        
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Build MVP matrix
        float mvp[16];
        // ... matrix math ...
        
        rev::shader::UseProgram(shader);
        rev::shader::SetUniformMatrix4fv(shader, "u_mvp", mvp);
        rev::mesh::Render(cube);
        
        rev::platform::SwapBuffers();
        rev::platform::PollEvents();
    }
    
    rev::mesh::DestroyMesh(cube);
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyWindow();
    
    return 0;
}
```

### Music-Synchronized Intro

```cpp
#include "rev_platform.h"
#include "rev_shader.h"
#include "rev_xm.h"
#include "rev_sequence.h"

// Embedded music data
extern const unsigned char music_xm[];
extern const size_t music_xm_len;

int main()
{
    rev::platform::CreateWindow(1280, 720, "Music Demo");
    rev::platform::InitOpenGL();
    
    // Load music
    auto* player = rev::xm::CreatePlayer(music_xm, music_xm_len, 48000);
    
    // Setup timeline
    auto* timeline = rev::sequence::CreateTimeline(120.0f, 10);
    rev::sequence::AddCue(timeline, 0.0f, "intro");
    rev::sequence::AddCue(timeline, 16.0f, "drop");
    rev::sequence::AddCue(timeline, 32.0f, "breakdown");
    rev::sequence::AddCue(timeline, 64.0f, "finale");
    
    uint32_t shader = rev::shader::CreateProgram(vs, fs);
    
    float last_time = 0.0f;
    while (!rev::platform::ShouldClose()) {
        float time = rev::platform::GetTime();
        float dt = time - last_time;
        last_time = time;
        
        // Update audio
        float audio_buffer[1024];
        rev::xm::Update(player, audio_buffer, 512);
        // ... submit to audio hardware ...
        
        // Update timeline
        rev::sequence::Update(timeline, dt);
        
        // Check cues
        if (rev::sequence::IsCueTriggered(timeline, "drop")) {
            // Trigger visual effect
        }
        
        glClear(GL_COLOR_BUFFER_BIT);
        rev::shader::UseProgram(shader);
        rev::shader::SetUniform1f(shader, "u_time", time);
        // ... render ...
        
        rev::platform::SwapBuffers();
        rev::platform::PollEvents();
    }
    
    rev::sequence::DestroyTimeline(timeline);
    rev::xm::DestroyPlayer(player);
    rev::shader::DestroyProgram(shader);
    rev::platform::DestroyWindow();
    
    return 0;
}
```

### Editor Application

```cpp
#include "rev_platform.h"
#include "rev_editor.h"
#include "rev_curve.h"

int main()
{
    rev::platform::CreateWindow(1280, 720, "Editor");
    rev::platform::InitOpenGL();
    
    // Initialize editor
    void* hwnd = rev::platform::GetWindowHandle();
    rev::editor::Init(hwnd);
    
    auto* curve = rev::curve::CreateCurve(10);
    rev::curve::AddControlPoint(curve, 0.0f, 0.0f);
    rev::curve::AddControlPoint(curve, 0.5f, 1.0f);
    rev::curve::AddControlPoint(curve, 1.0f, 0.0f);
    
    float current_time = 0.0f;
    float duration = 120.0f;
    
    while (!rev::platform::ShouldClose()) {
        rev::editor::BeginFrame();
        
        // Show UI windows
        rev::editor::ShowTimeline(&current_time, duration);
        rev::editor::ShowCurveEditor(curve);
        rev::editor::ShowSceneGraph();
        
        rev::editor::EndFrame();
        
        rev::platform::SwapBuffers();
        rev::platform::PollEvents();
    }
    
    rev::curve::DestroyCurve(curve);
    rev::editor::Shutdown();
    rev::platform::DestroyWindow();
    
    return 0;
}
```

## Build System

### CMake Structure

```cmake
# Root CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(HiMYM VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add libraries
add_subdirectory(revision_libs/rev_platform)
add_subdirectory(revision_libs/rev_shader)
add_subdirectory(revision_libs/rev_xm)
add_subdirectory(revision_libs/rev_curve)
add_subdirectory(revision_libs/rev_sequence)
add_subdirectory(revision_libs/rev_editor)
add_subdirectory(revision_libs/rev_mesh)

# Add examples
add_subdirectory(examples/minimal_intro)
add_subdirectory(examples/mesh_demo)
add_subdirectory(examples/editor_app)
```

### Build Commands

```bash
# Configure (Visual Studio 2022)
cmake -B build -G "Visual Studio 17 2022"

# Build (Release for size optimization)
cmake --build build --config Release

# Build specific target
cmake --build build --config Release --target minimal_intro

# Run
.\build\bin\Release\minimal_intro.exe
```

## Size Optimization

### Compiler Flags (MSVC)

```cmake
target_compile_options(${TARGET} PRIVATE
    /O1          # Optimize for size
    /GS-         # No buffer security check
)

target_link_options(${TARGET} PRIVATE
    /GL          # Whole program optimization
    /LTCG        # Link-time code generation
    /ENTRY:mainCRTStartup
    /SUBSYSTEM:WINDOWS
)
```

### Size Targets

- **Minimal intro**: ~16 KB (basic shader, no music)
- **Animated intro**: ~20 KB (shader + animation)
- **Demo intro**: ~24 KB (shader + simple music)
- **Full intro**: 30-64 KB (complex visuals + music)

## Common Issues

### Issue: glDrawElements returns nullptr
**Cause**: Core OpenGL 1.1 functions loaded via `wglGetProcAddress`
**Solution**: Use `#include <gl/gl.h>` for core functions

### Issue: libxm requires C23 stdbit.h
**Cause**: Original libxm uses C23 features
**Solution**: Use libxm-windows fork (C89-compatible)

### Issue: ImGui DockSpaceOverViewport signature mismatch
**Solution**: Use `ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport())`

### Issue: Objects not visible (black screen)
**Checklist**:
1. Verify shader compilation
2. Check camera position/projection
3. Enable depth test: `glEnable(GL_DEPTH_TEST)`
4. Verify vertex attribute layout
5. Check clear color

## Examples Structure

```
examples/
├── minimal_intro/      # 16 KB: Basic shader demo
├── animated_intro/     # 20 KB: Shader + curves
├── demo_intro/         # 24 KB: Shader + music
├── editor_app/         # 526 KB: ImGui editor
└── mesh_demo/          # 24 KB: 3D mesh rendering
```

## When to Use Which Library

- **rev_runtime**: Always (cue structs, parsers, GDI+ helpers, Mat4 math — source of truth)
- **rev_platform**: Always (window + OpenGL context)
- **rev_shader**: Always (unless raw OpenGL)
- **rev_xm**: When you need music
- **rev_curve**: For smooth animations
- **rev_sequence**: For music-visual sync
- **rev_editor**: During authoring phase only
- **rev_mesh**: For 3D geometry (cube/sphere/plane/torus) — linked into both editor and runtime

## Specialized Skills (use these for focused work)

- **`Revision Codebase Map`**: Project layout, file ownership, struct relationships, all cue types and cues.txt format
- **`Revision Runtime Core`**: `examples/minimal_intro/main.cpp`, `rev_runtime/` changes, cue loaders, Mat4 math, packed build
- **`Scene Block Editor`**: `rev_editor/`, editor_app, cue UI/export/import, pack-build-run workflow
- **`Shader Authoring`**: GLSL shaders, rev_shader, Phong shader contract, wglGetProcAddress patterns
- **`Revision Build Validation`**: CMake build commands, rebuild targets, stale binary detection
- **`Revision Director`**: Cross-domain routing (spans runtime + editor + shader + mesh)


## Response Guidelines

When helping with HiMYM framework:
1. **Identify libraries needed** for the task
2. **Show complete integration** (includes, CMake, usage)
3. **Consider size impact** (especially for intros)
4. **Provide working examples** (tested patterns)
5. **Mention common pitfalls** (OpenGL loading, libxm-windows, etc.)

Focus on demoscene workflows: fast iteration, size optimization, visual quality.
