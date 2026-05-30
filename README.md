# HiMYM - Modular C++ Intro Framework

A lightweight, modular framework for creating demoscene intros and small demos. Built with size optimization and simplicity in mind.

## Project Status

**Phase 1: Foundation Libraries** ✅ COMPLETE
**Phase 2: Animation Libraries** ✅ COMPLETE
**Phase 3: Editor Foundation** ✅ INFRASTRUCTURE READY

- ✅ `rev_platform` - Win32 window + WGL OpenGL context + timing
- ✅ `rev_shader` - GLSL shader compilation and uniform management  
- ✅ `rev_xm` - XM music playback (stub, needs libxm)
- ✅ `rev_curve` - Time-value curves with easing
- ✅ `rev_sequence` - Timeline and cue system
- ✅ `rev_editor` - Editor infrastructure (needs Dear ImGui)
- ✅ Minimal intro integration test
- ✅ Animated intro with curves and timeline
- ✅ Editor application framework

**Next: Add ImGui for full editor UI**

## Quick Start

### Prerequisites

- Visual Studio 2022 (or Clang)
- CMake 3.20+
- Windows 10/11

### Build

```powershell
# Configure project
cmake -B build -G "Visual Studio 17 2022"

# Build
cmake --build build --config Release

# Run test
.\build\bin\Release\minimal_intro.exe
```

### Expected Output

The minimal intro test creates a window with an animated pulsing gradient. It runs for 10 seconds or exits on ESC.

## Project Structure

```
himym/
├── PR/                          # All project documentation
│   ├── ROADMAP.md              # Implementation plan
│   ├── LIBRARY_DESIGN.md       # API specifications
│   ├── FROM_SCRATCH_V2.md      # Architecture overview
│   └── guides/                 # Shader, editor, controls guides
├── revision_libs/              # Core libraries
│   ├── rev_platform/           # Platform abstraction (Win32/WGL)
│   ├── rev_shader/             # Shader compilation
│   ├── rev_xm/                 # XM music player (needs libxm)
│   ├── rev_curve/              # Animation curves
│   ├── rev_sequence/           # Timeline system
│   └── rev_editor/             # Editor framework (needs ImGui)
├── examples/
│   ├── minimal_intro/          # Phase 1 integration test
│   ├── animated_intro/         # Phase 2 animation test
│   └── editor_app/             # Phase 3 editor application
└── CMakeLists.txt              # Root build configuration
```
**Size**: ~15 KB

### rev_shader
GLSL shader compilation, program linking, uniform setters (float, vec2/3/4, mat4, int).
**Size**: ~10 KB

### rev_xm
XM module playback. Currently a stub - requires libxm integration (see `revision_libs/rev_xm/README.md`).
**Size**: ~30 KB (with libxm)

### rev_curve
Time-value curves with 6 easing modes (Linear, EaseIn/Out, EaseInOut, Smoothstep, Hold).
**Size**: ~8 KB

### rev_sequence
Timeline management with cue system, fade in/out, and opacity calculation.
**Size**: ~12 KB

### rev_editor
Editor infrastructure with project management, UI framework (requires Dear ImGui).
**Size**: ~150 KB (with ImGui

### rev_xm
XM module playback. Currently a stub - requires libxm integration (see `revision_libs/rev_xm/README.md`).

**Size**: ~30 KB (with libxm)

## Documentation

All project documentation is in the `PR/` folder:
Dear ImGui** for editor UI (optional)
   - Download from https://github.com/ocornut/imgui
   - Follow instructions in `revision_libs/rev_editor/README.md`

2. **Add libxm** for audio playback (optional)
   - Download from https://github.com/Artefact2/libxm
   - Follow instructions in `revision_libs/rev_xm/README.md`

3. **Continue with Phase 4**: Editor features
   - Timeline UI with visual cue editing
   - Curve editor canvas
   - Shader parameter modal
   - Export and build integration

4. **Phase 5**: 3D rendering (optional)
   - `rev_mesh` library for 3D models
2. **Phase 2: Animation Libraries** (see ROADMAP.md)
   - `rev_curve` - Time-value curves with easing
   - `rev_sequence` - Timeline and cue system

3. **Phase 3: Editor** (see ROADMAP.md)
   - ImGui integration
   - Timeline editor
   - Shader parameter editing
   - Project export to runtime format

## Target Goals

- **Intro size**: <30 KB compressed (kkrunchy/Crinkler)
- **Editor size**: <300 KB
- **Build time**: <5 seconds
- **No external runtime dependencies** (except Win32 + OpenGL)

## Development

### Coding Guidelines

- C++17 standard
- No exceptions or RTTI (size optimization)
- Minimal STL usage in runtime code
- Each library is independent and reusable
- Follow APIs exactly as specified in LIBRARY_DESIGN.md

### Build Flags

Release builds use aggressive size optimization:
- MSVC: `/O1 /GS- /GL /MT` + `/LTCG /OPT:REF /OPT:ICF`
- GCC/Clang: `-Os -fno-exceptions -fno-rtti`

## Contributing

This is a personal demoscene project following a specific design philosophy. The architecture and APIs are deliberately minimalist to achieve extreme size optimization.

If you find this useful, feel free to fork and adapt for your own productions!

## License

See individual library files for license information. Most code is intended to be public domain or MIT licensed for demoscene use.

---

**Created**: May 30, 2026  
**Phase**: 1 of 6  
**Status**: Foundation Complete ✅
