# HiMYM - Modular C++ Intro Framework

A comprehensive framework for creating demoscene intros and interactive demos. Built with modularity, size optimization, and clean architecture in mind.

## Project Status

**Phase 1: Foundation Libraries** ✅ COMPLETE
**Phase 2: Animation & Sequence** ✅ COMPLETE
**Phase 3: 3D Mesh & Content** ✅ COMPLETE
**Phase 4: Editor Infrastructure** ✅ COMPLETE
**Phase 5: Asset Packing** ✅ COMPLETE
**Phase 6: Integration & Runtime** ✅ COMPLETE

### Core Libraries (revision_libs/)

- ✅ `rev_platform` - Win32 window + WGL OpenGL 3.3 context + high-precision timing + input
- ✅ `rev_shader` - GLSL shader compilation, linking, and uniform management
- ✅ `rev_xm` - XM module music playback
- ✅ `rev_curve` - Time-value curves with 6 easing modes
- ✅ `rev_sequence` - Timeline system with cues, fading, and opacity
- ✅ `rev_mesh` - 3D mesh loading (glTF) and rendering with Phong shading
- ✅ `rev_gltf` - glTF 2.0 parser with full material and animation support
- ✅ `rev_editor` - Scene authoring framework with project management
- ✅ `rev_pack` - Asset packing and embedding system
- ✅ `rev_runtime` - Complete shared runtime for intro playback

### Example Applications

- ✅ `minimal_intro` - Phase 1 integration test (pulsing gradient)
- ✅ `animated_intro` - Phase 2 animation demo (curves + timeline)
- ✅ `demo_intro` - Full-featured demo with shaders and sequences
- ✅ `editor_app` - ImGui-based scene editor
- ✅ `mesh_demo` - 3D mesh rendering with lighting

## Quick Start

### Prerequisites

- **Windows 10/11** (64-bit)
- **Visual Studio 2022** (or Clang/Ninja)
- **CMake 3.20+**
- **C++17** capable compiler

### Build & Run

```powershell
# Configure (generates Visual Studio solution)
cmake -B build -G "Visual Studio 17 2022"

# Build Release (optimized for size)
cmake --build build --config Release

# Run minimal intro test
.\build\bin\Release\minimal_intro.exe

# Or run demo with full features
.\build\bin\Release\demo_intro.exe
```

### Expected Output

- **minimal_intro**: Window with pulsing animated gradient, exits after 10 seconds or on ESC
- **demo_intro**: Full-featured demo with shaders, meshes, text, and music timeline
- **editor_app**: ImGui-based scene editor for authoring new intros

## Project Structure

```
himym/
├── PR/                              # Complete documentation
│   ├── QUICK_START.md              # 5-minute setup guide
│   ├── FROM_SCRATCH_V2.md          # Architecture & design
│   ├── LIBRARY_DESIGN.md           # API specifications
│   ├── ROADMAP.md                  # Implementation milestones
│   ├── SUMMARY.md                  # Documentation index
│   ├── architecture/               # System design docs
│   ├── guides/                     # Feature walkthroughs
│   │   ├── EDITOR_GUIDE.md        # Scene authoring
│   │   ├── SHADER_GUIDE.md        # GLSL effects
│   │   ├── MESH_GUIDE.md          # 3D modeling
│   │   └── CONTROLS_KNOBS.md      # Runtime parameters
│   └── context/                    # AI customization
├── revision_libs/                   # Core libraries
│   ├── rev_platform/               # Win32/WGL/timing (~15 KB)
│   ├── rev_shader/                 # GLSL compilation (~10 KB)
│   ├── rev_xm/                     # XM music playback (~30 KB)
│   ├── rev_curve/                  # Animation curves (~8 KB)
│   ├── rev_sequence/               # Timeline + cues (~12 KB)
│   ├── rev_mesh/                   # 3D mesh rendering (~20 KB)
│   ├── rev_gltf/                   # glTF 2.0 parser (~25 KB)
│   ├── rev_editor/                 # ImGui editor (~150 KB)
│   ├── rev_pack/                   # Asset packing (~5 KB)
│   └── rev_runtime/                # Shared runtime (~50 KB)
├── examples/
│   ├── minimal_intro/              # Phase 1 test
│   ├── animated_intro/             # Phase 2 test
│   ├── demo_intro/                 # Full-featured demo
│   ├── editor_app/                 # Editor application
│   ├── mesh_demo/                  # 3D mesh rendering
│   └── (other test apps)
├── assets/                          # Project content
│   ├── textures/                   # Image files
│   ├── meshes/                     # 3D models (.meshbin, .mtl)
│   ├── music/                      # Audio tracks (.xm, .mod)
│   ├── shader/                     # GLSL shader sources
│   ├── fonts/                      # Text rendering
│   ├── cues.txt                    # Exported scene timeline
│   └── project.json                # Editor project file
├── build/                           # Compiler output (generated)
├── CMakeLists.txt                   # Root build configuration
└── BUILD.md                         # Build reference
```
## Library Overview

### rev_platform
Cross-platform window creation with OpenGL 3.3 context, high-precision timing, and input handling.
**Size**: ~15 KB | **Dependencies**: Win32 API, WGL

### rev_shader
GLSL vertex/fragment shader compilation, linking, and uniform management (float, vec2/3/4, mat4, int).
**Size**: ~10 KB | **Dependencies**: OpenGL 3.3

### rev_xm
XM module music playback with integration for timeline synchronization.
**Size**: ~30 KB | **Dependencies**: libxm (embedded)

### rev_curve
Parametric time-value curves with 6 easing modes (Linear, EaseIn/Out, EaseInOut, Smoothstep, Hold).
**Size**: ~8 KB | **Dependencies**: None

### rev_sequence
Timeline management with cue-based sequencing, fade transitions, and opacity control.
**Size**: ~12 KB | **Dependencies**: None

### rev_mesh
3D mesh loading and rendering with per-material texture support, Phong shading, and transparency handling.
**Size**: ~20 KB | **Dependencies**: OpenGL 3.3, GLM

### rev_gltf
Full glTF 2.0 parser supporting geometry, materials, textures, skeletal animation, and multiple mesh nodes.
**Size**: ~25 KB | **Dependencies**: None

### rev_editor
ImGui-based scene authoring with project management, timeline editing, shader modal, curve editor, and export pipeline.
**Size**: ~150 KB | **Dependencies**: Dear ImGui 1.89+

### rev_pack
Asset packing and embedding system for bundling textures, meshes, audio, and shaders into compact format.
**Size**: ~5 KB | **Dependencies**: None

### rev_runtime
Shared runtime core providing unified app lifecycle, sequence management, content loading, and frame rendering.
**Size**: ~50 KB | **Dependencies**: All core libraries

## Minimum Runtime

**Smallest buildable intro** (shader + timeline):
- rev_platform + rev_shader + rev_xm + rev_sequence = ~67 KB

**Full-featured intro** (with 3D + animations):
- All runtime libraries = ~170 KB

## Key Features

### Runtime Capabilities
- **Scene Timeline**: Cue-based sequencing with precise timing
- **Shader Effects**: GLSL vertex/fragment shaders with curve-driven parameters
- **3D Rendering**: glTF mesh support with Phong lighting and material slots
- **Text Rendering**: TTF font rendering with fade/animation curves
- **Audio Sync**: XM music playback with timeline markers
- **Animation Curves**: 6 easing modes for smooth parameter interpolation
- **Transparency Support**: Per-layer alpha blending and fade control
- **Fullscreen & Windowed**: Cross-resolution rendering with aspect correction

### Editor Features
- **Scene Authoring**: Timeline-based scene composition
- **Shader Modal**: Interactive shader parameter tuning with randomization
- **Curve Editor**: Visual animation curve creation and manipulation
- **Asset Browser**: Browse and organize meshes, textures, and audio
- **Live Preview**: See changes in real-time during authoring
- **Export Pipeline**: One-click export to runtime-ready format
- **Do It All**: Single command to save→export→build→run

## Documentation

Comprehensive guides are in the `PR/` folder:

- **[QUICK_START.md](PR/QUICK_START.md)** - Setup in 5 minutes
- **[FROM_SCRATCH_V2.md](PR/FROM_SCRATCH_V2.md)** - Architecture & design philosophy
- **[LIBRARY_DESIGN.md](PR/LIBRARY_DESIGN.md)** - Complete API reference
- **[ROADMAP.md](PR/ROADMAP.md)** - Implementation milestones
- **[guides/EDITOR_GUIDE.md](PR/guides/EDITOR_GUIDE.md)** - Scene authoring walkthrough
- **[guides/SHADER_GUIDE.md](PR/guides/SHADER_GUIDE.md)** - GLSL effect patterns
- **[guides/MESH_GUIDE.md](PR/guides/MESH_GUIDE.md)** - 3D model integration
- **[architecture/ARCHITECTURE.md](PR/architecture/ARCHITECTURE.md)** - System design overview
- **[architecture/API_REFERENCE.md](PR/architecture/API_REFERENCE.md)** - Public C++ APIs

## Workflow

### Create a New Intro

1. **Launch Editor**
   ```powershell
   .\build\bin\Release\editor_app.exe
   ```

2. **Add Scenes**
   - Create timeline with scenes
   - Configure shader effects, meshes, text, and audio

3. **Author Content**
   - Adjust shader parameters with curves
   - Position and animate 3D objects
   - Synchronize with music timeline

4. **Export & Run**
   - Click "Export" to generate `cues.txt`
   - Click "Build & Run" to compile and launch

### Minimal Example

Create a simple shader intro in `examples/my_intro/`:

```cpp
#include "rev_runtime.h"

extern const char* default_shader_glsl;

int main() {
    auto ctx = rev::CreateContext(1920, 1080, false, "My Intro");
    // ... main loop using rev::runtime APIs
    rev::DestroyContext(ctx);
    return 0;
}
```

See `examples/minimal_intro/` for complete example.

## Design Principles

1. **Modular**: Each library solves one problem
2. **Minimal**: Only include what you need
3. **Deterministic**: Reproducible behavior across runs
4. **Size-Conscious**: Track footprint per library
5. **Clean APIs**: Explicit contracts, no hidden state

## Customization

The workspace is configured with AI agent specializations in `.github/`:

- `@runtime-dev` - Runtime code and core systems
- `@editor-dev` - Editor UI and workflows
- `@mesh-graphics` - 3D rendering and geometry
- `@shader-author` - GLSL effects and shading
- `@build-system` - CMake configuration
- `@director` - Multi-domain coordination

## License

See LICENSE file in repository root.

## Contributing

For development guidance, see `PR/FROM_SCRATCH_V2.md` and domain-specific guides in `PR/guides/`.
