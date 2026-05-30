# Building a Modern Intro/Demo Framework From Scratch

**Complete guide to create a modular, C++-based intro/demo runtime and editor from zero**

This document captures lessons learned from HowIMetYourMod and applies best practices to a fresh, modern architecture. All components are C++, modular, and reusable across projects.

---

## Table of Contents

1. [Project Vision & Requirements](#project-vision--requirements)
2. [Technology Stack (Revised)](#technology-stack-revised)
3. [Library Architecture](#library-architecture)
4. [Phase 1: Core Libraries](#phase-1-core-libraries)
5. [Phase 2: Runtime Foundation](#phase-2-runtime-foundation)
6. [Phase 3: Shader System](#phase-3-shader-system)
7. [Phase 4: C++ Editor with ImGui](#phase-4-c-editor-with-imgui)
8. [Phase 5: Animation & Timeline](#phase-5-animation--timeline)
9. [Phase 6: Optional 3D Rendering](#phase-6-optional-3d-rendering)
10. [Phase 7: Build & Optimization](#phase-7-build--optimization)
11. [Architecture Patterns](#architecture-patterns)
12. [Design Principles](#design-principles)
13. [Lessons Learned](#lessons-learned)

---

## Project Vision & Requirements

### Core Mission
Build a **modular, modern, C++-based** intro/demo framework with:
- **Reusable libraries** for platform, audio, 3D, shaders, animation
- **Native C++ editor** with ImGui (no Python dependency)
- **Scene-based timeline** authoring with real-time preview
- **GLSL shader pipeline** with curve-driven animation
- **Optional 3D mesh rendering** (modular, not compile-time flag)
- **Competition-ready**: Deterministic, size-conscious, ESC to exit

### Key Improvements Over Previous Design
✅ **All C++** - Single language, simpler debugging, native performance  
✅ **Modular Libraries** - Reusable across projects, clean APIs, zero interdependencies  
✅ **ImGui Editor** - 60 FPS UI, live preview, integrated debugging  
✅ **Better Separation** - Clear library boundaries, explicit contracts  
✅ **Size Discipline** - Each library tracks its footprint, no bloat  

### Target Constraints
- **Platform**: Windows 11 64-bit (libraries could port to Linux/Mac later)
- **Graphics**: OpenGL 3.3 Core Profile (forward-compatible)
- **Audio**: XM module playback (libxm, static link)
- **Build**: CMake + Visual Studio 2022 or Clang
- **Assets**: Embed for runtime, filesystem for editor

### Non-Goals
- Cross-platform out of the gate (but design for it)
- Plugin systems or dynamic loading (static linking only)
- Engine-style complexity (keep it simple)
- Scripting languages (C++ is the script)

---

## Technology Stack (Revised)

### Runtime (C++)
```
Language:     C++17 (or C++20 for better constexpr)
Platform API: Win32 (window, timing, input) - rev_platform library
Graphics:     OpenGL 3.3 Core/Compatibility - rev_shader library
Audio:        libxm (XM decoder, public domain) - rev_xm library
3D:           Custom mesh loader + renderer - rev_mesh library
Animation:    Curve evaluator - rev_curve library
Sequencing:   Timeline + cue system - rev_sequence library
Build:        CMake 3.20+, MSVC 2022 or Clang
Compression:  kkrunchy/Crinkler (manual, post-build)
```

### Editor (C++)
```
Language:     C++17
GUI:          Dear ImGui (MIT license) - rev_editor library
Integration:  Direct C++ API calls (no subprocess)
Data:         JSON (nlohmann/json or custom parser)
Build:        Same CMake project, separate executable
Preview:      Embedded OpenGL viewport (live rendering)
```

### Library Stack
```
rev_platform  → Win32 window, WGL, timing, input (~15 KB)
rev_xm        → XM playback (libxm wrapper) (~30 KB)
rev_shader    → GLSL compilation, uniforms (~10 KB)
rev_curve     → Time-value curves with easing (~8 KB)
rev_sequence  → Timeline, cues, transitions (~12 KB)
rev_mesh      → OBJ/meshbin loader + renderer (~20 KB)
rev_editor    → ImGui-based scene authoring (~150 KB with ImGui)
```

**Total Runtime**: ~95 KB (without 3D), ~115 KB (with 3D)  
**Total Editor**: ~245 KB (not size-critical)

---

## Library Architecture

### Design Philosophy

**Core Principle**: Each library is **independent, focused, and reusable**

```
revision_libs/
├── rev_platform/    # Win32 + WGL + timing + input (foundation)
├── rev_xm/          # XM music playback
├── rev_shader/      # GLSL compilation + uniforms
├── rev_curve/       # Animation curves
├── rev_sequence/    # Timeline + cues
├── rev_mesh/        # 3D mesh loading + rendering
└── rev_editor/      # ImGui-based authoring tool
```

### Library Contracts

**No circular dependencies**: Libraries never depend on each other except:
- All depend on standard library
- rev_editor may use all runtime libraries for preview
- Runtime only links what it needs (rev_platform + rev_shader + rev_xm minimum)

**Clear APIs**: Each library exposes C++ namespace with:
- Create/Destroy lifecycle functions
- Minimal public structs (opaque pointers when possible)
- No global state (context passed explicitly)

**Size conscious**: Each library tracks compiled size, optimizes for compression.

### Integration Strategy

**As Git Submodule**:
```bash
git submodule add https://github.com/yourname/revision_libs.git libs
```

**CMake Integration**:
```cmake
add_subdirectory(libs)
target_link_libraries(my_intro PRIVATE rev_platform rev_xm rev_shader)
```

**See [LIBRARY_DESIGN.md](LIBRARY_DESIGN.md) for complete API reference**

---

## Phase 1: Core Libraries

### Step 1.1: Win32 Window Bootstrap

**Goal**: Create a fullscreen window with WGL OpenGL context.

**Files to create**:
- `src/platform/window_win32.h` - Window creation, message pump
- `src/platform/window_win32.cc` - Implementation
- `src/app/main_win32.cc` - Entry point, frame loop

**Key patterns**:
```cpp
// main_win32.cc
int WINAPI WinMain(...) {
    Window window = CreateFullscreenWindow(1920, 1080);
    GLContext gl = InitWGL(window);
    
    while (!ShouldExit()) {
        PumpMessages();
        float time = GetTime();
        Render(time);
        SwapBuffers(gl);
    }
    
    Cleanup();
    return 0;
}
```

**Critical details**:
- Use `WGL_ARB_create_context` for GL 3.3 Core/Compatibility
- Manually load GL function pointers (no GLEW)
- Implement ESC and Alt+F4 exit
- Hide cursor only when window is active/foreground
- Restore cursor on exit and focus loss

### Step 1.2: Shader Pipeline Bootstrap

**Goal**: Load and compile fullscreen fragment shader.

**Files**:
- `src/renderer/shader.h` - Shader compilation, uniform management
- `src/renderer/shader.cc` - Implementation
- `assets/shader/fragment.glsl` - Initial shader (will be generated)

**Pattern**:
```cpp
// Fullscreen quad rendering (immediate mode for size)
glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(+1, -1);
    glTexCoord2f(1, 1); glVertex2f(+1, +1);
    glTexCoord2f(0, 1); glVertex2f(-1, +1);
glEnd();
```

### Step 1.3: Timing & Sequence

**Goal**: Deterministic time, music sync, cue-driven playback.

**Files**:
- `src/sequence/timeline.h` - Scene timing, transitions
- `src/sequence/timeline.cc` - Implementation
- `src/audio/xm_player.h` - XM module playback

**Key concepts**:
- Cue start/end/fade_in/fade_out for all assets
- Music cues are *optional* (silent startup if no music)
- Scene transitions: crossfade, cut, custom blend curves

---

## Phase 2: Shader System

### Step 2.1: Scene-Based Dispatch

**Goal**: Single fragment shader with 38+ scene branches.

**Structure**:
```glsl
// fragment.glsl (generated by CMake)
#version 330 compatibility

uniform float u_time;
uniform vec2 u_resolution;
uniform vec4 u_dyn;  // dyn0, dyn1, dyn2, dyn3
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_speed;
uniform float u_intensity;
uniform float u_warp;
uniform int u_scene_id;

// Shared helpers
vec3 palette(float t) { ... }
float vignette(vec2 uv) { ... }

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution;
    float st = u_time * u_speed;
    vec4 dyn = u_dyn;
    vec3 c_low = u_palette_low;
    vec3 c_mid = u_palette_mid;
    vec3 c_high = u_palette_high;
    float ins = u_intensity;
    float warp = u_warp;
    
    vec3 col = vec3(0.0);
    int id = u_scene_id;
    
    if (id == 0) {
        // Scene 0: Plasma
        col = plasma_effect(uv, st, dyn);
    }
    else if (id == 1) {
        // Scene 1: Tunnel
        col = tunnel_effect(uv, st, dyn);
    }
    // ... 36 more scenes
    
    gl_FragColor = vec4(col, 1.0);
}
```

### Step 2.2: Split Shader Workflow

**Goal**: Author shaders in separate files, concatenate at build time.

**CMake snippet**:
```cmake
file(GLOB SHADER_SCENES "assets/shader/scenes/*.glsl")
list(SORT SHADER_SCENES)

set(FRAGMENT_GLSL "")
file(READ "assets/shader/shader_common.glsl" SHADER_COMMON)
string(APPEND FRAGMENT_GLSL "${SHADER_COMMON}\n")

foreach(SCENE_FILE ${SHADER_SCENES})
    file(READ "${SCENE_FILE}" SCENE_CODE)
    # Strip @editor_* metadata lines
    string(REGEX REPLACE "// @[^\n]*\n" "" SCENE_CODE "${SCENE_CODE}")
    string(APPEND FRAGMENT_GLSL "${SCENE_CODE}\n")
endforeach()

file(READ "assets/shader/shader_footer.glsl" SHADER_FOOTER)
string(APPEND FRAGMENT_GLSL "${SHADER_FOOTER}\n")

file(WRITE "assets/shader/fragment.glsl" "${FRAGMENT_GLSL}")
```

**Shader file naming**: `<id>_<name>.glsl` (e.g., `00_plasma.glsl`, `36_rasterbars_classic.glsl`)

**Metadata pattern**:
```glsl
// @shader_id 36
// @name rasterbars_classic
// @editor_dyn dyn0 | Scroll speed and direction.
// @editor_dyn dyn1 | Bar count and spacing.
```

### Step 2.3: Shader Presets System

**Goal**: Store shader parameters as JSON presets for editor dropdown.

**File**: `assets/shader/presets.json`
```json
{
  "plasma_chill": {
    "shader_scene_id": 0,
    "speed": 0.8,
    "intensity": 1.2,
    "warp": 0.5,
    "palette_low": [0.1, 0.3, 0.8],
    "palette_mid": [0.45, 0.25, 0.7],
    "palette_high": [0.8, 0.2, 0.6]
  }
}
```

---

## Phase 3: Python Editor

### Step 3.1: Project Structure

**Goal**: Scene block timeline with JSON persistence.

**Files**:
- `tools/scene_block_editor.py` - Main editor (8000+ lines, intentionally monolithic)
- `tools/scene_block_editor_*.py` - Helper modules (policy, parsing, actions, state)

**Data model**:
```python
project = {
    "version": 2,
    "metadata": {"title": "How I Met Your Mod", "author": "..."},
    "blocks": [
        {
            "id": 1,
            "type": "scene",
            "name": "Opening",
            "start": 0.0,
            "duration": 10.0,
            "shader_scene_id": 0,
            "palette_low": [0.1, 0.3, 0.8],
            "speed": 1.0,
            "payload": {"shader_cue_start_s": 0.0, ...}
        },
        {"type": "effect", ...},
        {"type": "music", ...}
    ],
    "shader_curves": {...},
    "assets": {...}
}
```

### Step 3.2: Editor UI Layout

**Tkinter structure**:
```
┌─────────────────────────────────────────────────────────┐
│ [Save] [Export] [Build] [Run] [Do It All] ...          │ Top bar
├─────────────────┬───────────────────────────────────────┤
│ Timeline        │ Properties Panel                       │
│ (Listbox)       │ - Scene settings                       │
│                 │ - Shader modal                         │
│ [+ Scene]       │ - Image/Text cues                      │
│ [+ Effect]      │ - Music controls                       │
│ [+ Cue]         │ - 3D stage inspector                   │
│ [Delete]        │                                        │
│ [Edit...]       │ [Shader Curves...]                     │
├─────────────────┼───────────────────────────────────────┤
│ Asset Browser   │ 3D Layout Preview (optional)           │
│ (Workspace)     │                                        │
└─────────────────┴───────────────────────────────────────┘
```

### Step 3.3: Export Pipeline

**Goal**: `project.json` → `assets/cues.txt` (runtime format)

**Export format** (`cues.txt`):
```
[shader_cues]
shader_scene_id|palette_low_r|palette_low_g|palette_low_b|palette_mid_r|...|shader_cue_start_s|shader_cue_end_s|...
0|0.1|0.3|0.8|0.45|0.25|0.7|0.8|0.2|0.6|1.0|1.0|0.5|0.76|0.02|0.04|-0.04|0.0|10.0|0.5|0.5|0|1.0|0|0
1|...

[shader_curves]
target|param|point_count
shader_id:0|speed|3
0.0|0.5|0.0|0.0|linear
0.5|1.2|0.3|0.3|smooth
1.0|0.8|0.0|0.0|linear
shader_id:0|dyn0|2
0.0|0.0|0.0|0.0|linear
1.0|1.0|0.0|0.0|linear

[shader_pipeline]
# order, shader_scene_id, start, end, fade_in, fade_out, implicit_end, layer_role, opacity, blend, layer_order, speed, intensity, warp
0|0|0.0|10.0|0.5|0.5|0|0|1.0|0|0|1.0|1.0|0.5
```

**Critical**: Runtime parses this at startup, no JSON in runtime code.

---

## Phase 4: Curve System

### Step 4.1: Curve Data Structure

**Goal**: Time-value curves with easing for shader parameters.

**Curve format**:
```python
{
  "common": {
    "exposure": {
      "points": [
        {"t": 0.0, "v": 0.0, "in_ease": 0.0, "out_ease": 0.0, "mode": "linear"},
        {"t": 0.5, "v": 1.2, "in_ease": 0.3, "out_ease": 0.3, "mode": "smooth"},
        {"t": 1.0, "v": 0.0, "in_ease": 0.0, "out_ease": 0.0, "mode": "linear"}
      ]
    }
  },
  "shaders": {
    "shader_id:36": {
      "dyn0": {"points": [...]},
      "speed": {"points": [...]}
    }
  }
}
```

**Ease modes**: `linear`, `ease_in`, `ease_out`, `ease_in_out`, `smoothstep`, `hold`

### Step 4.2: Curve Editor UI

**Goal**: Visual curve editing with point manipulation.

**Implementation** (`ShaderCurveEditor` class):
- Canvas-based rendering (tk.Canvas)
- Click to select points, drag to move
- Add/delete points, adjust easing
- Target dropdown: `(common)` or shader-specific
- Parameter dropdown: `speed`, `intensity`, `warp`, `dyn0`-`dyn3`, etc.

**Critical**: Curves are 0.0-centered (neutral = 0.0, range = [-2.0, +2.0])

### Step 4.3: Runtime Curve Evaluation

**C++ pattern**:
```cpp
float EvaluateCurve(const Curve& curve, float t) {
    if (curve.points.empty()) return 0.0f;
    if (t <= curve.points.front().t) return curve.points.front().v;
    if (t >= curve.points.back().t) return curve.points.back().v;
    
    // Binary search for segment
    for (size_t i = 0; i < curve.points.size() - 1; ++i) {
        if (t >= curve.points[i].t && t <= curve.points[i + 1].t) {
            float t0 = curve.points[i].t;
            float t1 = curve.points[i + 1].t;
            float v0 = curve.points[i].v;
            float v1 = curve.points[i + 1].v;
            float alpha = (t - t0) / (t1 - t0);
            
            // Apply easing
            if (curve.points[i].mode == "smooth") {
                alpha = smoothstep(alpha);
            } else if (curve.points[i].mode == "hold") {
                alpha = 0.0f;
            }
            
            return mix(v0, v1, alpha);
        }
    }
    return 0.0f;
}
```

---

## Phase 5: Optional 3D Stage

### Step 5.1: Compile-Time Flag

**CMake option**:
```cmake
option(REV_ENABLE_3D "Enable optional 3D mesh rendering" OFF)

if(REV_ENABLE_3D)
    target_compile_definitions(intro PRIVATE REV_ENABLE_3D=1)
endif()
```

**Runtime guard**:
```cpp
#ifdef REV_ENABLE_3D
    Render3DStage(time, camera, meshes);
#endif
```

### Step 5.2: Meshbin Format

**Goal**: Compact binary mesh format with material slots.

**Format** (v4):
```
Header:
  magic: "MESH" (4 bytes)
  version: 4 (uint32)
  vertex_count: N (uint32)
  index_count: M (uint32)
  material_slot_count: K (uint32)

Vertices: N * 32 bytes
  vec3 position
  vec3 normal
  vec2 texcoord
  vec2 padding

Indices: M * uint32

Material Slots: K slots
  start_index: uint32
  count: uint32
  diffuse_tex: 64 bytes (path or "")
  normal_tex: 64 bytes (path or "")
```

**Converter**: `tools/obj_to_meshbin.py` (OBJ/MTL → meshbin)

### Step 5.3: 3D Rendering

**Pattern**:
```cpp
// Per-frame
glEnable(GL_DEPTH_TEST);
glClear(GL_DEPTH_BUFFER_BIT);

for (auto& mesh : meshes) {
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glVertexPointer(3, GL_FLOAT, 32, (void*)0);
    glNormalPointer(GL_FLOAT, 32, (void*)12);
    glTexCoordPointer(2, GL_FLOAT, 32, (void*)24);
    
    for (auto& slot : mesh.material_slots) {
        if (slot.diffuse_tex) glBindTexture(GL_TEXTURE_2D, slot.diffuse_tex);
        glDrawElements(GL_TRIANGLES, slot.count, GL_UNSIGNED_INT, (void*)(slot.start * 4));
    }
}
```

---

## Phase 6: Build & Optimization

### Step 6.1: Asset Embedding

**CMake pattern**:
```cmake
# Generate embedded_assets.h
set(SHADER_FRAGMENT "${CMAKE_SOURCE_DIR}/assets/shader/fragment.glsl")
file(READ "${SHADER_FRAGMENT}" FRAGMENT_CODE)
string(REPLACE "\\" "\\\\" FRAGMENT_CODE "${FRAGMENT_CODE}")
string(REPLACE "\"" "\\\"" FRAGMENT_CODE "${FRAGMENT_CODE}")

configure_file(
    "${CMAKE_SOURCE_DIR}/src/config/embedded_assets.h.in"
    "${CMAKE_BINARY_DIR}/embedded_assets.h"
    @ONLY
)
```

**Template** (`embedded_assets.h.in`):
```cpp
#pragma once
namespace embedded {
    const char* fragment_shader = R"(@FRAGMENT_CODE@)";
    const char* cues_data = R"(@CUES_DATA@)";
    // ... more assets
}
```

### Step 6.2: Size Optimization

**Compiler flags** (CMakeLists.txt):
```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(intro PRIVATE
        /O1         # Optimize for size
        /GS-        # No buffer security checks
        /GL         # Whole program optimization
    )
    target_link_options(intro PRIVATE
        /LTCG       # Link-time code generation
        /OPT:REF    # Remove unreferenced functions
        /OPT:ICF    # Identical COMDAT folding
        /ENTRY:WinMainCRTStartup
    )
endif()
```

**Manual packing** (post-build):
```
kkrunchy intro.exe --out intro_packed.exe
# or
Crinkler /OUT:intro_packed.exe intro.obj ...
```

---

## Phase 7: Workflow Tools

### Step 7.1: Shader Scene Duplicator

**Tool**: `tools/shader_scene_duplicator.py`

**Purpose**: Fork existing shaders with new ID and preset overrides.

**Usage**:
```bash
python tools/shader_scene_duplicator.py \
    --source 0_plasma.glsl \
    --name plasma_vibrant \
    --speed 1.5 \
    --intensity 1.3
```

**Output**:
- New file: `assets/shader/scenes/39_plasma_vibrant.glsl`
- Updated: `assets/shader/presets.json`

### Step 7.2: Shader ID Finder

**Tool**: `tools/shader_id_finder.py`

**Operations**:
```bash
python tools/shader_id_finder.py --next         # Next available ID
python tools/shader_id_finder.py --list         # All IDs
python tools/shader_id_finder.py --map          # ID → name mapping
python tools/shader_id_finder.py --check 42     # Is ID 42 taken?
python tools/shader_id_finder.py --gaps         # Find unused ID ranges
```

### Step 7.3: Text Baking

**Tool**: `tools/bake_text_to_png.py`

**Purpose**: Render text to PNG atlas for runtime (no TTF dependency).

**Usage**:
```bash
python tools/bake_text_to_png.py \
    --font "Arial" \
    --size 48 \
    --text "HELLO REVISION 2026" \
    --output assets/textures/title.png
```

---

## Architecture Patterns

### Runtime Ownership Model

**Golden rule**: Minimize stack pressure, avoid large static globals.

**Pattern**:
```cpp
// BAD: Large stack allocation
void RenderScene() {
    MeshData meshes[64];  // 64 * sizeof(MeshData) on stack!
    Load3DStage(meshes);
}

// BAD: Large static global
static MeshData g_meshes[64];  // Bloats PE size

// GOOD: Heap allocation, explicit cleanup
class Renderer {
    std::vector<MeshData> meshes_;  // Heap-allocated
public:
    void Init() { meshes_.reserve(16); }
    void Render() { ... }
};
```

### Deterministic Behavior

**Rules**:
1. No random startup variation (fixed seed if randomness needed)
2. Music cues drive timeline, not vice versa
3. Asset loading errors are fatal (no silent fallback)
4. Time is always forward, never reset mid-runtime

### Size Discipline

**Every byte counts** for intro profiles:
- Avoid string literals for error messages
- Prefer integer enums over string comparisons
- Strip debug symbols and metadata
- Favor inline over virtual dispatch
- Use compiler intrinsics for common math

---

## Design Principles

### 1. Competition Reliability First
- ESC must exit immediately (no "are you sure?" dialogs)
- Fullscreen only, no windowed mode toggle
- Deterministic startup (no probing for files/configs)
- No network access, no external runtime dependencies

### 2. Direct Runtime Flow
- No plugin systems, no dynamic module loading
- Explicit subsystems: platform → renderer → sequence → audio
- Frame loop is obvious in `main_win32.cc`
- No hidden state machines or async callbacks

### 3. Maintainability Without Bloat
- Monolithic editor (`scene_block_editor.py`) for searchability
- Helper modules only when complexity demands it
- No speculative abstractions ("we might need this later")
- Keep C++ runtime simple, move complexity to editor

### 4. Size-First Mindset
- Treat each import and dependency as budgeted
- Prefer static data over runtime systems
- Favor compile-time flags over runtime config
- No heavyweight libraries (no JSON parser in runtime)

### 5. Authored Data is Truth
- Runtime never guesses or fills defaults silently
- Editor exports complete, explicit definitions
- No "smart" runtime behavior that editor doesn't control
- Curve system owns animation, not hardcoded constants

---

## AI Agent Setup

### GitHub Copilot Workspace Configuration

**Files to create**:

#### 1. `.github/copilot-instructions.md`
```markdown
# Workspace Routing

- Use skills for repeatable tasks before prompts
- `Revision Runtime Core` for C++ runtime work
- `Scene Block Editor` for Python editor
- `Shader Authoring` for GLSL work
- `Revision Director` for multi-domain tasks

- Specialist agents:
  - `Intro Runtime` for Win32/WGL/sequence/timing
  - `Scene Block Editor` for editor/export flow
  - `Shader Author` for GLSL scenes
  - `Revision Director` for routing

- Keep customization map synchronized
- Align authored semantics across editor and runtime
- Update docs when semantics change
```

#### 2. `AGENTS.md` - Project Guidelines
```markdown
# Project Guidelines

## Scope
- Revision 2026 PC: Demo and Intro profiles
- Windows only
- Shared runtime core, profile-specific paths

## Priorities
1. Competition reliability
2. Size and compression
3. Direct runtime flow
4. Rendering capability
5. Maintainability

## Forbidden Patterns
- No plugins, DLL probing, scripting
- No fake generic renderer API
- No speculative abstractions

## Audio Rules
- XM-only, static decoder
- Cue-driven startup (silent if no music)

## Cursor Rules
- Hide only when active/foreground
- Restore on focus loss and shutdown

## 3D Stage Rules
- Global timing continuous by default
- Per-scene overrides explicit
- Material-slot support lightweight

## Build Discipline
- Release strips debug paths
- Minimize dependencies and imports
- Keep code packer-friendly
- Verify both REV_ENABLE_3D=OFF and ON
```

#### 3. Skills (`.github/skills/*/SKILL.md`)

Create 8 skills:
- `revision-runtime-core` - C++ subsystems
- `revision-editor-authoring` - Editor workflows
- `revision-shader-authoring` - GLSL scenes
- `revision-director` - Multi-domain routing
- `revision-codebase-map` - Ownership orientation
- `python-editor-tooling-map` - Python script ownership
- `python-editor-utilities` - Importer/baker helpers
- `revision-build-validation` - Build/compile checks

#### 4. Agents (`.github/agents/*.agent.md`)

Create 4 agents:
- `intro-runtime.agent.md` - Runtime specialist
- `scene-block-editor.agent.md` - Editor specialist
- `shader-author.agent.md` - Shader specialist
- `revision-director.agent.md` - Router/integrator

#### 5. Instructions (`.github/instructions/*.instructions.md`)

Create file-specific rules:
- `intro_runtime.instructions.md` - `src/**` patterns
- `scene_block_editor.instructions.md` - `tools/scene_block_editor*.py` patterns
- `shader_author.instructions.md` - `assets/shader/**/*.glsl` patterns

---

## Testing & Validation

### Build Verification
```bash
# Base build (no 3D)
cmake -S . -B build
cmake --build build --config Release

# 3D enabled
cmake -S . -B build_3d -DREV_ENABLE_3D=ON
cmake --build build_3d --config Release

# Both must succeed
.\build\Release\intro.exe
.\build_3d\Release\intro.exe
```

### Editor Workflow Test
```bash
# Launch editor
python tools/scene_block_editor.py

# Steps:
1. Create new project
2. Add scene block
3. Open shader modal → pick shader → Apply
4. Click "Do It All" (Save → Export → Build → Run)
5. Verify intro runs for 30 seconds then exits
```

### Shader Authoring Test
```bash
# Duplicate shader
python tools/shader_scene_duplicator.py \
    --source 0_plasma.glsl \
    --name test_variant \
    --speed 2.0

# Verify
python tools/shader_id_finder.py --map | grep test_variant

# Build and test
cmake -S . -B build
cmake --build build --config Release
.\build\Release\intro.exe
```

---

## Common Pitfalls & Solutions

### Pitfall 1: Curve System Lag Below 0.5
**Old design**: Neutral = 0.5, range [0.0, 1.0]  
**Problem**: Bidirectional control broken, lag below 0.5  
**Solution**: Neutral = 0.0, range [-2.0, +2.0]

### Pitfall 2: Shader Disappears with Random Values
**Problem**: Randomized exposure/fade can make shader invisible  
**Solution**: Add "Reset Values" button with safe defaults

### Pitfall 3: Stack Overflow with Multi-Object 3D
**Problem**: Large mesh arrays on stack  
**Solution**: Heap allocation via `std::vector`

### Pitfall 4: CMake String Literal Generation
**Problem**: Newlines in shader code break embedded strings  
**Solution**: Use `R"(...)"` raw string literals or escape properly

### Pitfall 5: Editor Changes Not Reflected in Runtime
**Problem**: Forgot to export before rebuilding  
**Solution**: Use "Do It All" button (Save → Export → Build → Run)

---

## Next Steps After Implementation

### 1. Content Creation
- Author 20+ shader scenes
- Create 3D meshes and textures
- Design scene progression
- Compose XM music track

### 2. Optimization Pass
- Profile frame time and startup
- Strip unused code paths
- Compress with kkrunchy/Crinkler
- Verify size target (64k/128k/256k)

### 3. Competition Prep
- Test on clean Windows 11 install
- Verify compo machine compatibility
- Package with README and screenshot
- Prepare backup submission

### 4. Documentation
- Update API reference with final interfaces
- Document all tuning knobs with defaults
- Create shader authoring examples
- Write competition-ready README

---

## Appendix: File Structure Reference

```
HowIMetYourMod/
├── src/
│   ├── app/
│   │   └── main_win32.cc          Entry point, frame loop
│   ├── platform/
│   │   ├── window_win32.h/.cc     Win32 window + WGL
│   │   └── timing.h/.cc           High-res timer
│   ├── renderer/
│   │   ├── shader.h/.cc           GL shader compilation
│   │   ├── texture.h/.cc          Image loading
│   │   └── mesh.h/.cc             3D mesh rendering (optional)
│   ├── sequence/
│   │   ├── timeline.h/.cc         Scene timing, transitions
│   │   └── curve.h/.cc            Curve evaluation
│   ├── content/
│   │   └── asset_loader.h/.cc     Load cues.txt, meshbin
│   └── audio/
│       └── xm_player.h/.cc        libxm wrapper
├── assets/
│   ├── shader/
│   │   ├── shader_common.glsl     Shared helpers
│   │   ├── shader_footer.glsl     Main function close
│   │   ├── fragment.glsl          Generated (gitignore)
│   │   ├── scenes/
│   │   │   ├── 00_plasma.glsl
│   │   │   ├── 01_tunnel.glsl
│   │   │   └── ... (38+ scenes)
│   │   └── presets.json           Shader preset library
│   ├── cues.txt                   Exported runtime data
│   ├── music/                     XM modules
│   ├── textures/                  PNG/JPG images
│   └── meshes/                    .meshbin files
├── tools/
│   ├── scene_block_editor.py      Main editor (8000+ lines)
│   ├── scene_block_editor_*.py    Helper modules
│   ├── shader_scene_duplicator.py Shader forking tool
│   ├── shader_id_finder.py        ID management
│   ├── obj_to_meshbin.py          OBJ/MTL converter
│   └── bake_text_to_png.py        Text rendering
├── .github/
│   ├── copilot-instructions.md    Workspace routing
│   ├── skills/                    Reusable AI skills
│   ├── agents/                    Specialized agents
│   └── instructions/              File-specific rules
├── docs/
│   ├── ARCHITECTURE.md            System design
│   ├── API-REFERENCE.md           C++ API docs
│   ├── CONTROLS-KNOBS-REFERENCE.md Tuning parameters
│   └── SHADER_TOOLS_REFERENCE.md  Tool documentation
├── CMakeLists.txt                 Build configuration
├── AGENTS.md                      Project guidelines
├── TECH-STACK.md                  Technology overview
└── OPENGL-EXPLAINER.md            WGL bootstrap guide
```

---

## Conclusion

This guide captures the complete architecture, patterns, and workflows to recreate HowIMetYourMod from scratch. Key success factors:

1. **Start simple**: Win32 window → WGL context → fullscreen quad → single shader
2. **Build incrementally**: Add one subsystem at a time, test thoroughly
3. **Keep runtime minimal**: Move complexity to editor and build system
4. **Author explicitly**: Never rely on runtime defaults or guessing
5. **Verify continuously**: Test both REV_ENABLE_3D modes, run "Do It All" often

With this foundation, you have a competition-ready intro/demo platform that's:
- **Reliable**: Deterministic, no surprises on compo machine
- **Compact**: Size-disciplined for intro profiles
- **Powerful**: 38+ shaders, optional 3D, curve-driven animation
- **Maintainable**: Clear ownership, explicit dependencies
- **Extensible**: Add shaders, effects, 3D meshes without core changes

**Good luck at Revision 2026!** 🚀

---

**Document Version**: 1.0  
**Last Updated**: May 30, 2026  
**Author**: Synthesized from HowIMetYourMod codebase and development history
